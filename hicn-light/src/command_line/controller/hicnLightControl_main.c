/*
 * Copyright (c) 2017-2019 Cisco and/or its affiliates.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <src/config.h>

#include <getopt.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <unistd.h>

#include <parc/assert/parc_Assert.h>
#include <string.h>

#include <parc/security/parc_IdentityFile.h>
#include <parc/security/parc_Security.h>

#include <parc/algol/parc_ArrayList.h>
#include <parc/algol/parc_List.h>
#include <parc/algol/parc_Memory.h>
#include <parc/algol/parc_SafeMemory.h>

#include <src/core/dispatcher.h>
#include <src/core/forwarder.h>

#include <errno.h>
#include <src/config/controlRoot.h>
#include <src/config/controlState.h>

#include <src/utils/commands.h>

size_t commandOutputLen = 0;  // preserve the number of structs composing
                              // payload in case on not interactive call.

// REMINDER: when a new_command is added, the following array has to be updated
// with the sizeof(new_command). It allows to allocate the buffer for receiving
// the payload of the DAEMON RESPONSE after the header has beed read. Each
// command identifier (typedef enum command_id) corresponds to a position in the
// following array.
static int payloadLengthController[LAST_COMMAND_VALUE] = {
    sizeof(add_listener_command),
    sizeof(add_connection_command),
    sizeof(list_connections_command),  // needed when get response from FWD
    sizeof(add_route_command),
    sizeof(list_routes_command),  // needed when get response from FWD
    sizeof(remove_connection_command),
    sizeof(remove_route_command),
    sizeof(cache_store_command),
    sizeof(cache_serve_command),
    0,  // cache clear
    sizeof(set_strategy_command),
    sizeof(set_wldr_command),
    sizeof(add_punting_command),
    sizeof(list_listeners_command),  // needed when get response from FWD
    sizeof(mapme_activator_command),
    sizeof(mapme_activator_command),
    sizeof(mapme_timing_command),
    sizeof(mapme_timing_command)};

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/uio.h>

typedef struct controller_main_state {
  ControlState *controlState;
} ControlMainState;

static void _displayForwarderLogo(void){
  const char cli_banner [] =
  "\033[0;31m   ____ ___      _       \033[0m  __    _               __ _        __   __\n"
  "\033[0;31m  / __// _ \\    (_)___  \033[0m  / /   (_)____ ___ ____/ /(_)___ _ / /  / /_\n"
  "\033[0;31m / _/ / // /_  / // _ \\ \033[0m / _ \\ / // __// _ \\___/ // // _ `// _ \\/ __/\n"
  "\033[0;31m/_/  /____/(_)/_/ \\___/ \033[0m/_//_//_/ \\__//_//_/  /_//_/ \\_, //_//_/\\__/\n"
  "                                                    /___/            \n";
  printf("%s", cli_banner);
  printf("\n");
}

static void _displayUsage(char *programName) {
  printf("Usage: %s -h\n", programName);
  printf(
      "hicn-light is the 1.0 source, which runs on each end system and as a "
      "software source\n");
  printf(
      "on intermediate systems.  controller is the program to configure the "
      "source, daemon.\n");
  printf("\n");
  printf("Options:\n");
  printf("-h              = This help screen\n");
  printf(
      "commands        = configuration line to send to hicn-light (use 'help' "
      "for list)\n");
  printf("\n");
}

static int _parseArgs(int argc, char *argv[], char **keystorePath,
                      char **keystorePassword, PARCList *commandList) {
  static struct option longFormOptions[] = {
      {"help", no_argument, 0, 'h'},
      {"keystore", required_argument, 0, 'k'},
      {"password", required_argument, 0, 'p'},
      {0, 0, 0, 0}};

  int c;

  while (1) {
    // getopt_long stores the option index here.
    int optionIndex = 0;

    c = getopt_long(argc, argv, "hk:p:", longFormOptions, &optionIndex);

    // Detect the end of the options.
    if (c == -1) {
      break;
    }

    switch (c) {
      case 'k':
        *keystorePath = optarg;
        break;

      case 'p':
        *keystorePassword = optarg;
        break;

      case 'h':
      default:
        _displayUsage(argv[0]);
        return 0;
    }
  }

  // Any remaining parameters get put in the command list.
  if (optind < argc) {
    while (optind < argc) {
      parcList_Add(commandList, argv[optind]);
      optind++;
    }
  }

  return 1;
}

struct iovec *_writeAndReadMessage(ControlState *state, struct iovec *msg) {
  parcAssertNotNull(msg, "Parameter msg must be non-null");
  int sockfd = controlState_GetSockfd(state);

  // check if request has a payload
  if (((header_control_message *)msg[0].iov_base)->length >
      0) {  // command with payload
    // write header + payload (compatibility issue: two write needed instead of
    // the writev)
    if (write(sockfd, msg[0].iov_base, msg[0].iov_len) < 0 ||
        write(sockfd, msg[1].iov_base, msg[1].iov_len) < 0) {
      printf("\nError while sending the Message: cannot write on socket \n");
      exit(EXIT_FAILURE);
    }
    parcMemory_Deallocate(&msg[1].iov_base);
  } else {  // command without payload, e.g. 'list'
    // write header only
    if (write(sockfd, msg[0].iov_base, msg[0].iov_len) < 0) {
      printf("\nError while sending the Message: cannot write on socket \n");
      exit(EXIT_FAILURE);
    }
  }
  parcMemory_Deallocate(&msg[0].iov_base);

  // ======= RECEIVE =======

  header_control_message *headerResponse =
      (header_control_message *)parcMemory_AllocateAndClear(
          sizeof(header_control_message));
  if (recv(sockfd, headerResponse, sizeof(header_control_message), 0) < 0) {
    printf("\nError in Receiving the Message \n");
    exit(EXIT_FAILURE);
  }

  if (headerResponse->messageType < RESPONSE_LIGHT ||
      headerResponse->messageType >= LAST_MSG_TYPE_VALUE) {
    char *checkFinMsg = parcMemory_Reallocate(headerResponse, 32);
    if (recv(sockfd, checkFinMsg, sizeof(checkFinMsg),
             MSG_PEEK | MSG_DONTWAIT) == 0) {
      // if recv returns zero, that means the connection has been closed:
      close(sockfd);
      printf("\nConnection terminated by the Daemon. Exiting... \n");
      exit(EXIT_SUCCESS);
    } else {
      printf("\nError: Unrecognized message type received \n");
      exit(EXIT_FAILURE);
    }
  }

  void *payloadResponse = NULL;

  if ((commandOutputLen = headerResponse->length) > 0) {
    payloadResponse = parcMemory_AllocateAndClear(
        payloadLengthController[headerResponse->commandID] *
        headerResponse->length);

    if (recv(sockfd, payloadResponse,
             payloadLengthController[headerResponse->commandID] *
                 headerResponse->length,
             0) < 0) {
      printf("\nError in Receiving the Message \n");
      exit(EXIT_FAILURE);
    }
  }

  struct iovec *response =
      parcMemory_AllocateAndClear(sizeof(struct iovec) * 2);

  response[0].iov_base = headerResponse;
  response[0].iov_len = sizeof(header_control_message);
  response[1].iov_base = payloadResponse;
  response[1].iov_len = payloadLengthController[headerResponse->commandID] *
                        headerResponse->length;

  return response;
}

int main(int argc, char *argv[]) {
  _displayForwarderLogo();

  if (argc == 2 && strcmp("-h", argv[1]) == 0) {
    _displayUsage(argv[0]);
    exit(EXIT_SUCCESS);
  }

  PARCList *commands =
      parcList(parcArrayList_Create(NULL), PARCArrayListAsPARCList);

  if (!_parseArgs(argc, argv, NULL, NULL, commands)) {
    parcList_Release(&commands);
    exit(EXIT_FAILURE);
  }

  ControlMainState mainState;
  mainState.controlState =
      controlState_Create(&mainState, _writeAndReadMessage, true);

  controlState_RegisterCommand(mainState.controlState,
                               controlRoot_HelpCreate(mainState.controlState));
  controlState_RegisterCommand(mainState.controlState,
                               controlRoot_Create(mainState.controlState));

  if (parcList_Size(commands) > 0) {
    controlState_SetInteractiveFlag(mainState.controlState, false);
    controlState_DispatchCommand(mainState.controlState, commands);
    char **commandOutputMain =
        controlState_GetCommandOutput(mainState.controlState);
    if (commandOutputMain != NULL && commandOutputLen > 0) {
      for (size_t j = 0; j < commandOutputLen; j++) {
        printf("Output %zu: %s \n", j, commandOutputMain[j]);
      }
      controlState_ReleaseCommandOutput(mainState.controlState,
                                        commandOutputMain, commandOutputLen);
    }
    // release

  } else {
    controlState_Interactive(mainState.controlState);
  }

  parcList_Release(&commands);

  controlState_Destroy(&mainState.controlState);

  return EXIT_SUCCESS;
}
# Copyright (c) 2017-2019 Cisco and/or its affiliates.
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at:
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

######################
# Packages section
######################

set(${HICN_PLUGIN}_DESCRIPTION
  "A high-performance Hybrid ICN forwarder as a plugin to VPP."
  CACHE STRING "Description for deb/rpm package."
)

set(${HICN_PLUGIN}_DEB_DEPENDENCIES
  "vpp (>= stable_version-release), vpp (<< next_version-release), vpp-plugin-core (>= stable_version-release), vpp-plugin-core (<< next_version-release)"
  CACHE STRING "Dependencies for deb/rpm package."
)

set(${HICN_PLUGIN}_RPM_DEPENDENCIES
  "vpp >= stable_version-release, vpp < next_version-release, vpp-plugins >= stable_version-release, vpp-plugins < next_version-release"
  CACHE STRING "Dependencies for deb/rpm package."
)

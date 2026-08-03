# Copyright 2026 Ian Pike
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# The suites that are about the library as a whole and not one module: header and ABI hygiene,
# the cross-backend contract campaign and the slow ones.

# Compiling each public header on its own is a build-time property so it is a library the
# conformance suite links and not a case it runs. A header that stopped standing alone fails the
# build here instead of failing for the first consumer who included it by itself.
azoth_rhi_add_header_self_containment_library(rhi_header_self_containment)

azoth_rhi_add_conformance_suite(rhi_conformance
        LABEL conformance)
target_link_libraries(rhi_conformance PRIVATE rhi_header_self_containment)

# The cross-backend campaign. Every backend this build bundled runs the same portable contract, which
# is what makes the Null backend a reference for the others, not only a stand-in for CI.
azoth_rhi_add_rigorous_suite(rhi_rigorous_campaign
        MODULE campaign
        LABEL rigorous
        TIMEOUT 600)

# Same sources, both shipping validation modes. eDeveloper tracks resource state, handle liveness and
# descriptor-arena epochs, eReleaseLight does not and each suite asserts what its own mode promises.
include(azoth_rhi_test_options)

azoth_rhi_collect_test_sources(rigorous/validation _validation_sources)
azoth_rhi_prefix_test_sources("../" "${_validation_sources}" _validation_prefixed)

foreach(_mode eDeveloper eReleaseLight eOff)
    string(TOLOWER "${_mode}" _mode_suffix)
    set(_target rhi_rigorous_validation_${_mode_suffix})

    azoth_rhi_add_rigorous_suite(${_target}
            SOURCES ${_validation_prefixed}
            LABEL rigorous
            TIMEOUT 300)
    azoth_rhi_apply_validation_mode(${_target} ${_mode})
endforeach()

if(AZOTH_RHI_BUILD_STRESS_TESTS)
    azoth_rhi_add_rigorous_suite(rhi_rigorous_stress
            MODULE stress
            LABEL stress
            TIMEOUT 1800)
endif()

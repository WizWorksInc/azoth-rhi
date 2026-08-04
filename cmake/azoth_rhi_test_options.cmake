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

# Configuration axes a suite can be rebuilt along.
#
# Most of what the RHI promises is conditional on the validation mode a device was created with:
# eDeveloper tracks resource states, handle liveness and descriptor-arena epochs, while
# eReleaseLight skips the tracking that costs. Both are shipping configurations so the contract has
# to be pinned in both, which means compiling the same sources twice without branching inside
# them. The default fixture reads AZOTH_RHI_TEST_VALIDATION_MODE so a suite opts in by asking for a
# mode here and changes nothing in its own code.

include_guard(GLOBAL)

set(AZOTH_RHI_TEST_VALIDATION_MODES eOff eReleaseLight eDeveloper eCapture)

# The mode every suite is built for when it does not ask for its own, which is the axis a run sweeps to
# check the contract in each shipping configuration rather than only the one the header defaults to.
# eDeveloper matches that default, so leaving this alone changes nothing.
set(AZOTH_RHI_TEST_VALIDATION_MODE "eDeveloper" CACHE STRING "ValidationMode every test suite is built for unless it asks for its own")
set_property(CACHE AZOTH_RHI_TEST_VALIDATION_MODE PROPERTY STRINGS ${AZOTH_RHI_TEST_VALIDATION_MODES})

function(azoth_rhi_apply_validation_mode target mode)
    if(NOT TARGET ${target})
        message(FATAL_ERROR "azoth_rhi_apply_validation_mode: no such target: ${target}")
    endif()
    if(NOT mode IN_LIST AZOTH_RHI_TEST_VALIDATION_MODES)
        message(FATAL_ERROR "azoth_rhi_apply_validation_mode(${target}): ${mode} is not a ValidationMode enumerator.")
    endif()

    # Last call wins. A suite that names its own mode is applied over the build-wide one, and two -D of the
    # same name is a redefinition whose winner is the compiler's business and not something a suite should
    # be reading its own configuration out of.
    get_target_property(_existing ${target} COMPILE_DEFINITIONS)
    if(_existing)
        list(FILTER _existing EXCLUDE REGEX "^AZOTH_RHI_TEST_(VALIDATION_MODE|CONFIGURATION_NAME)=")
        set_target_properties(${target} PROPERTIES COMPILE_DEFINITIONS "${_existing}")
    endif()

    target_compile_definitions(${target} PRIVATE
            AZOTH_RHI_TEST_VALIDATION_MODE=${mode}
            AZOTH_RHI_TEST_CONFIGURATION_NAME="validation-${mode}"
    )
endfunction()

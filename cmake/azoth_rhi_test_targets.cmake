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

# Convenience targets that run one ctest label from the build tree root so a suite can be run from
# an IDE without remembering the label spelling.

include_guard(GLOBAL)

if(NOT AZOTH_RHI_BUILD_TESTS)
    return()
endif()

include(ProcessorCount)
ProcessorCount(_azoth_rhi_ctest_jobs)
if(_azoth_rhi_ctest_jobs EQUAL 0)
    set(_azoth_rhi_ctest_jobs 1)
endif()

function(_azoth_rhi_add_ctest_target target_name label)
    set(_args --output-on-failure -j ${_azoth_rhi_ctest_jobs})

    if(CMAKE_CONFIGURATION_TYPES)
        list(APPEND _args -C $<CONFIG>)
    endif()

    if(label)
        list(APPEND _args -L ${label})
        set(_comment "Running ctest label ${label}")
    else()
        set(_comment "Running every ctest test")
    endif()

    add_custom_target(${target_name}
            COMMAND ${CMAKE_CTEST_COMMAND} ${_args}
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
            USES_TERMINAL
            COMMENT ${_comment}
    )
endfunction()

if(AZOTH_RHI_BUILD_UNIT_TESTS)
    _azoth_rhi_add_ctest_target(rhi_ctest_unit unit)
endif()

if(AZOTH_RHI_BUILD_RIGOROUS_TESTS)
    _azoth_rhi_add_ctest_target(rhi_ctest_conformance conformance)
    _azoth_rhi_add_ctest_target(rhi_ctest_rigorous rigorous)

    if(AZOTH_RHI_BUILD_STRESS_TESTS)
        _azoth_rhi_add_ctest_target(rhi_ctest_stress stress)
    endif()
endif()

_azoth_rhi_add_ctest_target(rhi_ctest_all "")

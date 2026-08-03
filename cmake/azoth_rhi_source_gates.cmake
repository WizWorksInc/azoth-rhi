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

# The source scanning gates, which are one shape repeated: run a script over the tree, fail the build on
# a violation, and stand as a ctest case as well so a suite run covers what a build would have caught.
#
# Stamped, not run outright. A custom target with no output re-runs whatever else happened, so
# five whole-tree scans were the fixed price of an incremental build that changed nothing at all.

set(_azoth_rhi_gate_globs)
foreach(_root lib/include lib/src tests examples benchmarks utils extras)
    foreach(_ext hpp h hh cpp cc cxx mm inl ipp)
        list(APPEND _azoth_rhi_gate_globs "${PROJECT_SOURCE_DIR}/${_root}/*.${_ext}")
    endforeach()
endforeach()

# Deliberately a superset of what any one script reads and not each script's own roots repeated here.
#
# A superset can do nothing worse than re-run a gate that had no reason to. Naming the roots here would
# put the same list in two places, and a script that later grew a root this list did not know about
# would quietly stop being re-run: a gate that has gone silent looks exactly like a gate that passes,
# which is the failure these gates exist to catch in the first place.
file(GLOB_RECURSE AZOTH_RHI_GATE_INPUTS CONFIGURE_DEPENDS ${_azoth_rhi_gate_globs})

file(MAKE_DIRECTORY "${PROJECT_BINARY_DIR}/gates")

# stem names cmake/azoth_rhi_<stem>.cmake and the target built from it. test_name is separate because a
# gate's ctest name says what is being promised, not which file checks it.
function(azoth_rhi_add_source_gate stem test_name comment)
    set(_script "${PROJECT_SOURCE_DIR}/cmake/azoth_rhi_${stem}.cmake")
    set(_stamp "${PROJECT_BINARY_DIR}/gates/${stem}.stamp")

    if(NOT EXISTS "${_script}")
        message(FATAL_ERROR "azoth_rhi_add_source_gate(${stem}): no such script: ${_script}")
    endif()

    add_custom_command(OUTPUT "${_stamp}"
            COMMAND ${CMAKE_COMMAND} -DAZOTH_RHI_SOURCE_ROOT=${PROJECT_SOURCE_DIR} -P "${_script}"
            COMMAND ${CMAKE_COMMAND} -E touch "${_stamp}"
            DEPENDS ${AZOTH_RHI_GATE_INPUTS} "${_script}"
            COMMENT "${comment}"
            VERBATIM
    )

    add_custom_target(azoth_rhi_${stem} ALL DEPENDS "${_stamp}")

    # The ctest entry runs the script and not the stamp, so a suite reports the violation itself
    # instead of reporting that a build step did not produce a file.
    if(AZOTH_RHI_BUILD_TESTS)
        add_test(NAME ${test_name}
                COMMAND ${CMAKE_COMMAND} -DAZOTH_RHI_SOURCE_ROOT=${PROJECT_SOURCE_DIR} -P "${_script}"
        )
        set_tests_properties(${test_name} PROPERTIES LABELS conformance)
    endif()
endfunction()

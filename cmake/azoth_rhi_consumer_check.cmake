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

# Installs this build, then configures and builds a separate project against the result.
#
# Without this, a broken export or a header missing from the install rules passes every other gate
# because every other check in this repository compiles against the source tree, which is the one
# arrangement a stranger never has.
#
# Run through ctest so it is a test and not a thing somebody remembers to do:
#
#     cmake -DAZOTH_RHI_SOURCE_ROOT=<repo> -DAZOTH_RHI_BUILD_ROOT=<build> \
#           -DAZOTH_RHI_CMAKE=<cmake> -DAZOTH_RHI_CONFIG=<config> -P cmake/azoth_rhi_consumer_check.cmake

cmake_minimum_required(VERSION 3.24)

foreach(_required AZOTH_RHI_SOURCE_ROOT AZOTH_RHI_BUILD_ROOT)
    if(NOT DEFINED ${_required})
        message(FATAL_ERROR "azoth_rhi_consumer_check: pass -D${_required}=<path>.")
    endif()
endforeach()

if(NOT DEFINED AZOTH_RHI_CMAKE)
    set(AZOTH_RHI_CMAKE "${CMAKE_COMMAND}")
endif()
# An empty configuration is a real answer, not a missing one: a single config generator with no
# CMAKE_BUILD_TYPE builds and exports under no configuration at all. Defaulting that to Release invents one
# the library was never built in and the consumer then fails looking for an import that does not exist.
# So an empty config is carried through as empty and the config arguments are simply not passed.
set(_configArgs)
set(_consumerConfigArgs)
if(DEFINED AZOTH_RHI_CONFIG AND NOT AZOTH_RHI_CONFIG STREQUAL "")
    set(_configArgs --config "${AZOTH_RHI_CONFIG}")
    set(_consumerConfigArgs "-DCMAKE_BUILD_TYPE=${AZOTH_RHI_CONFIG}")
endif()

set(_work "${AZOTH_RHI_BUILD_ROOT}/consumer-check")
set(_prefix "${_work}/prefix")
set(_build "${_work}/build")

# Fresh every run. A stale prefix would let a header that stopped being installed keep passing, which is
# the same shape as the gate roots that went quiet when the tree moved.
file(REMOVE_RECURSE "${_work}")
file(MAKE_DIRECTORY "${_prefix}")

# Runs a step and reports its own output on failure, since a configure error inside a nested project is
# useless when all that escapes is a status code.
function(_azoth_run_step description)
    execute_process(COMMAND ${ARGN}
            RESULT_VARIABLE _status
            OUTPUT_VARIABLE _out
            ERROR_VARIABLE _err)

    if(NOT _status EQUAL 0)
        message(FATAL_ERROR
                "AzothRHI consumer check failed while ${description}.\n\n"
                "This means an installed AzothRHI could not be used by a project outside this tree, which "
                "no other gate here can see: every one of them compiles against the source.\n\n"
                "--- stdout ---\n${_out}\n--- stderr ---\n${_err}")
    endif()
endfunction()

_azoth_run_step("installing the build"
        "${AZOTH_RHI_CMAKE}" --install "${AZOTH_RHI_BUILD_ROOT}" --prefix "${_prefix}" ${_configArgs})

_azoth_run_step("configuring a consumer against the installed prefix"
        "${AZOTH_RHI_CMAKE}" -S "${AZOTH_RHI_SOURCE_ROOT}/tests/consumer" -B "${_build}"
        ${_consumerConfigArgs}
        -DCMAKE_PREFIX_PATH=${_prefix})

_azoth_run_step("building the consumer"
        "${AZOTH_RHI_CMAKE}" --build "${_build}" ${_configArgs})

# Finding the built program without assuming where a generator put it, since multi-config generators
# put it one directory deeper and this has to pass under all of them.
file(GLOB_RECURSE _programs "${_build}/consumer_backend" "${_build}/consumer_backend.exe")
list(LENGTH _programs _found)
if(NOT _found EQUAL 1)
    message(FATAL_ERROR "AzothRHI consumer check: expected one consumer_backend under ${_build}, found ${_found}.")
endif()

list(GET _programs 0 _program)
_azoth_run_step("running the out of tree backend" "${_program}")

# And the module, which is built but never run: a loadable module is loaded by a host and the in-tree
# suite already proves that half. What this adds is that one can be built at all from outside.
file(GLOB_RECURSE _modules "${_build}/consumer_module.*")
list(LENGTH _modules _moduleCount)
if(_moduleCount EQUAL 0)
    message(FATAL_ERROR "AzothRHI consumer check: the module SDK produced no loadable module outside the tree.")
endif()

message(STATUS "AzothRHI: consumer check OK, an out of tree backend and module built and ran against the installed prefix.")

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

# AZOTH_RHI_SANITIZER, one of OFF, THREAD, ADDRESS or UNDEFINED.
#
# The threading model this library promises is a set of claims about what may be called concurrently
# and a suite that passes without a sanitizer says only that no race happened to be observed on the
# run. The verification gate for that model asks for a thread sanitizer by name so building one has
# to be a configure flag and not a command line someone reconstructs from memory.
#
# Applied to everything this project compiles, since a sanitizer that covers the library and not the
# tests driving it reports races in neither.

set(AZOTH_RHI_SANITIZER "OFF" CACHE STRING "Sanitizer to build with: OFF, THREAD, ADDRESS, or UNDEFINED")
set_property(CACHE AZOTH_RHI_SANITIZER PROPERTY STRINGS OFF THREAD ADDRESS UNDEFINED)

# A returned-to stack frame is the one thing the address sanitizer does not catch by default, because
# catching it means keeping every frame alive in a shadow stack and paying for it on every call. Off
# unless asked for, and only meaningful beside ADDRESS.
option(AZOTH_RHI_SANITIZER_USE_AFTER_RETURN "Detect use of stack memory after the frame holding it returned, with AZOTH_RHI_SANITIZER=ADDRESS" OFF)

function(azoth_rhi_apply_sanitizer)
    string(TOUPPER "${AZOTH_RHI_SANITIZER}" _mode)

    # Refused, not ignored, so a build asked for a check it is not getting says so at configure
    # time and not by reporting nothing on a run someone trusted.
    if(AZOTH_RHI_SANITIZER_USE_AFTER_RETURN AND NOT _mode STREQUAL "ADDRESS")
        message(FATAL_ERROR
                "AzothRHI: AZOTH_RHI_SANITIZER_USE_AFTER_RETURN is part of the address sanitizer, "
                "so it needs AZOTH_RHI_SANITIZER=ADDRESS and not ${AZOTH_RHI_SANITIZER}.")
    endif()

    if(_mode STREQUAL "OFF" OR _mode STREQUAL "")
        return()
    endif()

    if(NOT _mode MATCHES "^(THREAD|ADDRESS|UNDEFINED)$")
        message(FATAL_ERROR
                "AzothRHI: AZOTH_RHI_SANITIZER is ${AZOTH_RHI_SANITIZER}, which is not one of OFF, THREAD, ADDRESS, or UNDEFINED.")
    endif()

    if(MSVC AND NOT _mode STREQUAL "ADDRESS")
        message(FATAL_ERROR "AzothRHI: MSVC ships only the address sanitizer, so AZOTH_RHI_SANITIZER=${_mode} cannot be built here.")
    endif()

    if(MSVC AND AZOTH_RHI_SANITIZER_USE_AFTER_RETURN)
        message(FATAL_ERROR "AzothRHI: MSVC's address sanitizer has no use-after-return check, so AZOTH_RHI_SANITIZER_USE_AFTER_RETURN cannot be built here.")
    endif()

    # Symbolized frames are the whole value of a report. A sanitizer build is already slow enough that
    # trading the optimizer for a readable stack is not a trade worth offering as an option.
    #
    # Picked per driver, not per mode alone. MSVC takes none of the spellings below, so the branch that
    # let ADDRESS through on MSVC used to hand cl.exe a -fsanitize it cannot read.
    if(MSVC)
        # Two Debug defaults are refused outright beside this sanitizer, not merely ignored, so
        # they come back out of the flags instead of failing the build they were never asked about.
        string(REPLACE "/RTC1" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
        set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}" PARENT_SCOPE)

        add_compile_options(/fsanitize=address /Zi)
        add_link_options(/DEBUG /INCREMENTAL:NO)
    else()
        if(_mode STREQUAL "THREAD")
            set(_flags "-fsanitize=thread")
        elseif(_mode STREQUAL "ADDRESS")
            set(_flags "-fsanitize=address" "-fno-omit-frame-pointer")

            # always, not runtime, so the shadow stack is there whether or not the environment also
            # asks for it. runtime leaves the decision to ASAN_OPTIONS, which is the thing this option
            # exists to stop anyone having to remember.
            if(AZOTH_RHI_SANITIZER_USE_AFTER_RETURN)
                list(APPEND _flags "-fsanitize-address-use-after-return=always")
            endif()
        else()
            set(_flags "-fsanitize=undefined" "-fno-omit-frame-pointer")
        endif()

        add_compile_options(${_flags} -g)
        add_link_options(${_flags})
    endif()

    message(STATUS "AzothRHI: building with the ${_mode} sanitizer")

    # A sanitizer runtime that cannot start is a real possibility and it looks like a passing run, since
    # a binary that dies before main reports no findings. Whether a given toolchain has that problem is
    # a property of the machine, not of this project so it is not diagnosed here.
endfunction()

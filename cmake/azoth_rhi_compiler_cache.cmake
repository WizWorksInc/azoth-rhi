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

# Routes compiles through ccache or sccache when one is installed.
#
# Worth having because of how this tree is edited, not how large it is. The dispatch header and
# the block headers reach most of the backend translation units, which are the expensive ones so a
# change there rebuilds about a third of the project. Reverting it rebuilds the same third back to
# bytes that already existed. A cache makes the second of those free, which is most of what a bisect,
# a revert or trying a change and dropping it actually costs.
#
# Off when nothing is found so a machine without one configures exactly as before and skipped when
# the caller has already set a launcher, since that is someone saying they want their own.

option(AZOTH_RHI_COMPILER_CACHE "Route compiles through ccache or sccache when one is installed" ON)

function(azoth_rhi_apply_compiler_cache)
    if(NOT AZOTH_RHI_COMPILER_CACHE)
        return()
    endif()

    # MSVC and a cache disagree about debug information: /Zi writes to a shared PDB the cache cannot
    # account for so a hit restores an object whose debug records were never written. Left alone
    # there, not silently changing what a debugger can see.
    if(MSVC)
        return()
    endif()

    find_program(AZOTH_RHI_COMPILER_CACHE_PROGRAM NAMES ccache sccache)

    # Anything already there that is not what we would have put there is the caller's own choice and a
    # reconfigure finding our own value must not report that as someone else's.
    if(CMAKE_CXX_COMPILER_LAUNCHER AND NOT CMAKE_CXX_COMPILER_LAUNCHER STREQUAL "${AZOTH_RHI_COMPILER_CACHE_PROGRAM}")
        message(STATUS "AzothRHI: compiler cache left alone, CMAKE_CXX_COMPILER_LAUNCHER is already ${CMAKE_CXX_COMPILER_LAUNCHER}")
        return()
    endif()

    if(NOT AZOTH_RHI_COMPILER_CACHE_PROGRAM)
        return()
    endif()

    set(CMAKE_CXX_COMPILER_LAUNCHER "${AZOTH_RHI_COMPILER_CACHE_PROGRAM}" CACHE STRING "Program compiles are routed through" FORCE)
    message(STATUS "AzothRHI: compiling through ${AZOTH_RHI_COMPILER_CACHE_PROGRAM}")
endfunction()

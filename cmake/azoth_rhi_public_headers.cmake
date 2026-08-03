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

# A public header is the same header in every build. Which backends were compiled in is answered at
# runtime by rhi::AvailableBackends, in the same terms a backend of your own answers in so nothing
# under include has any business branching on it.
#
# The AZOTH_RHI_BACKEND_* definitions are PRIVATE to the library target, which means a public header
# testing one would not see it from a consumer's build and would quietly compile to the wrong thing
# without failing. This checks that none does.
#
#     cmake -DAZOTH_RHI_SOURCE_ROOT=<repo> -P cmake/azoth_rhi_public_headers.cmake

cmake_minimum_required(VERSION 3.24)

if(NOT DEFINED AZOTH_RHI_SOURCE_ROOT)
    message(FATAL_ERROR "azoth_rhi_public_headers: pass -DAZOTH_RHI_SOURCE_ROOT=<repo root>.")
endif()

file(GLOB_RECURSE _headers "${AZOTH_RHI_SOURCE_ROOT}/lib/include/*.hpp")

# Conditional compilation only. Naming the AZOTH_RHI_BACKEND environment variable in a doc comment
# is not what this is about.
set(_forbidden "^[ \t]*#[ \t]*(if|ifdef|ifndef|elif)[^\n]*AZOTH_RHI_BACKEND")

set(_violations)
foreach(_header IN LISTS _headers)
    file(STRINGS "${_header}" _lines REGEX "${_forbidden}")
    foreach(_line IN LISTS _lines)
        file(RELATIVE_PATH _rel "${AZOTH_RHI_SOURCE_ROOT}" "${_header}")
        string(STRIP "${_line}" _trimmed)
        list(APPEND _violations "  ${_rel}\n      ${_trimmed}")
    endforeach()
endforeach()

list(LENGTH _headers _scanned)
if(_violations)
    list(JOIN _violations "\n" _report)
    message(FATAL_ERROR
            "AzothRHI public header contract violated: a public header branches on which backends this build has.\n"
            "Those definitions are PRIVATE to the library, so a consumer compiling this header would not see them "
            "and would get the other branch without a word about it. Ask rhi::AvailableBackends or "
            "rhi::FindAvailableBackend at runtime instead, which is also the only form a backend of your own can "
            "answer.\n\n${_report}\n")
endif()
# A root that stops resolving makes this quieter, not redder, which is how a stale one once went on
# printing OK over a third of the tree. The floor is the count at the last deliberate change: raise it as
# headers are added, lower it only when headers are genuinely removed.
set(_floor 76)
if(_scanned LESS _floor)
    message(FATAL_ERROR
            "AzothRHI public header contract scanned ${_scanned} headers, fewer than the ${_floor} this check covers.\n"
            "A scan root has most likely stopped resolving, which leaves the check reporting OK over a smaller tree "
            "than it was written for.\nFix the root, or lower the floor in cmake/azoth_rhi_public_headers.cmake if "
            "headers were genuinely removed.\n")
endif()
message(STATUS "AzothRHI: public header contract OK, ${_scanned} headers scanned, none branch on the build's backends.")

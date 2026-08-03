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

# The library allocates through the installed HostAllocator and nothing else so a host that installs one
# accounts for every byte the RHI takes. That holds only while no translation unit reaches for a std::
# container or make_unique directly, which is a one-line mistake to make and invisible until someone
# measures. This checks it.
#
#     cmake -DAZOTH_RHI_SOURCE_ROOT=<repo> -P cmake/azoth_rhi_host_allocations.cmake

cmake_minimum_required(VERSION 3.24)

if(NOT DEFINED AZOTH_RHI_SOURCE_ROOT)
    message(FATAL_ERROR "azoth_rhi_host_allocations: pass -DAZOTH_RHI_SOURCE_ROOT=<repo root>.")
endif()

# src is the library and so is include/azoth/rhi/backend: publishing a header for backend
# authors moved where it lives, not what it is. Missing that second root would have quietly stopped
# checking the dispatch tables, the block headers and the resource tables the day they were shipped.
#
# The conformance suite is not here and could not be. It allocates for its own bookkeeping while it
# runs and not on a host's behalf so the seam has nothing to say about it and it lives under
# tests/support/conformance, outside every root this scans.
set(_globs)
foreach(_ext hpp h hh cpp cc cxx mm inl ipp)
    list(APPEND _globs "${AZOTH_RHI_SOURCE_ROOT}/lib/src/*.${_ext}")
    list(APPEND _globs "${AZOTH_RHI_SOURCE_ROOT}/lib/include/azoth/rhi/backend/*.${_ext}")
endforeach()
file(GLOB_RECURSE _files ${_globs})

# The aliases are defined in terms of the std templates so the header that declares them is the one
# place these names are allowed to appear.
set(_alias_header "${AZOTH_RHI_SOURCE_ROOT}/lib/include/azoth/rhi/backend/support/host_containers.hpp")

set(_forbidden "std::(vector|unordered_map|unordered_set|deque|list|set|map|string|make_unique|make_shared|unique_ptr|shared_ptr)[<(]")

set(_violations)
foreach(_file IN LISTS _files)
    if(_file STREQUAL "${_alias_header}")
        continue()
    endif()

    file(STRINGS "${_file}" _lines REGEX "${_forbidden}")
    foreach(_line IN LISTS _lines)
        # std::string_view owns no storage and the adapter names the std template it wraps.
        if(_line MATCHES "std::string_view" OR _line MATCHES "HostAllocatorAdapter")
            continue()
        endif()

        file(RELATIVE_PATH _rel "${AZOTH_RHI_SOURCE_ROOT}" "${_file}")
        string(STRIP "${_line}" _trimmed)
        list(APPEND _violations "  ${_rel}\n      ${_trimmed}")
    endforeach()
endforeach()

list(LENGTH _files _scanned)
if(_violations)
    list(JOIN _violations "\n" _report)
    message(FATAL_ERROR
            "AzothRHI host allocation contract violated: a library source allocates outside the installed HostAllocator.\n"
            "Use the detail::Host aliases (HostVector, HostMap, HostSet, HostDeque, HostString) or HostNew instead, "
            "so an installed allocator sees this memory too. A driver wrapper that allocates for the RHI takes one of "
            "the same adapters, the way the Vulkan enumerate calls name theirs.\n\n${_report}\n")
endif()
# A root that stops resolving makes this quieter, not redder, which is how a stale one once went on
# printing OK over a third of the tree. The floor is the count at the last deliberate change: raise it as
# files are added, lower it only when files are genuinely removed.
set(_floor 88)
if(_scanned LESS _floor)
    message(FATAL_ERROR
            "AzothRHI host allocation contract scanned ${_scanned} sources, fewer than the ${_floor} this check covers.\n"
            "A scan root has most likely stopped resolving, which leaves the check reporting OK over a smaller tree "
            "than it was written for.\nFix the root, or lower the floor in cmake/azoth_rhi_host_allocations.cmake if "
            "files were genuinely removed.\n")
endif()
message(STATUS "AzothRHI: host allocation contract OK, ${_scanned} library sources scanned, none allocate outside the seam.")

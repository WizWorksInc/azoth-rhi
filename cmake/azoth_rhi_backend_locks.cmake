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

# A backend author writes no synchronization. The RHI guards what needs guarding, one lock per
# ResourceType around a device's creates and destroys and one process scoped lock around the
# four entries that bring instances and devices into and out of existence.
#
# That promise is what lets someone write a backend without knowing which of its entries can be
# reached concurrently. A backend guarding its own instance and device lists would be one rule
# written four times and a fifth an out of tree author would have to infer from backends they do
# not have.
#
# A check, not prose because prose cannot fail. What it scans is the bundled backends, which
# are the worked examples a stranger copies.
#
#     cmake -DAZOTH_RHI_SOURCE_ROOT=<repo> -P cmake/azoth_rhi_backend_locks.cmake

cmake_minimum_required(VERSION 3.24)

if(NOT DEFINED AZOTH_RHI_SOURCE_ROOT)
    message(FATAL_ERROR "azoth_rhi_backend_locks: pass -DAZOTH_RHI_SOURCE_ROOT=<repo root>.")
endif()

set(_globs)
foreach(_ext hpp h cpp cc cxx mm inl ipp)
    list(APPEND _globs "${AZOTH_RHI_SOURCE_ROOT}/lib/src/backends/*.${_ext}")
endforeach()
file(GLOB_RECURSE _files ${_globs})

# Every spelling of taking a lock or hand rolling one. std::atomic is deliberately not here: a backend
# reads counters a driver writes from its own threads, the Vulkan and Direct3D 12 debug messengers
# being the case and no RHI guard can reach a callback the driver raises. An atomic counter is not
# the backend synchronizing its own entries against each other, which is what this check is about.
set(_patterns
        "std::mutex"
        "std::shared_mutex"
        "std::recursive_mutex"
        "std::timed_mutex"
        "std::scoped_lock"
        "std::lock_guard"
        "std::unique_lock"
        "std::shared_lock"
        "std::condition_variable"
        "std::call_once"
        "std::once_flag"
        "SpinLock"
        "pthread_mutex"
        "InitializeCriticalSection"
        "EnterCriticalSection"
)
list(JOIN _patterns "|" _joined)

set(_violations)
foreach(_file IN LISTS _files)
    file(STRINGS "${_file}" _lines REGEX "${_joined}")
    foreach(_line IN LISTS _lines)
        string(STRIP "${_line}" _trimmed)

        # A comment saying why there is no lock is the opposite of a violation.
        if(_trimmed MATCHES "^(//|\\*|/\\*)")
            continue()
        endif()

        file(RELATIVE_PATH _rel "${AZOTH_RHI_SOURCE_ROOT}" "${_file}")
        list(APPEND _violations "  ${_rel}\n      ${_trimmed}")
    endforeach()
endforeach()

list(LENGTH _files _scanned)
if(_violations)
    list(JOIN _violations "\n" _report)
    message(FATAL_ERROR
            "AzothRHI backend lock violated: a bundled backend synchronizes itself.\n"
            "A backend author writes none. The RHI takes one lock per ResourceType around a device's "
            "creates and destroys, and detail::LifetimeLock around createInstance, createDevice, destroyDevice "
            "and destroyInstance, so a backend's own object lists are already serialized.\nIf something here is "
            "genuinely outside every one of those, it needs a threading model row saying so before it needs a "
            "lock.\n\n${_report}\n")
endif()
# A root that stops resolving makes this quieter, not redder, which is how a stale one once went on
# printing OK over a third of the tree. The floor is the count at the last deliberate change: raise it as
# files are added, lower it only when files are genuinely removed.
set(_floor 54)
if(_scanned LESS _floor)
    message(FATAL_ERROR
            "AzothRHI backend locks scanned ${_scanned} files, fewer than the ${_floor} this check covers.\n"
            "A scan root has most likely stopped resolving, which leaves the check reporting OK over a smaller tree "
            "than it was written for.\nFix the root, or lower the floor in cmake/azoth_rhi_backend_locks.cmake if "
            "files were genuinely removed.\n")
endif()
message(STATUS "AzothRHI: backend locks OK, ${_scanned} files scanned, no bundled backend synchronizes itself.")

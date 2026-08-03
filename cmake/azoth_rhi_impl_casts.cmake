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

# A facade's impl pointer is whatever the layer above the backend put there, which with validation on
# is a wrapper and not the backend's own object. Casting it straight across reads a wrapper's
# fields as a device's and because ImplOf hands back void, the compiler has nothing to say about it.
#
# A native handle accessor doing that hands back an object that was never one and nothing fails
# until a driver dereferences it several calls later. This is what makes that unspellable rather
# than something to be caught by review.
#
# Two spellings are allowed and both say which question they are answering:
#
#   NativeImplOf     resolve through whatever is in front, for the native handle accessors
#   UnwrappedImplOf  the caller already knows there is nothing in front, for the submit entries
#
#     cmake -DAZOTH_RHI_SOURCE_ROOT=<repo> -P cmake/azoth_rhi_impl_casts.cmake

cmake_minimum_required(VERSION 3.24)

if(NOT DEFINED AZOTH_RHI_SOURCE_ROOT)
    message(FATAL_ERROR "azoth_rhi_impl_casts: pass -DAZOTH_RHI_SOURCE_ROOT=<repo root>.")
endif()

set(_globs)
foreach(_root "${AZOTH_RHI_SOURCE_ROOT}/lib/include" "${AZOTH_RHI_SOURCE_ROOT}/lib/src" "${AZOTH_RHI_SOURCE_ROOT}/tests"
        "${AZOTH_RHI_SOURCE_ROOT}/examples" "${AZOTH_RHI_SOURCE_ROOT}/benchmarks")
    foreach(_ext hpp h cpp cc cxx mm inl ipp)
        list(APPEND _globs "${_root}/*.${_ext}")
    endforeach()
endforeach()
file(GLOB_RECURSE _files ${_globs})

set(_violations)
foreach(_file IN LISTS _files)
    # The validation layer is the thing that does the wrapping so it is the one place a cast to a
    # wrapper is the point and not the mistake.
    #
    # Anchored on the source directory and not on a path fragment. This read /rhi/validation/ until the
    # tree lost that level, after which it exempted nothing, which is the harmless direction: the gate got
    # stricter and said so. The root above going stale was the other direction and said nothing at all.
    if(_file MATCHES "${AZOTH_RHI_SOURCE_ROOT}/lib/src/validation/")
        continue()
    endif()

    file(STRINGS "${_file}" _lines REGEX "static_cast[ \t]*<")
    foreach(_line IN LISTS _lines)
        if(NOT _line MATCHES "FacadeBuilder::ImplOf")
            continue()
        endif()

        # Either named resolution is what makes the cast answerable so a line carrying one is fine.
        if(_line MATCHES "NativeImplOf|UnwrappedImplOf")
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
            "AzothRHI impl cast violated: a facade's impl pointer was cast without saying what is in front of it.\n"
            "With validation on that pointer is a layer, not the backend's object, and the cast reads the layer's "
            "fields as the backend's.\nUse detail::NativeImplOf to resolve through whatever is there, or "
            "detail::UnwrappedImplOf where the caller already knows there is nothing.\n\n${_report}\n")
endif()
# A root that stops resolving makes this quieter, not redder, which is how a stale one once went on
# printing OK over a third of the tree. The floor is the count at the last deliberate change: raise it as
# files are added, lower it only when files are genuinely removed.
set(_floor 232)
if(_scanned LESS _floor)
    message(FATAL_ERROR
            "AzothRHI impl casts scanned ${_scanned} files, fewer than the ${_floor} this check covers.\n"
            "A scan root has most likely stopped resolving, which leaves the check reporting OK over a smaller tree "
            "than it was written for.\nFix the root, or lower the floor in cmake/azoth_rhi_impl_casts.cmake if files "
            "were genuinely removed.\n")
endif()
message(STATUS "AzothRHI: impl casts OK, ${_scanned} files scanned, none cast a facade's impl without resolving it.")

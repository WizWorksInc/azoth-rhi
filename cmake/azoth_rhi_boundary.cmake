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

# Native graphics headers may appear in the public tree only under include/azoth/rhi/native/.
# src/ is exempt.
#
# Scoped to the library's own public headers, which are what the rule is actually about: a consumer
# includes azoth/rhi/rhi.hpp and must not be handed Vulkan or Metal through it. Tests, examples and
# benchmarks are consumers of that surface and not part of it, and they legitimately reach for a
# native header when the thing they are showing is native interop, which is a good part of why the
# accessors exist. Holding them to the public rule turned every such example into a native/ directory
# for no boundary that a stranger could cross.
#
#     cmake -DAZOTH_RHI_SOURCE_ROOT=<repo> -P cmake/azoth_rhi_boundary.cmake

cmake_minimum_required(VERSION 3.24)

if(NOT DEFINED AZOTH_RHI_SOURCE_ROOT)
    message(FATAL_ERROR "azoth_rhi_boundary: pass -DAZOTH_RHI_SOURCE_ROOT=<repo root>.")
endif()

set(_scan_roots
        "${AZOTH_RHI_SOURCE_ROOT}/lib/include"
)

set(_globs)
foreach(_root IN LISTS _scan_roots)
    foreach(_ext hpp h hh cpp cc cxx mm inl ipp)
        list(APPEND _globs "${_root}/*.${_ext}")
    endforeach()
endforeach()
file(GLOB_RECURSE _files ${_globs})

# Matched against the included path, anchored so our own headers (azoth/rhi/native/vulkan_native.hpp)
# never match. Only genuinely third-party graphics headers do.
set(_forbidden "(^vulkan/|^vk_mem_alloc\\.h|^volk\\.h|^d3d12\\.h|^d3dx12|^dxgi|^D3D12MemAlloc\\.h|^Metal/|^MetalKit/|^QuartzCore/|^Foundation/|^MoltenVK/|^wrl/|^pix3\\.h|^tracy/TracyVulkan\\.hpp)")

# The other half of the same rule and the half an include list cannot catch. A window library's own
# header pulls the graphics headers in behind a name of its own, SDL3/SDL_vulkan.h being the one that
# started this so a file can name VkSurfaceKHR without ever including anything on the list above.
#
# Matched on the types themselves and only on the spellings no other library uses. Our own names are
# deliberately not among them: rhi::VulkanApi and VulkanSurfacePayload are how a caller says which API it
# wants without touching that API, which is what the payloads are for.
# CMake regex has no word boundary so these are spelled as prefixes that cannot begin an identifier of
# ours. Vk followed by a capital is a Vulkan type and nothing else we write.
set(_forbidden_symbols "(Vk[A-Z]|PFN_vk|VK_[A-Z]|vk::|MTL::|CA::|CAMetalLayer|NSWindow|HWND|ID3D12|IDXGI|D3D12_)")

set(_violations)
foreach(_file IN LISTS _files)
    # The quarantine. Anything under a native/ directory may include and name what it needs.
    if(_file MATCHES "/native/")
        continue()
    endif()

    file(STRINGS "${_file}" _includes REGEX "^[ \t]*#[ \t]*(include|import)[ \t]*[<\"]")
    foreach(_line IN LISTS _includes)
        if(_line MATCHES "[<\"]([^>\"]+)[>\"]")
            set(_hdr "${CMAKE_MATCH_1}")
            if(_hdr MATCHES "${_forbidden}")
                file(RELATIVE_PATH _rel "${AZOTH_RHI_SOURCE_ROOT}" "${_file}")
                string(STRIP "${_line}" _trimmed)
                list(APPEND _violations "  ${_rel}\n      ${_trimmed}")
            endif()
        endif()
    endforeach()

    file(STRINGS "${_file}" _named REGEX "${_forbidden_symbols}")
    foreach(_line IN LISTS _named)
        # Prose is not code. A comment explaining why a type is kept out of a header has to be able to
        # name the type and these payloads keep the explanation near them.
        if(_line MATCHES "^[ \t]*(//|\\*|/\\*)")
            continue()
        endif()

        # Shader source is not C++ either. A sample that carries a Slang shader as a string has to spell its bindings [[vk::binding(set, index)]]
        # and [[vk::push_constant]], which is what Slang wants for SPIR-V and warns about the absence of. Those name no type, there is no C++
        # attribute by either name and nothing here uses them, so they come out before the line is judged. A vk::Device beside one still fails.
        string(REGEX REPLACE "\\[\\[vk::[^]]*\\]\\]" "" _code "${_line}")
        string(REGEX REPLACE "//.*$" "" _code "${_code}")
        if(NOT _code MATCHES "${_forbidden_symbols}")
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
            "AzothRHI boundary violated: a file outside native/ includes a graphics header or names a "
            "graphics API type.\nBoth belong under a native/ directory and nowhere else outside "
            "src/.\nUse an opaque payload from azoth/rhi/native/surface_payloads.hpp, a forward "
            "declaration, or move the header under include/azoth/rhi/native/.\n\n${_report}\n")
endif()
# A root that stops resolving makes this quieter, not redder, which is how a stale one once went on
# printing OK over a third of the tree. The floor is the count at the last deliberate change: raise it as
# files are added, lower it only when files are genuinely removed.
set(_floor 82)
if(_scanned LESS _floor)
    message(FATAL_ERROR
            "AzothRHI API boundary scanned ${_scanned} files, fewer than the ${_floor} this check covers.\n"
            "A scan root has most likely stopped resolving, which leaves the check reporting OK over a smaller tree "
            "than it was written for.\nFix the root, or lower the floor in cmake/azoth_rhi_boundary.cmake if files "
            "were genuinely removed.\n")
endif()
message(STATUS "AzothRHI: API boundary OK, ${_scanned} files scanned, no graphics headers or API types outside native/.")

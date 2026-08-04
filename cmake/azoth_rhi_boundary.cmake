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
# Only lib/include is scanned. Tests, examples and benchmarks consume that surface rather than form
# it, and they reach for a native header when interop is the thing being shown. Holding them to the
# rule turned every such example into a native/ directory.
#
# Registered as gate_api_boundary. By hand:
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

set(_forbidden "(^vulkan/|^vk_mem_alloc\\.h|^volk\\.h|^d3d12\\.h|^d3dx12|^dxgi|^D3D12MemAlloc\\.h|^Metal/|^MetalKit/|^QuartzCore/|^Foundation/|^MoltenVK/|^wrl/|^pix3\\.h|^tracy/TracyVulkan\\.hpp)")

# An include list cannot catch a window library that pulls the graphics headers in behind a name of its
# own. SDL3/SDL_vulkan.h started this: a file can name VkSurfaceKHR without including anything above.
# Our own rhi::VulkanApi and VulkanSurfacePayload are deliberately not matched, and CMake regex has no
# word boundary so these are prefixes that cannot begin an identifier of ours.
set(_forbidden_symbols "(Vk[A-Z]|PFN_vk|VK_[A-Z]|vk::|MTL::|CA::|CAMetalLayer|NSWindow|HWND|ID3D12|IDXGI|D3D12_)")

set(_violations)
foreach(_file IN LISTS _files)
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
        # Make comments an exception also as comments may mention forbidden words.
        if(_line MATCHES "^[ \t]*(//|\\*|/\\*)")
            continue()
        endif()

        # Make an exception for Slang's usage of vk::*
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
            "AzothRHI boundary violate: a file outside native/ has a graphics header or names a graphics API type.\n"
            "\n\n${_report}\n"
    )
endif()

set(_floor 81)
if(_scanned LESS _floor)
    message(FATAL_ERROR
            "AzothRHI API boundary scanned ${_scanned} files, fewer than the ${_floor} this check covers.\n"
            "Fix the root or lower the floor in cmake/azoth_rhi_boundary.cmake if files were genuinely removed.\n")
endif()

message(STATUS "AzothRHI: API boundary OK, ${_scanned} files scanned, no graphics headers or API types outside native/.")

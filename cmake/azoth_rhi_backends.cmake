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

# Each selectable backend is ON or OFF and that decides one thing: whether its implementation
# compiles into the library. A backend that is OFF is absent from what AvailableBackends reports so
# nothing can register it and nothing looks for it. Its API tag and its typed entry point are
# declared either way, which is what leaves room for a backend of your own to fill them.
#
# Replacing one of ours is turning it OFF and registering yours under the same tag. There is no
# third state for that and no macro a caller has to test.
#
# Only the hardware backends are selectable. The Null backend is not configurable and is not modelled
# here at all: it always builds so there is nothing to decide about it.

function(azoth_rhi_compute_platform_support)
    set(AZOTH_RHI_VULKAN_SUPPORTED TRUE PARENT_SCOPE)

    if(WIN32)
        set(AZOTH_RHI_D3D12_SUPPORTED TRUE PARENT_SCOPE)
    else()
        set(AZOTH_RHI_D3D12_SUPPORTED FALSE PARENT_SCOPE)
    endif()

    if(APPLE)
        set(AZOTH_RHI_METAL_SUPPORTED TRUE PARENT_SCOPE)

        # Metal 4 is a backend of its own, not a mode of the one above, so it gets its own switch. Whether
        # the pinned metal-cpp release actually carries the MTL4 headers is checked in lib/CMakeLists.txt,
        # where the dependency has been fetched and there is a directory to look in.
        set(AZOTH_RHI_METAL4_SUPPORTED TRUE PARENT_SCOPE)
    else()
        set(AZOTH_RHI_METAL_SUPPORTED FALSE PARENT_SCOPE)
        set(AZOTH_RHI_METAL4_SUPPORTED FALSE PARENT_SCOPE)
    endif()
endfunction()

# Sets AZOTH_RHI_BACKEND_<NAME>, which decides whether that backend's sources compile.
macro(azoth_rhi_declare_backend name display supported default)
    if(${supported})
        option(AZOTH_RHI_BACKEND_${name} "Compile the ${display} backend into the library" ${default})
    else()
        if(AZOTH_RHI_BACKEND_${name})
            message(FATAL_ERROR
                    "AzothRHI: the ${display} backend cannot be built on this platform "
                    "(requested AZOTH_RHI_BACKEND_${name}=${AZOTH_RHI_BACKEND_${name}}).")
        endif()
        set(AZOTH_RHI_BACKEND_${name} OFF)
    endif()
endmacro()

# Vulkan only warns, since the headers are fetched and a missing loader costs nothing until runtime.
function(azoth_rhi_detect_platform_apis)
    if(NOT AZOTH_RHI_DETECT_PLATFORM_APIS)
        message(STATUS "AzothRHI: platform SDK detection off, enabled backends are taken on trust")
        return()
    endif()

    if(AZOTH_RHI_BACKEND_VULKAN)
        find_package(Vulkan QUIET)
        if(Vulkan_FOUND)
            message(STATUS "AzothRHI: Vulkan loader found (${Vulkan_LIBRARY})")
        else()
            message(WARNING
                    "AzothRHI: no Vulkan loader found on this machine. The backend still builds, since "
                    "the headers are fetched, but creating a Vulkan device will fail at runtime until a "
                    "driver or the LunarG SDK is installed. Set AZOTH_RHI_DETECT_PLATFORM_APIS=OFF to "
                    "silence this, or AZOTH_RHI_BACKEND_VULKAN=OFF to drop the backend.")
        endif()
    endif()

    if(AZOTH_RHI_BACKEND_D3D12)
        include(CheckIncludeFileCXX)
        check_include_file_cxx(d3d12.h AZOTH_RHI_HAVE_D3D12_H)
        if(NOT AZOTH_RHI_HAVE_D3D12_H)
            message(FATAL_ERROR
                    "AzothRHI: the Direct3D 12 backend is enabled but d3d12.h was not found. Install the "
                    "Windows SDK, or set AZOTH_RHI_BACKEND_D3D12=OFF.")
        endif()
        message(STATUS "AzothRHI: Direct3D 12 headers found")
    endif()

    if(AZOTH_RHI_BACKEND_METAL OR AZOTH_RHI_BACKEND_METAL4)
        find_library(AZOTH_RHI_METAL_FRAMEWORK Metal)
        find_library(AZOTH_RHI_QUARTZCORE_FRAMEWORK QuartzCore)
        if(NOT AZOTH_RHI_METAL_FRAMEWORK OR NOT AZOTH_RHI_QUARTZCORE_FRAMEWORK)
            message(FATAL_ERROR
                    "AzothRHI: a Metal backend is enabled but the Metal or QuartzCore framework was not "
                    "found. Install the Xcode command line tools, or set AZOTH_RHI_BACKEND_METAL=OFF and "
                    "AZOTH_RHI_BACKEND_METAL4=OFF.")
        endif()
        message(STATUS "AzothRHI: Metal frameworks found")
    endif()
endfunction()

function(azoth_rhi_report_backends)
    message(STATUS "AzothRHI backends:")
    foreach(name IN ITEMS VULKAN D3D12 METAL METAL4)
        if(NOT AZOTH_RHI_${name}_SUPPORTED)
            message(STATUS "  ${name}: unavailable")
        elseif(AZOTH_RHI_BACKEND_${name})
            message(STATUS "  ${name}: on")
        else()
            message(STATUS "  ${name}: off, register your own under its API tag to fill it")
        endif()
    endforeach()
endfunction()

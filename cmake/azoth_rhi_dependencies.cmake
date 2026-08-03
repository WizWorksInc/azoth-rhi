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

include(FetchContent)

set(AZOTH_RHI_VK_DYNAMIC_TAG "v1.4.357" CACHE STRING "vk-dynamic version (Vulkan-Hpp plus dispatcher storage)")
set(AZOTH_RHI_VMA_TAG "v3.4.0" CACHE STRING "VulkanMemoryAllocator version")
set(AZOTH_RHI_D3D12MA_TAG "v3.2.0" CACHE STRING "D3D12MemoryAllocator version")
# A release tag and not a branch or a commit. metal-cpp tags by the SDK a release targets and not by
# a version of its own, so this names the SDK the headers expect, and picking one above the installed SDK
# would compile against declarations the platform does not have.
set(AZOTH_RHI_METAL_CPP_TAG "release/metal-cpp_macOS26.4_iOS26.4" CACHE STRING "metal-cpp release, which is tagged by the SDK it targets")

mark_as_advanced(
        AZOTH_RHI_VK_DYNAMIC_TAG
        AZOTH_RHI_VMA_TAG
        AZOTH_RHI_D3D12MA_TAG
        AZOTH_RHI_METAL_CPP_TAG
)

# The graphics SDK headers install here, not straight into the include prefix.
#
# This is what keeps the quarantine intact after installation. Our own headers install to
# <prefix>/include, which azoth::rhi puts on every consumer's include path so anything else landing
# beside them would make <vulkan/vulkan.hpp> reachable from a plain link. Tucking the SDKs one level
# down means only azoth::rhi-native-vulkan, which adds this directory, can see them.
set(AZOTH_RHI_NATIVE_INCLUDE_SUBDIR "azoth-rhi-native")

function(azoth_rhi_mark_system target)
    if(NOT TARGET ${target})
        return()
    endif()
    get_target_property(_aliased ${target} ALIASED_TARGET)
    if(_aliased)
        set(target ${_aliased})
    endif()
    get_target_property(_dirs ${target} INTERFACE_INCLUDE_DIRECTORIES)
    if(_dirs)
        set_target_properties(${target} PROPERTIES INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${_dirs}")
    endif()
endfunction()

# Takes the first of several candidate spellings that exists, isolates its includes, and appends it to
# out_var under the name install(TARGETS) will accept.
#
# The spellings are what lib/CMakeLists.txt already accepts when it links these. Testing one of them here
# and another there would let an upstream that renamed its target link in tree while dropping out of the
# export set, which fails nowhere until somebody imports the installed package. An alias is resolved
# because install(TARGETS) refuses one by name.
function(azoth_rhi_bundle_target out_var include_dir)
    foreach(_candidate IN LISTS ARGN)
        if(NOT TARGET ${_candidate})
            continue()
        endif()

        azoth_rhi_isolate_includes(${_candidate} "${include_dir}")

        get_target_property(_aliased ${_candidate} ALIASED_TARGET)
        if(_aliased)
            set(_candidate ${_aliased})
        endif()

        set(${out_var} ${${out_var}} ${_candidate} PARENT_SCOPE)
        return()
    endforeach()
endfunction()

function(azoth_rhi_isolate_includes target build_dir)
    if(NOT TARGET ${target})
        return()
    endif()
    get_target_property(_aliased ${target} ALIASED_TARGET)
    if(_aliased)
        set(target ${_aliased})
    endif()
    set(_dirs
            "$<BUILD_INTERFACE:${build_dir}>"
            "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/${AZOTH_RHI_NATIVE_INCLUDE_SUBDIR}>"
    )
    set_target_properties(${target} PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${_dirs}"
            INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${_dirs}"
    )
endfunction()

# A macro and not a function on purpose. FetchContent sets <name>_SOURCE_DIR in the calling
# scope and the install rules need those paths so a function would hide them.
macro(azoth_rhi_fetch_dependencies)
    set(AZOTH_RHI_BUNDLED_TARGETS "")
    set(AZOTH_RHI_BUNDLED_INCLUDE_DIRS "")

    if(AZOTH_RHI_BACKEND_VULKAN)
        FetchContent_Declare(vk-dynamic
                GIT_REPOSITORY https://github.com/Rinzii/vk-dynamic.git
                GIT_TAG ${AZOTH_RHI_VK_DYNAMIC_TAG}
                GIT_SHALLOW TRUE
        )
        FetchContent_MakeAvailable(vk-dynamic)
        list(APPEND AZOTH_RHI_BUNDLED_TARGETS vk-dynamic vk-dynamic-hpp-dispatch)
        list(APPEND AZOTH_RHI_BUNDLED_INCLUDE_DIRS "${vk-dynamic_SOURCE_DIR}/include/")
        azoth_rhi_isolate_includes(vk-dynamic "${vk-dynamic_SOURCE_DIR}/include")
        azoth_rhi_isolate_includes(vk-dynamic-hpp-dispatch "${vk-dynamic_SOURCE_DIR}/include")

    endif()

    if(AZOTH_RHI_BACKEND_VULKAN)
        FetchContent_Declare(VulkanMemoryAllocator
                GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
                GIT_TAG ${AZOTH_RHI_VMA_TAG}
                GIT_SHALLOW TRUE
        )
        FetchContent_MakeAvailable(VulkanMemoryAllocator)
        azoth_rhi_bundle_target(AZOTH_RHI_BUNDLED_TARGETS "${vulkanmemoryallocator_SOURCE_DIR}/include"
                VulkanMemoryAllocator GPUOpen::VulkanMemoryAllocator)
        list(APPEND AZOTH_RHI_BUNDLED_INCLUDE_DIRS "${vulkanmemoryallocator_SOURCE_DIR}/include/")
    endif()

    if(AZOTH_RHI_BACKEND_D3D12)
        FetchContent_Declare(D3D12MemoryAllocator
                GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/D3D12MemoryAllocator.git
                GIT_TAG ${AZOTH_RHI_D3D12MA_TAG}
                GIT_SHALLOW TRUE
        )
        FetchContent_MakeAvailable(D3D12MemoryAllocator)
        azoth_rhi_bundle_target(AZOTH_RHI_BUNDLED_TARGETS "${d3d12memoryallocator_SOURCE_DIR}/include"
                D3D12MemoryAllocator GPUOpen::D3D12MemoryAllocator)
        list(APPEND AZOTH_RHI_BUNDLED_INCLUDE_DIRS "${d3d12memoryallocator_SOURCE_DIR}/include/")
    endif()

    if(AZOTH_RHI_BACKEND_METAL OR AZOTH_RHI_BACKEND_METAL4)
        FetchContent_Declare(metal_cpp
                GIT_REPOSITORY https://github.com/apple/metal-cpp.git
                GIT_TAG ${AZOTH_RHI_METAL_CPP_TAG}
        )
        FetchContent_MakeAvailable(metal_cpp)

        if(NOT TARGET metal_cpp)
            add_library(metal_cpp INTERFACE)
            target_link_libraries(metal_cpp INTERFACE
                    "-framework Foundation"
                    "-framework Metal"
                    "-framework QuartzCore"
            )
        endif()
        list(APPEND AZOTH_RHI_BUNDLED_TARGETS metal_cpp)
        list(APPEND AZOTH_RHI_BUNDLED_INCLUDE_DIRS "${metal_cpp_SOURCE_DIR}/")
        azoth_rhi_isolate_includes(metal_cpp "${metal_cpp_SOURCE_DIR}")
    endif()
endmacro()

function(azoth_rhi_install_dependencies)
    if(AZOTH_RHI_BUNDLED_TARGETS)
        install(TARGETS ${AZOTH_RHI_BUNDLED_TARGETS}
                EXPORT AzothRHITargets
                ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
                LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
                RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        )
    endif()

    foreach(dir IN LISTS AZOTH_RHI_BUNDLED_INCLUDE_DIRS)
        if(EXISTS "${dir}")
            install(DIRECTORY "${dir}"
                    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/${AZOTH_RHI_NATIVE_INCLUDE_SUBDIR}"
                    FILES_MATCHING
                    PATTERN "*.h"
                    PATTERN "*.hpp"
                    PATTERN "*.inl"
                    PATTERN ".git" EXCLUDE
                    PATTERN ".github" EXCLUDE
                    PATTERN "CMakeFiles" EXCLUDE
            )
        endif()
    endforeach()
endfunction()

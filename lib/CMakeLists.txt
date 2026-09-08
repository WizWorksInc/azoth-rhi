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

add_library(AzothRHI)
add_library(azoth::rhi ALIAS AzothRHI)

target_sources(AzothRHI PRIVATE
        src/device/selection.cpp
        src/support/clip_space.cpp
        src/device/allocation_tracker.cpp
        src/support/driver_version.cpp
        src/device/facades.cpp
        src/host/presentation_backend.cpp
        src/host/process_state.cpp
        src/module/backend_module.cpp
        src/validation/decorator.cpp
)

if (AZOTH_RHI_BACKEND_VULKAN)
    target_sources(AzothRHI PRIVATE
            src/backends/vulkan/adoption.cpp
            src/backends/vulkan/backend.cpp
            src/backends/vulkan/commands.cpp
            src/backends/vulkan/conversions.cpp
            src/backends/vulkan/descriptors.cpp
            src/backends/vulkan/destroy.cpp
            src/backends/vulkan/external.cpp
            src/backends/vulkan/instance.cpp
            src/backends/vulkan/queue.cpp
            src/backends/vulkan/swapchain.cpp
            src/backends/vulkan/tables.cpp
            src/backends/vulkan/swapchain_bundle.cpp
            src/backends/vulkan/vma_impl.cpp
            src/host/presentation_vulkan.cpp
    )
endif ()

if (AZOTH_RHI_BACKEND_D3D12)
    target_sources(AzothRHI PRIVATE
            src/backends/d3d12/backend.cpp
            src/backends/d3d12/barriers.cpp
            src/backends/d3d12/blit.cpp
            src/backends/d3d12/buffer.cpp
            src/backends/d3d12/common.cpp
            src/backends/d3d12/copy.cpp
            src/backends/d3d12/descriptors.cpp
            src/backends/d3d12/device.cpp
            src/backends/d3d12/external.cpp
            src/backends/d3d12/adoption.cpp
            src/backends/d3d12/layout.cpp
            src/backends/d3d12/memory.cpp
            src/backends/d3d12/pipeline.cpp
            src/backends/d3d12/query.cpp
            src/backends/d3d12/queue.cpp
            src/backends/d3d12/render.cpp
            src/backends/d3d12/sampler_heap.cpp
            src/backends/d3d12/swapchain.cpp
            src/backends/d3d12/sync.cpp
            src/backends/d3d12/tables.cpp
            src/backends/d3d12/texture.cpp
    )
endif ()

# Shared by both Metal backends so a format added to the RHI extends one switch and not two that
# drift.
if (AZOTH_RHI_BACKEND_METAL OR AZOTH_RHI_BACKEND_METAL4)
    target_sources(AzothRHI PRIVATE
            src/backends/metal_common/conversions.cpp
            src/backends/metal_common/metal_cpp_impl.cpp
    )
endif ()

if (AZOTH_RHI_BACKEND_METAL)
    target_sources(AzothRHI PRIVATE
            src/backends/metal/backend.cpp
            src/backends/metal/buffer_texture.cpp
            src/backends/metal/commands.cpp
            src/backends/metal/common.cpp
            src/backends/metal/descriptors.cpp
            src/backends/metal/device.cpp
            src/backends/metal/external.cpp
            src/backends/metal/formats.cpp
            src/backends/metal/heap.cpp
            src/backends/metal/instance.cpp
            src/backends/metal/adoption.cpp
            src/backends/metal/pipeline.cpp
            src/backends/metal/query.cpp
            src/backends/metal/queue.cpp
            src/backends/metal/render.cpp
            src/backends/metal/swapchain.cpp
            src/backends/metal/sync.cpp
            src/backends/metal/tables.cpp
    )
endif ()

if (AZOTH_RHI_BACKEND_METAL4)
    target_sources(AzothRHI PRIVATE
            src/backends/metal4/backend.cpp
            src/backends/metal4/buffer_texture.cpp
            src/backends/metal4/commands.cpp
            src/backends/metal4/common.cpp
            src/backends/metal4/descriptors.cpp
            src/backends/metal4/device.cpp
            src/backends/metal4/external.cpp
            src/backends/metal4/formats.cpp
            src/backends/metal4/heap.cpp
            src/backends/metal4/instance.cpp
            src/backends/metal4/adoption.cpp
            src/backends/metal4/pipeline.cpp
            src/backends/metal4/query.cpp
            src/backends/metal4/queue.cpp
            src/backends/metal4/render.cpp
            src/backends/metal4/swapchain.cpp
            src/backends/metal4/sync.cpp
            src/backends/metal4/tables.cpp
    )
endif ()

# Always built and not configurable so this is unconditional and there is no AZOTH_RHI_BACKEND_NULL to consult.
target_sources(AzothRHI PRIVATE src/backends/null/backend.cpp)

target_compile_features(AzothRHI PUBLIC cxx_std_23)

target_include_directories(AzothRHI
        PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        # Where configure wrote core/version_numbers.hpp. Installing puts it beside the rest so
        # the install interface needs one directory.
        $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# PRIVATE on purpose. AvailableBackends answers at runtime which backends compiled in. A public
# macro would change a header's shape with the build.
foreach (backend VULKAN D3D12 METAL METAL4)
    if (AZOTH_RHI_BACKEND_${backend})
        target_compile_definitions(AzothRHI PRIVATE AZOTH_RHI_BACKEND_${backend})
    endif ()
endforeach ()

target_compile_definitions(AzothRHI PRIVATE AZOTH_RHI_BACKEND_DEFAULT="${AZOTH_RHI_DEFAULT_BACKEND}")

# What AZO_RHI_API reads. Private because only this library should see dllexport where a consumer
# sees dllimport.
target_compile_definitions(AzothRHI PRIVATE AZOTH_RHI_BUILDING)

# Public. Asked of the target instead of BUILD_SHARED_LIBS because the type is what actually got
# resolved.
get_target_property(_azoth_rhi_library_type AzothRHI TYPE)
if (_azoth_rhi_library_type STREQUAL "SHARED_LIBRARY")
    target_compile_definitions(AzothRHI PUBLIC AZOTH_RHI_SHARED)
endif ()

if (AZOTH_RHI_ENABLE_PROFILING)
    target_compile_definitions(AzothRHI PUBLIC AZOTH_RHI_ENABLE_PROFILING)
endif ()

if (AZOTH_RHI_NO_EXCEPTIONS)
    # PUBLIC because it changes a header the consumer compiles: RefuseAllocation built two ways is
    # the ABI disagreement this option exists to avoid.
    target_compile_definitions(AzothRHI PUBLIC AZOTH_RHI_NO_EXCEPTIONS)

    # PRIVATE, unlike the macro above. A throw left in these sources fails the build. Public it
    # would leave this configuration with no test suite at all.
    target_compile_options(AzothRHI PRIVATE
            $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-fno-exceptions>
            $<$<CXX_COMPILER_ID:MSVC>:/EHs-c->)
endif ()

if (AZOTH_RHI_BACKEND_VULKAN)
    # PRIVATE keeps Vulkan off a consumer's include path. Link requirements still forward.
    target_link_libraries(AzothRHI PRIVATE vk::dynamic_hpp_dispatch)
    target_compile_definitions(AzothRHI PRIVATE VULKAN_HPP_NO_TO_STRING=1)

    # Unconditional so the backend reads the same in both builds. The empty assertion lets the
    # .result each call site tests decide, not a debug abort.
    target_compile_definitions(AzothRHI PRIVATE VULKAN_HPP_NO_EXCEPTIONS VULKAN_HPP_ASSERT_ON_RESULT=)

    # A process-global dispatcher lets two devices with different layer chains share entry points.
    # Dropping the default argument makes that a compile error.
    target_compile_definitions(AzothRHI PRIVATE VULKAN_HPP_NO_DEFAULT_DISPATCHER)

    # DispatchLoaderDynamic gains a member per platform entry point. A consumer that disagrees
    # about this reads the table at different offsets.
    if (WIN32)
        target_compile_definitions(AzothRHI PRIVATE VK_USE_PLATFORM_WIN32_KHR NOMINMAX WIN32_LEAN_AND_MEAN)
    endif ()
endif ()

if (AZOTH_RHI_BACKEND_VULKAN)
    if (TARGET GPUOpen::VulkanMemoryAllocator)
        target_link_libraries(AzothRHI PRIVATE GPUOpen::VulkanMemoryAllocator)
    elseif (TARGET VulkanMemoryAllocator)
        target_link_libraries(AzothRHI PRIVATE VulkanMemoryAllocator)
    else ()
        message(FATAL_ERROR "AzothRHI: the bundled Vulkan backend is on but no VulkanMemoryAllocator target was found.")
    endif ()
endif ()

if (AZOTH_RHI_BACKEND_D3D12)
    target_link_libraries(AzothRHI PRIVATE d3d12 dxgi dxguid)

    if (TARGET D3D12MemoryAllocator)
        target_link_libraries(AzothRHI PRIVATE D3D12MemoryAllocator)
    elseif (TARGET GPUOpen::D3D12MemoryAllocator)
        target_link_libraries(AzothRHI PRIVATE GPUOpen::D3D12MemoryAllocator)
    else ()
        message(FATAL_ERROR "AzothRHI: the Direct3D 12 backend is on but no D3D12MemoryAllocator target was found.")
    endif ()
endif ()

if (AZOTH_RHI_BACKEND_METAL OR AZOTH_RHI_BACKEND_METAL4)
    target_link_libraries(AzothRHI PRIVATE metal_cpp)
endif ()

# Probing for the header and not a version string, since the tag names the SDK. Refusing beats
# quietly leaving AvailableBackends short of an entry.
if (AZOTH_RHI_BACKEND_METAL4 AND NOT EXISTS "${metal_cpp_SOURCE_DIR}/Metal/MTL4Counters.hpp")
    message(FATAL_ERROR
            "AzothRHI: the Metal 4 backend is enabled but this metal-cpp release "
            "(${AZOTH_RHI_METAL_CPP_TAG}) has no MTL4 headers. Point AZOTH_RHI_METAL_CPP_TAG at a release "
            "for macOS 26 or later, or set AZOTH_RHI_BACKEND_METAL4=OFF.")
endif ()

if (AZOTH_RHI_PIX)
    target_link_libraries(AzothRHI PUBLIC ${AZOTH_RHI_PIX_TARGET})
    target_compile_definitions(AzothRHI PUBLIC AZOTH_RHI_ENABLE_PIX)
    # Outside a debug build, pix3.h compiles its events away without this. Set here since a target
    # that only links the runtime need not carry it.
    target_compile_definitions(AzothRHI PRIVATE USE_PIX)
endif ()

# No definition of our own. Tracy's TRACY_ENABLE arrives on the client target's interface and
# reaches everything downstream.
if (AZOTH_RHI_TRACY_ENABLED)
    target_sources(AzothRHI PRIVATE src/host/tracy_profiler.cpp)
    target_link_libraries(AzothRHI PUBLIC ${AZOTH_RHI_TRACY_TARGET})
endif ()

# Hidden by default so a shared build exports what AZO_RHI_API names and nothing else. It also
# keeps a missing one from being a Windows-only link failure.
set_target_properties(AzothRHI PROPERTIES
        VERSION ${PROJECT_VERSION}
        SOVERSION ${PROJECT_VERSION_MAJOR}
        EXPORT_NAME rhi
        POSITION_INDEPENDENT_CODE ON
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON
)

# What a backend written outside this repository links. Everything behind it is header only and
# already installed. This target names the supported surface.
add_library(AzothRHI-backend-sdk INTERFACE)
add_library(azoth::rhi-backend-sdk ALIAS AzothRHI-backend-sdk)
target_link_libraries(AzothRHI-backend-sdk INTERFACE AzothRHI)
set_target_properties(AzothRHI-backend-sdk PROPERTIES EXPORT_NAME rhi-backend-sdk)

# What a loadable backend links. Interface only, since AZO_RHI_DEFINE_MODULE stamps the ABI in the
# module's own translation unit and not the host's.
add_library(AzothRHI-module-sdk INTERFACE)
add_library(azoth::rhi-module-sdk ALIAS AzothRHI-module-sdk)
target_link_libraries(AzothRHI-module-sdk INTERFACE AzothRHI-backend-sdk)
set_target_properties(AzothRHI-module-sdk PROPERTIES EXPORT_NAME rhi-module-sdk)

if (AZOTH_RHI_BACKEND_VULKAN)
    add_library(AzothRHI-native-vulkan INTERFACE)
    add_library(azoth::rhi-native-vulkan ALIAS AzothRHI-native-vulkan)
    target_link_libraries(AzothRHI-native-vulkan INTERFACE AzothRHI vk::dynamic_hpp_dispatch)
    target_compile_definitions(AzothRHI-native-vulkan INTERFACE VULKAN_HPP_NO_TO_STRING=1)

    # Matched to what the library was compiled with, since the Win32 entry points are members of
    # the table VulkanNativeDevice::dispatch points at.
    if (WIN32)
        target_compile_definitions(AzothRHI-native-vulkan INTERFACE VK_USE_PLATFORM_WIN32_KHR)
    endif ()
    set_target_properties(AzothRHI-native-vulkan PROPERTIES EXPORT_NAME rhi-native-vulkan)
endif ()

# The Metal counterpart. metal_native.hpp only forward declares the MTL and MTL4 types. A consumer
# reading MetalNativeDevice::device needs metal-cpp itself.
if (AZOTH_RHI_BACKEND_METAL OR AZOTH_RHI_BACKEND_METAL4)
    add_library(AzothRHI-native-metal INTERFACE)
    add_library(azoth::rhi-native-metal ALIAS AzothRHI-native-metal)
    target_link_libraries(AzothRHI-native-metal INTERFACE AzothRHI metal_cpp)
    set_target_properties(AzothRHI-native-metal PROPERTIES EXPORT_NAME rhi-native-metal)
endif ()

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

if(AZOTH_RHI_BACKEND_VULKAN)
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
endif()

if(AZOTH_RHI_BACKEND_D3D12)
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
endif()

# How an RHI enum becomes a Metal enum, which is one answer and not one per generation. Compiled when
# either Metal backend is, and depending on neither: a format added to the RHI is a single switch to
# extend, not two that drift.
if(AZOTH_RHI_BACKEND_METAL OR AZOTH_RHI_BACKEND_METAL4)
    target_sources(AzothRHI PRIVATE
            src/backends/metal_common/conversions.cpp
            src/backends/metal_common/metal_cpp_impl.cpp
    )
endif()

if(AZOTH_RHI_BACKEND_METAL)
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
endif()

if(AZOTH_RHI_BACKEND_METAL4)
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
endif()

# Always built and not configurable so this is unconditional and there is no AZOTH_RHI_BACKEND_NULL to consult.
target_sources(AzothRHI PRIVATE src/backends/null/backend.cpp)

target_compile_features(AzothRHI PUBLIC cxx_std_23)

target_include_directories(AzothRHI
        PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        # Where configure wrote core/version_numbers.hpp. Installing puts it beside the rest so only
        # a build out of the tree needs the second directory.
        $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# PRIVATE on purpose. Which backends compiled in is answered at runtime by AvailableBackends so a
# consumer never tests one of these and a public header that did would be a header that changes
# shape with the build.
foreach(backend VULKAN D3D12 METAL METAL4)
    if(AZOTH_RHI_BACKEND_${backend})
        target_compile_definitions(AzothRHI PRIVATE AZOTH_RHI_BACKEND_${backend})
    endif()
endforeach()

target_compile_definitions(AzothRHI PRIVATE AZOTH_RHI_BACKEND_DEFAULT="${AZOTH_RHI_DEFAULT_BACKEND}")

# What AZO_RHI_API reads. BUILDING is private and is what tells dllexport from dllimport, so only this
# library ever sees it. SHARED is public and rides the exported target, which is what stops an installed
# package from disagreeing with the binary beside it about which one a consumer should be spelling.
#
# Asked of the target, not of BUILD_SHARED_LIBS, since the type is what actually got resolved.
target_compile_definitions(AzothRHI PRIVATE AZOTH_RHI_BUILDING)

get_target_property(_azoth_rhi_library_type AzothRHI TYPE)
if(_azoth_rhi_library_type STREQUAL "SHARED_LIBRARY")
    target_compile_definitions(AzothRHI PUBLIC AZOTH_RHI_SHARED)
endif()

if(AZOTH_RHI_ENABLE_PROFILING)
    target_compile_definitions(AzothRHI PUBLIC AZOTH_RHI_ENABLE_PROFILING)
endif()

if(AZOTH_RHI_NO_EXCEPTIONS)
    # PUBLIC because it changes a header the consumer compiles. A consumer that did not see it would build a different HostAllocatorAdapter::RefuseAllocation
    # than the library was built with, which is the ABI disagreement the option exists to avoid.
    target_compile_definitions(AzothRHI PUBLIC AZOTH_RHI_NO_EXCEPTIONS)

    # PRIVATE, unlike the macro above. What the option promises is a library that neither throws nor needs exceptions, and compiling these sources this way
    # is what proves it: a throw or a catch still carried here fails the build. Carrying the flag out to a consumer proves nothing further and takes
    # exceptions away from code that never asked for that. The suite was the standing case, since PUBLIC reached every test translation unit and left this
    # configuration with no suite at all and not one case to account for.
    target_compile_options(AzothRHI PRIVATE
            $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-fno-exceptions>
            $<$<CXX_COMPILER_ID:MSVC>:/EHs-c->)
endif()

if(AZOTH_RHI_BACKEND_VULKAN)
    # PRIVATE keeps Vulkan off a consumer's include path. Link requirements still forward.
    target_link_libraries(AzothRHI PRIVATE vk::dynamic_hpp_dispatch)
    target_compile_definitions(AzothRHI PRIVATE VULKAN_HPP_NO_TO_STRING=1)

    # Vulkan-Hpp reports a failed result by throwing unless told otherwise. Told otherwise, the same entry points hand back a ResultValue and every call
    # site checks it, which is the shape the backend has to be in before a host without exceptions can link this at all.
    #
    # Unconditional, not tied to AZOTH_RHI_NO_EXCEPTIONS, so the backend reads the same in both builds. Every backend entry is a noexcept function
    # pointer in a block, so a Vulkan-Hpp throw crossing one was already a terminate and not an error a caller could read, and a call site whose result
    # check only exists under the option is one that nothing compiles until somebody builds with the option on.
    # With exceptions off, Vulkan-Hpp reports a failed call by asserting and not by returning, so every result check in this backend was dead in a debug
    # build: a driver saying eErrorInitializationFailed aborted the process instead of becoming an Error the caller reads. Silencing the assertion is what
    # lets the .result each call site already tests be the thing that decides.
    target_compile_definitions(AzothRHI PRIVATE VULKAN_HPP_NO_EXCEPTIONS VULKAN_HPP_ASSERT_ON_RESULT=)

    # Vulkan-Hpp's default dispatcher is one process-global table and its init overwrites whatever
    # the last device put there. Two devices whose layer chains differ then share one set of entry
    # points and the older one calls into the wrong place. The backend carries a dispatcher per
    # instance and per device instead and this drops the default argument that would let a call
    # reach for the global one so leaving it off is a compile error and not a crash at teardown.
    #
    # PRIVATE on purpose: this changes the signature of every Vulkan-Hpp entry point
    target_compile_definitions(AzothRHI PRIVATE VULKAN_HPP_NO_DEFAULT_DISPATCHER)

    # Windows has no file descriptor, so the Win32 handle types are the whole of external sharing there, and their structures and entry points sit
    # behind this macro. It also settles a layout disagreement that predates external sharing: DispatchLoaderDynamic carries one member per platform
    # entry point, so a consumer that defined this for its own surface creation and a library built without it read the same table at different offsets.
    # The two Windows macros come with it because the macro pulls windows.h into every Vulkan translation unit, and min and max as macros collide with
    # the std and vk names used throughout the backend.
    if(WIN32)
        target_compile_definitions(AzothRHI PRIVATE VK_USE_PLATFORM_WIN32_KHR NOMINMAX WIN32_LEAN_AND_MEAN)
    endif()
endif()

if(AZOTH_RHI_BACKEND_VULKAN)
    if(TARGET GPUOpen::VulkanMemoryAllocator)
        target_link_libraries(AzothRHI PRIVATE GPUOpen::VulkanMemoryAllocator)
    elseif(TARGET VulkanMemoryAllocator)
        target_link_libraries(AzothRHI PRIVATE VulkanMemoryAllocator)
    else()
        message(FATAL_ERROR "AzothRHI: the bundled Vulkan backend is on but no VulkanMemoryAllocator target was found.")
    endif()
endif()

if(AZOTH_RHI_BACKEND_D3D12)
    target_link_libraries(AzothRHI PRIVATE d3d12 dxgi dxguid)

    if(TARGET D3D12MemoryAllocator)
        target_link_libraries(AzothRHI PRIVATE D3D12MemoryAllocator)
    elseif(TARGET GPUOpen::D3D12MemoryAllocator)
        target_link_libraries(AzothRHI PRIVATE GPUOpen::D3D12MemoryAllocator)
    else()
        message(FATAL_ERROR "AzothRHI: the Direct3D 12 backend is on but no D3D12MemoryAllocator target was found.")
    endif()
endif()

if(AZOTH_RHI_BACKEND_METAL OR AZOTH_RHI_BACKEND_METAL4)
    target_link_libraries(AzothRHI PRIVATE metal_cpp)
endif()

# Whether this metal-cpp release carries Metal 4 at all, which is a question about the pinned dependency
# and not about the machine. Probing for the header and not for a version string, since the tag names
# the SDK it targets and a tag comparison would need updating every release.
#
# A release without it cannot build this backend, and saying so at configure time is the whole of the
# handling. Turning the backend off quietly instead would hand back a build whose AvailableBackends is
# missing an entry the configuration asked for, which nothing downstream can tell from a build that never
# wanted it. What the adapter says is a separate question, asked at runtime through supportsFamily.
if(AZOTH_RHI_BACKEND_METAL4 AND NOT EXISTS "${metal_cpp_SOURCE_DIR}/Metal/MTL4Counters.hpp")
    message(FATAL_ERROR
            "AzothRHI: the Metal 4 backend is enabled but this metal-cpp release "
            "(${AZOTH_RHI_METAL_CPP_TAG}) has no MTL4 headers. Point AZOTH_RHI_METAL_CPP_TAG at a release "
            "for macOS 26 or later, or set AZOTH_RHI_BACKEND_METAL4=OFF.")
endif()

if(AZOTH_RHI_PIX)
    target_link_libraries(AzothRHI PUBLIC ${AZOTH_RHI_PIX_TARGET})
    target_compile_definitions(AzothRHI PUBLIC AZOTH_RHI_ENABLE_PIX)
    # Outside a debug build, pix3.h compiles its events away without this. Set here, not
    # assumed of whoever supplies the runtime, since a target that only links it need not carry it.
    target_compile_definitions(AzothRHI PRIVATE USE_PIX)
endif()

# No definition of our own to say the sink is built. Tracy's TRACY_ENABLE arrives on the client
# target's interface and reaches everything downstream so the header and the sources ask Tracy.
if(AZOTH_RHI_TRACY_ENABLED)
    target_sources(AzothRHI PRIVATE src/host/tracy_profiler.cpp)
    target_link_libraries(AzothRHI PUBLIC ${AZOTH_RHI_TRACY_TARGET})
endif()

# Hidden by default, so what leaves a shared build is what AZO_RHI_API names and nothing else. Without
# it a dylib or an .so exports every backend internal it happens to have compiled, which is the same
# boundary the gate scripts check for in the headers, given away at the binary instead.
#
# It also makes the two platforms agree. An unmarked symbol is absent from a Windows DLL whatever this
# says, so hidden elsewhere is what keeps a missing AZO_RHI_API from being a Windows-only link failure.
set_target_properties(AzothRHI PROPERTIES
        VERSION ${PROJECT_VERSION}
        SOVERSION ${PROJECT_VERSION_MAJOR}
        EXPORT_NAME rhi
        POSITION_INDEPENDENT_CODE ON
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON
)

# What somebody writing a backend outside this repository links and the name that says so.
#
# The headers behind it are the ones a backend cannot be written without and should not have to write:
# generational handle storage, the object pool, the containers that route through the host allocator
# seam, format and subresource arithmetic and the validation registry. Without them an out of tree
# backend reimplements slot maps before it writes a line of graphics code, which is why Vulkan declares
# fourteen SlotMap instantiations here and D3D12 another fourteen.
#
# It carries no sources of its own. Everything it exposes is header only and already installed with the
# library so what this target adds is the statement of which surface is supported and a name a build
# can link without reaching into include paths it guessed at.
add_library(AzothRHI-backend-sdk INTERFACE)
add_library(azoth::rhi-backend-sdk ALIAS AzothRHI-backend-sdk)
target_link_libraries(AzothRHI-backend-sdk INTERFACE AzothRHI)
set_target_properties(AzothRHI-backend-sdk PROPERTIES EXPORT_NAME rhi-backend-sdk)

# What a loadable backend links, which is the backend SDK plus the entry point and the stamp.
#
# Interface only, like the SDK it sits on. A module compiles the entry point itself through
# AZO_RHI_DEFINE_MODULE so there is no object here to link and the stamp has to be evaluated in the
# module's own translation unit or it would report the host's answer and not the module's.
add_library(AzothRHI-module-sdk INTERFACE)
add_library(azoth::rhi-module-sdk ALIAS AzothRHI-module-sdk)
target_link_libraries(AzothRHI-module-sdk INTERFACE AzothRHI-backend-sdk)
set_target_properties(AzothRHI-module-sdk PROPERTIES EXPORT_NAME rhi-module-sdk)

if(AZOTH_RHI_BACKEND_VULKAN)
    add_library(AzothRHI-native-vulkan INTERFACE)
    add_library(azoth::rhi-native-vulkan ALIAS AzothRHI-native-vulkan)
    target_link_libraries(AzothRHI-native-vulkan INTERFACE AzothRHI vk::dynamic_hpp_dispatch)
    target_compile_definitions(AzothRHI-native-vulkan INTERFACE VULKAN_HPP_NO_TO_STRING=1)

    # Matched to what the library itself was compiled with. A consumer reading VulkanNativeDevice::dispatch has to lay DispatchLoaderDynamic out the
    # same way the backend that filled it did, and the Win32 entry points are members of that table.
    if(WIN32)
        target_compile_definitions(AzothRHI-native-vulkan INTERFACE VK_USE_PLATFORM_WIN32_KHR)
    endif()
    set_target_properties(AzothRHI-native-vulkan PROPERTIES EXPORT_NAME rhi-native-vulkan)
endif()

# The Metal counterpart, and the same gap it closes.
#
# metal_native.hpp forward declares the MTL and MTL4 types its payloads are made of, which is enough to
# pass one around and not enough to call anything on it. A consumer reading MetalNativeDevice::device, or
# taking a frame capture through MTLCaptureManager, needs metal-cpp on its include path, and guessing at
# where the fetched copy landed is not something a consumer should have to do.
#
# One target for both backends. The payload types are the same objects on either, and a program that
# reaches for native access does not know which generation it came up on until it asks.
if(AZOTH_RHI_BACKEND_METAL OR AZOTH_RHI_BACKEND_METAL4)
    add_library(AzothRHI-native-metal INTERFACE)
    add_library(azoth::rhi-native-metal ALIAS AzothRHI-native-metal)
    target_link_libraries(AzothRHI-native-metal INTERFACE AzothRHI metal_cpp)
    set_target_properties(AzothRHI-native-metal PROPERTIES EXPORT_NAME rhi-native-metal)
endif()

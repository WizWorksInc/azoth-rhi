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

# Both arms drive Vulkan, so a build without the Vulkan backend has nothing to compare.
if (NOT AZOTH_RHI_BACKEND_VULKAN)
    message(FATAL_ERROR "AzothRHI: AZOTH_RHI_BENCH_NRI compares the two libraries on Vulkan, and this build has no Vulkan backend.")
endif ()

set(AZOTH_RHI_NRI_TAG "v180" CACHE STRING "NRI release the comparison builds against")
mark_as_advanced(AZOTH_RHI_NRI_TAG)

# Static, so neither arm pays a dylib stub the other does not. Vulkan only, since that is the API both arms share on every platform.
set(NRI_STATIC_LIBRARY ON CACHE BOOL "" FORCE)
set(NRI_ENABLE_VK_SUPPORT ON CACHE BOOL "" FORCE)
set(NRI_ENABLE_D3D11_SUPPORT OFF CACHE BOOL "" FORCE)
set(NRI_ENABLE_D3D12_SUPPORT OFF CACHE BOOL "" FORCE)
set(NRI_ENABLE_WGPU_SUPPORT OFF CACHE BOOL "" FORCE)
set(NRI_ENABLE_NONE_SUPPORT OFF CACHE BOOL "" FORCE)

# Validation is off at run time in both arms, so NRI's layer is compiled out rather than carried and skipped.
set(NRI_ENABLE_VALIDATION_SUPPORT OFF CACHE BOOL "" FORCE)

# The Azoth arm turns debug names and labels off on its device, so NRI's are compiled out to match.
set(NRI_ENABLE_NVTX_SUPPORT OFF CACHE BOOL "" FORCE)
set(NRI_ENABLE_DEBUG_NAMES_AND_ANNOTATIONS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(nri
        GIT_REPOSITORY https://github.com/NVIDIA-RTX/NRI.git
        GIT_TAG ${AZOTH_RHI_NRI_TAG}
        GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(nri)

# NRI builds with -Werror, and a warning a newer compiler finds in code this project does not own must not fail the build.
target_compile_options(NRI_Shared PUBLIC $<IF:$<CXX_COMPILER_FRONTEND_VARIANT:MSVC>,/WX-,-Wno-error>)

# The Vulkan headers NRI pinned, so the raw control arm in both executables compiles against one set.
FetchContent_GetProperties(vulkan_headers)
if (NOT vulkan_headers_SOURCE_DIR)
    message(FATAL_ERROR "AzothRHI: NRI ${AZOTH_RHI_NRI_TAG} no longer fetches vulkan_headers, which the raw Vulkan arm compiles against.")
endif ()

# NRI's own sample data, so the scene benchmark draws the scene NRI's SceneViewer opens by default.
FetchContent_Declare(nri_samples_data
        URL https://github.com/NVIDIA-RTX/NRISamples/releases/download/assets/_Data.7z
        URL_HASH SHA256=0b937cf4d75666bd29012b7f6d66ff13fd4fb1279d7b1bf690c232e103233347
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(nri_samples_data)

set(AZOTH_RHI_VS_NRI_DATA_DIR "${nri_samples_data_SOURCE_DIR}")
if (EXISTS "${nri_samples_data_SOURCE_DIR}/_Data/Scenes")
    set(AZOTH_RHI_VS_NRI_DATA_DIR "${nri_samples_data_SOURCE_DIR}/_Data")
endif ()
if (NOT EXISTS "${AZOTH_RHI_VS_NRI_DATA_DIR}/Scenes/ShaderBalls/ShaderBalls.gltf")
    message(FATAL_ERROR "AzothRHI: the NRISamples data archive no longer holds Scenes/ShaderBalls/ShaderBalls.gltf.")
endif ()

# Same pins and cache entries as cmake/azoth_rhi_examples.cmake, so one setting moves both.
set(AZOTH_RHI_FASTGLTF_TAG "v0.9.0" CACHE STRING "fastgltf version the scene loader is built against")
set(AZOTH_RHI_STB_TAG "31c1ad37456438565541f4919958214b6e762fb4" CACHE STRING "stb commit the image decoder is built against")
mark_as_advanced(AZOTH_RHI_FASTGLTF_TAG AZOTH_RHI_STB_TAG)

FetchContent_Declare(fastgltf
        GIT_REPOSITORY https://github.com/spnda/fastgltf.git
        GIT_TAG ${AZOTH_RHI_FASTGLTF_TAG}
        GIT_SHALLOW TRUE
)
FetchContent_Declare(stb
        GIT_REPOSITORY https://github.com/nothings/stb.git
        GIT_TAG ${AZOTH_RHI_STB_TAG}
)
FetchContent_MakeAvailable(fastgltf stb)

if (NOT TARGET azoth_rhi_stb)
    add_library(azoth_rhi_stb INTERFACE)
    target_include_directories(azoth_rhi_stb SYSTEM INTERFACE ${stb_SOURCE_DIR})
endif ()

add_library(rhi_bench_vs_nri_harness STATIC harness.cpp raw_vulkan.cpp scene_asset.cpp)
target_link_libraries(rhi_bench_vs_nri_harness
        PUBLIC benchmark::benchmark
        PRIVATE fastgltf::fastgltf azoth_rhi_stb)
target_include_directories(rhi_bench_vs_nri_harness
        PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
        PRIVATE ${vulkan_headers_SOURCE_DIR}/include)
target_compile_definitions(rhi_bench_vs_nri_harness PRIVATE VK_NO_PROTOTYPES VSNRI_SCENE_DATA_DIR="${AZOTH_RHI_VS_NRI_DATA_DIR}")
target_compile_features(rhi_bench_vs_nri_harness PUBLIC cxx_std_23)
set_target_properties(rhi_bench_vs_nri_harness PROPERTIES FOLDER benchmarks)

# The NRI arm links no part of this library, which also keeps its VMA and NRI's from meeting in one link.
add_executable(rhi_bench_vs_nri_nri nri_arm.cpp)
azoth_rhi_use_tracy_lifetime(rhi_bench_vs_nri_nri)
target_link_libraries(rhi_bench_vs_nri_nri PRIVATE rhi_bench_vs_nri_harness NRI)
set_target_properties(rhi_bench_vs_nri_nri PROPERTIES
        FOLDER benchmarks
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

add_executable(rhi_bench_vs_nri_azoth azoth_arm.cpp)
azoth_rhi_use_tracy_lifetime(rhi_bench_vs_nri_azoth)
target_link_libraries(rhi_bench_vs_nri_azoth PRIVATE rhi_bench_vs_nri_harness azoth::rhi azoth::rhi-native-vulkan)
set_target_properties(rhi_bench_vs_nri_azoth PROPERTIES
        FOLDER benchmarks
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
azoth_rhi_stage_test_runtime(rhi_bench_vs_nri_azoth)

# The Tracy client reaches the Azoth arm through the library's public link. The harness and the NRI arm take it directly.
if (AZOTH_RHI_TRACY_ENABLED)
    target_link_libraries(rhi_bench_vs_nri_harness PUBLIC ${AZOTH_RHI_TRACY_TARGET})
endif ()

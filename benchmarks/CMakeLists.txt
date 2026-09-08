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

set(AZOTH_RHI_BENCHMARK_TAG "v1.9.5" CACHE STRING "Google Benchmark version")
mark_as_advanced(AZOTH_RHI_BENCHMARK_TAG)

# Its suite wants a GoogleTest we do not fetch. Its install rules would reach our export set.
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)

# A new warning in code this project does not own must not fail the build.
set(BENCHMARK_ENABLE_WERROR OFF CACHE BOOL "" FORCE)

FetchContent_Declare(googlebenchmark
        GIT_REPOSITORY https://github.com/google/benchmark.git
        GIT_TAG ${AZOTH_RHI_BENCHMARK_TAG}
        GIT_SHALLOW TRUE
        FIND_PACKAGE_ARGS NAMES benchmark
)
FetchContent_MakeAvailable(googlebenchmark)

add_library(rhi_bench_support STATIC
        shared/options.cpp
        shared/pass_plan.cpp
        shared/spread_gate.cpp)
add_library(azoth::rhi-bench-support ALIAS rhi_bench_support)

# benchmark::benchmark and not benchmark_main, since each benchmark owns its main.
target_link_libraries(rhi_bench_support PUBLIC azoth::rhi benchmark::benchmark)
target_include_directories(rhi_bench_support PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(rhi_bench_support PUBLIC cxx_std_23)
set_target_properties(rhi_bench_support PROPERTIES FOLDER benchmarks)

# A fourth benchmark added through this gets its Windows runtime staged without anyone remembering to.
function(azoth_rhi_add_benchmark target)
    add_executable(${target} ${ARGN})
    target_link_libraries(${target} PRIVATE azoth::rhi-bench-support)
    set_target_properties(${target} PROPERTIES
            FOLDER benchmarks
            RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    azoth_rhi_stage_test_runtime(${target})
endfunction()

azoth_rhi_add_benchmark(rhi_bench_recording recording/main.cpp)

# indirect.cpp is its own file so the optimiser knows no more about that arm than about the RHI's.
azoth_rhi_add_benchmark(rhi_bench_dispatch dispatch/main.cpp dispatch/indirect.cpp)

set(_native_delta_sources native_delta/main.cpp native_delta/native/arms.cpp)

if (AZOTH_RHI_BACKEND_VULKAN)
    list(APPEND _native_delta_sources native_delta/native/vulkan_arm.cpp)
endif ()

if (AZOTH_RHI_BACKEND_METAL OR AZOTH_RHI_BACKEND_METAL4)
    list(APPEND _native_delta_sources native_delta/native/metal_arm.cpp)
endif ()

if (AZOTH_RHI_BACKEND_D3D12)
    list(APPEND _native_delta_sources native_delta/native/d3d12_arm.cpp)
endif ()

azoth_rhi_add_benchmark(rhi_bench_native_delta ${_native_delta_sources})

if (AZOTH_RHI_BACKEND_VULKAN)
    target_link_libraries(rhi_bench_native_delta PRIVATE azoth::rhi-native-vulkan)
    target_compile_definitions(rhi_bench_native_delta PRIVATE AZOTH_RHI_BENCH_VULKAN)
endif ()

if (AZOTH_RHI_BACKEND_METAL OR AZOTH_RHI_BACKEND_METAL4)
    target_link_libraries(rhi_bench_native_delta PRIVATE metal_cpp)
    target_compile_definitions(rhi_bench_native_delta PRIVATE AZOTH_RHI_BENCH_METAL)

    if (AZOTH_RHI_BACKEND_METAL)
        target_compile_definitions(rhi_bench_native_delta PRIVATE AZOTH_RHI_BENCH_METAL3)
    endif ()
    if (AZOTH_RHI_BACKEND_METAL4)
        target_compile_definitions(rhi_bench_native_delta PRIVATE AZOTH_RHI_BENCH_METAL4)
    endif ()
endif ()

if (AZOTH_RHI_BACKEND_D3D12)
    target_link_libraries(rhi_bench_native_delta PRIVATE d3d12)
    target_compile_definitions(rhi_bench_native_delta PRIVATE AZOTH_RHI_BENCH_D3D12)
endif ()

if (AZOTH_RHI_BUILD_TESTS)
    add_test(NAME benchmark_recording_smoke
            COMMAND rhi_bench_recording null --commands 5000 --warmup 0 --benchmark_dry_run)
    set_tests_properties(benchmark_recording_smoke PROPERTIES LABELS benchmark)

    # The native arm has no Null backend, so this proves only that the harness runs and skips.
    add_test(NAME benchmark_native_delta_smoke
            COMMAND rhi_bench_native_delta null --commands 5000 --warmup 0 --benchmark_dry_run)
    set_tests_properties(benchmark_native_delta_smoke PROPERTIES LABELS benchmark)

    add_test(NAME benchmark_dispatch_smoke
            COMMAND rhi_bench_dispatch --commands 5000 --warmup 0 --validation off --benchmark_dry_run)
    set_tests_properties(benchmark_dispatch_smoke PROPERTIES LABELS benchmark)
endif ()

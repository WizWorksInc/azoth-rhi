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

# The measurements the non-functional budgets are checked against. The ctest entries here catch a
# benchmark that stopped working, not one that got slower.

include(FetchContent)

set(AZOTH_RHI_BENCHMARK_TAG "v1.9.5" CACHE STRING "Google Benchmark version")
mark_as_advanced(AZOTH_RHI_BENCHMARK_TAG)

# Its own suite wants a GoogleTest this directory does not fetch. Its install rules would put a
# third-party target into our export set.
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)

# The same reason the test suite demotes GoogleTest's own -Werror: a new warning in code this
# project does not own must not fail the build.
set(BENCHMARK_ENABLE_WERROR OFF CACHE BOOL "" FORCE)

FetchContent_Declare(googlebenchmark
        GIT_REPOSITORY https://github.com/google/benchmark.git
        GIT_TAG ${AZOTH_RHI_BENCHMARK_TAG}
        GIT_SHALLOW TRUE
        FIND_PACKAGE_ARGS NAMES benchmark
)
FetchContent_MakeAvailable(googlebenchmark)

# What every benchmark here shares: the command line on top of Google Benchmark's, the tolerance a
# run is judged on and the run sizing.
add_library(rhi_bench_support STATIC
        shared/options.cpp
        shared/pass_plan.cpp
        shared/spread_gate.cpp)
add_library(azoth::rhi-bench-support ALIAS rhi_bench_support)

# benchmark::benchmark, since every benchmark here owns its main so it can measure the machine
# before it decides what to register.
target_link_libraries(rhi_bench_support PUBLIC azoth::rhi benchmark::benchmark)
target_include_directories(rhi_bench_support PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(rhi_bench_support PUBLIC cxx_std_23)
set_target_properties(rhi_bench_support PROPERTIES FOLDER benchmarks)

add_executable(rhi_bench_recording recording/main.cpp)
target_link_libraries(rhi_bench_recording PRIVATE azoth::rhi-bench-support)
set_target_properties(rhi_bench_recording PROPERTIES
        FOLDER benchmarks
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

# The floor the other two are read against. indirect.cpp is a file of its own so the optimiser
# knows no more about that arm than about the RHI's own block.
add_executable(rhi_bench_dispatch dispatch/main.cpp dispatch/indirect.cpp)
target_link_libraries(rhi_bench_dispatch PRIVATE azoth::rhi-bench-support)
set_target_properties(rhi_bench_dispatch PROPERTIES
        FOLDER benchmarks
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

# What the abstraction costs against the native API recording the same command. It is the one
# benchmark allowed to link the quarantined native targets.
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

add_executable(rhi_bench_native_delta ${_native_delta_sources})
target_link_libraries(rhi_bench_native_delta PRIVATE azoth::rhi-bench-support)
set_target_properties(rhi_bench_native_delta PROPERTIES
        FOLDER benchmarks
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

if (AZOTH_RHI_BACKEND_VULKAN)
    target_link_libraries(rhi_bench_native_delta PRIVATE azoth::rhi-native-vulkan)
    target_compile_definitions(rhi_bench_native_delta PRIVATE AZOTH_RHI_BENCH_VULKAN)
endif ()

if (AZOTH_RHI_BACKEND_METAL OR AZOTH_RHI_BACKEND_METAL4)
    # metal-cpp because the Metal native header names MTL types it only forward declares. A define
    # per backend too, since each half needs its own.
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
    # --benchmark_dry_run is one iteration with no repetitions, which leaves nothing for the spread
    # check to rule on. The warm-up and the budgets are ours.
    add_test(NAME benchmark_recording_smoke
            COMMAND rhi_bench_recording null --commands 5000 --warmup 0 --benchmark_dry_run)
    set_tests_properties(benchmark_recording_smoke PROPERTIES LABELS benchmark)

    # The native arm has no Null backend to reach. This proves the harness still runs and reports
    # the clean skip.
    add_test(NAME benchmark_native_delta_smoke
            COMMAND rhi_bench_native_delta null --commands 5000 --warmup 0 --benchmark_dry_run)
    set_tests_properties(benchmark_native_delta_smoke PROPERTIES LABELS benchmark)

    # Null is the only backend this one runs against. One mode named on the line keeps it to eight
    # benchmarks instead of twenty-four.
    add_test(NAME benchmark_dispatch_smoke
            COMMAND rhi_bench_dispatch --commands 5000 --warmup 0 --validation off --benchmark_dry_run)
    set_tests_properties(benchmark_dispatch_smoke PROPERTIES LABELS benchmark)
endif ()

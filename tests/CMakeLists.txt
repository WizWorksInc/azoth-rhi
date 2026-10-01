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

set(AZOTH_RHI_GOOGLETEST_TAG "v1.15.2" CACHE STRING "GoogleTest version")
mark_as_advanced(AZOTH_RHI_GOOGLETEST_TAG)

FetchContent_Declare(googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG ${AZOTH_RHI_GOOGLETEST_TAG}
        GIT_SHALLOW TRUE
        FIND_PACKAGE_ARGS NAMES GTest
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

# Under MSVC gmock compiles its own gtest
set(_azoth_rhi_shared_libs ${BUILD_SHARED_LIBS})
set(BUILD_SHARED_LIBS OFF)
FetchContent_MakeAvailable(googletest)
set(BUILD_SHARED_LIBS ${_azoth_rhi_shared_libs})
unset(_azoth_rhi_shared_libs)

# GoogleTest compiles itself with -Werror.
foreach (_gtest_target IN ITEMS gtest gtest_main gmock gmock_main)
    if (TARGET ${_gtest_target})
        target_compile_options(${_gtest_target} PRIVATE
                $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wno-error>
                $<$<CXX_COMPILER_ID:MSVC>:/WX->)
    endif ()
endforeach ()

include(GoogleTest)

set(AZOTH_RHI_TESTS_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
include(azoth_rhi_test_helpers)
include(azoth_rhi_test_options)

add_library(AzothRHITest STATIC
        support/conformance/backend_contract.cpp
        support/conformance/block_contract.cpp
        support/conformance/overload_contract.cpp
        support/fixtures/minimal_backend.cpp
        support/harness/backends.cpp
        support/harness/environment.cpp
        support/harness/selection.cpp
)
add_library(azoth::rhi-test ALIAS AzothRHITest)

target_link_libraries(AzothRHITest PUBLIC azoth::rhi azoth::rhi-backend-sdk GTest::gtest GTest::gmock)

target_include_directories(AzothRHITest PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/support
)

target_compile_features(AzothRHITest PUBLIC cxx_std_23)

include(azoth_rhi_module)

azoth_rhi_add_backend_module(rhi_test_loadable_backend SOURCES support/modules/loadable_backend.cpp)

add_library(rhi_test_object_backend OBJECT support/backends/object_library_backend.cpp)
target_link_libraries(rhi_test_object_backend PUBLIC azoth::rhi)
target_compile_features(rhi_test_object_backend PUBLIC cxx_std_23)

add_library(rhi_test_static_backend STATIC support/backends/static_library_backend.cpp)
target_link_libraries(rhi_test_static_backend PUBLIC azoth::rhi)
target_compile_features(rhi_test_static_backend PUBLIC cxx_std_23)

if (AZOTH_RHI_BUILD_UNIT_TESTS)
    add_subdirectory(unit)
endif ()

if (AZOTH_RHI_BUILD_RIGOROUS_TESTS)
    add_subdirectory(rigorous)
endif ()

if (AZOTH_RHI_BUILD_RIGOROUS_TESTS)
    add_subdirectory(shader_abi)
endif ()

if (AZOTH_RHI_BUILD_RIGOROUS_TESTS)
    add_subdirectory(adoption)
endif ()

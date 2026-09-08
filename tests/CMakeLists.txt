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

# Everything here runs against whichever backends this build bundled. The Null backend needs no
# driver, which is what makes the whole suite meaningful in CI.

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
FetchContent_MakeAvailable(googletest)

# GoogleTest compiles itself with -Werror. A new warning in its own headers fails a build this
# project cannot fix. Clang 22's -Wcharacter-conversion was one.
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

# The harness every suite links. azoth_rhi_add_gtest_suite compiles main.cpp into each suite so the
# per-target validation definitions reach the entry point. The two hand-built suites, adoption and
# shader_abi, link gtest_main instead.
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

# The backend SDK because the block checks ask an object what it publishes, which only the dispatch
# header can ask.
target_link_libraries(AzothRHITest PUBLIC azoth::rhi azoth::rhi-backend-sdk GTest::gtest GTest::gmock)

target_include_directories(AzothRHITest PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/support
)

target_compile_features(AzothRHITest PUBLIC cxx_std_23)

include(azoth_rhi_module)

# The loadable backend, built the way a host would and never linked by anything. The suite is told
# where it landed instead of reconstructing the path.
azoth_rhi_add_backend_module(rhi_test_loadable_backend SOURCES support/modules/loadable_backend.cpp)

# The two library kinds that decide whether self-registration works: an OBJECT library has no
# archive to drop its translation unit from and a static one does.
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

# Grouped with the rigorous suites. It cross-checks instead of covering one module. It is also the
# one suite that reaches for a shader compiler.
if (AZOTH_RHI_BUILD_RIGOROUS_TESTS)
    add_subdirectory(shader_abi)
endif ()

# Grouped here too. Adoption is templated on an API tag. It names a graphics API, which a portable
# suite cannot reach.
if (AZOTH_RHI_BUILD_RIGOROUS_TESTS)
    add_subdirectory(adoption)
endif ()

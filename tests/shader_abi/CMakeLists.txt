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

# The one suite that needs a shader compiler, kept apart from the rest for that reason.
#
# Everything else here measures the RHI against itself. This measures the published binding ABI against
# what a real compiler emits, which is the only way to catch the two drifting apart, and it is also the
# only reason any test would want Slang. A host without Slang skips the suite without failing.

include(azoth_rhi_shader_tools)

azoth_rhi_provide_slang()

if(NOT slang_FOUND)
    message(STATUS "AzothRHI tests: skipping the shader ABI suite, Slang was not found")
    return()
endif()

# gtest's own main and not the shared one, which brings a device harness parameterised over every
# backend. One test here does create a device, but on whichever backend the build prefers and not on
# each in turn, so the harness would cost more than it gives.
add_executable(rhi_shader_abi abi_agreement_test.cpp)

target_link_libraries(rhi_shader_abi PRIVATE azoth::rhi GTest::gtest GTest::gtest_main slang::slang)
set_target_properties(rhi_shader_abi PROPERTIES FOLDER tests)

# Slang ships as a shared library, so on Windows it has to sit beside the executable or the test dies at
# load time with no output at all. TARGET_RUNTIME_DLLS resolves whatever the imported target actually needs,
# which is nothing on the platforms that use an rpath.
if(WIN32)
    add_custom_command(TARGET rhi_shader_abi POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:rhi_shader_abi> $<TARGET_FILE_DIR:rhi_shader_abi>
            COMMAND_EXPAND_LISTS)
endif()

add_test(NAME shader_abi COMMAND rhi_shader_abi)
set_tests_properties(shader_abi PROPERTIES LABELS conformance)

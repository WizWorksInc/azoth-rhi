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

add_executable(rhi_adoption
        native/adoption_test.cpp
        native/export_surface_test.cpp)

target_link_libraries(rhi_adoption PRIVATE azoth::rhi azoth::rhi-test GTest::gtest GTest::gtest_main)
set_target_properties(rhi_adoption PROPERTIES FOLDER tests)

# Built by hand, so the staging azoth_rhi_add_gtest_suite would have done is asked for here.
azoth_rhi_stage_test_runtime(rhi_adoption)

# Which backends compiled is deliberately not answerable from a public header, hence these defines.
if (TARGET azoth::rhi-native-vulkan)
    target_link_libraries(rhi_adoption PRIVATE azoth::rhi-native-vulkan)

    # Matches the backend, so calls here return a ResultValue instead of throwing.
    target_compile_definitions(rhi_adoption PRIVATE AZOTH_RHI_TEST_ADOPTION_VULKAN VULKAN_HPP_NO_EXCEPTIONS)
endif ()

if (AZOTH_RHI_BACKEND_METAL OR AZOTH_RHI_BACKEND_METAL4)
    target_compile_definitions(rhi_adoption PRIVATE AZOTH_RHI_TEST_ADOPTION_METAL)

    if (AZOTH_RHI_BACKEND_METAL)
        target_compile_definitions(rhi_adoption PRIVATE AZOTH_RHI_TEST_ADOPTION_METAL3)
    endif ()
    if (AZOTH_RHI_BACKEND_METAL4)
        target_compile_definitions(rhi_adoption PRIVATE AZOTH_RHI_TEST_ADOPTION_METAL4)
    endif ()
endif ()

if (AZOTH_RHI_BACKEND_D3D12)
    target_compile_definitions(rhi_adoption PRIVATE AZOTH_RHI_TEST_ADOPTION_D3D12)
endif ()

# Per case, so a backend with no driver skips its own cases instead of the whole suite.
gtest_discover_tests(rhi_adoption
        DISCOVERY_MODE PRE_TEST
        DISCOVERY_TIMEOUT ${AZOTH_RHI_TEST_DISCOVERY_TIMEOUT}
        PROPERTIES LABELS conformance
)

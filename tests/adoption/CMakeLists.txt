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

# The one suite that names a graphics API on purpose. Adoption is templated on an API tag, so an
# entry call means naming Vulkan or Metal or Direct3D 12, which the portable conformance suite may
# not do. That is why the overload contract excludes these entries and points here instead.

# Under native/ so the layout matches lib/include, which is the only tree the API boundary gate
# scans. Convention here rather than enforcement, since the gate never reaches tests.
add_executable(rhi_adoption
        native/adoption_test.cpp
        native/export_surface_test.cpp)

target_link_libraries(rhi_adoption PRIVATE azoth::rhi azoth::rhi-test GTest::gtest GTest::gtest_main)
set_target_properties(rhi_adoption PROPERTIES FOLDER tests)

# Built by hand instead of through azoth_rhi_add_gtest_suite, so the staging that helper does is
# asked for here.
azoth_rhi_stage_test_runtime(rhi_adoption)

# The native headers sit behind their own interface targets, so a consumer opts in per backend and
# this suite is a consumer like any other. Each also defines the macro its half of the file is
# guarded by, since which backends compiled is deliberately not answerable from a public header.
if (TARGET azoth::rhi-native-vulkan)
    target_link_libraries(rhi_adoption PRIVATE azoth::rhi-native-vulkan)

    # The same Vulkan-Hpp convention the backend is built with, so a call here hands back a
    # ResultValue this file checks, without relying on a throw the library would never make.
    target_compile_definitions(rhi_adoption PRIVATE AZOTH_RHI_TEST_ADOPTION_VULKAN VULKAN_HPP_NO_EXCEPTIONS)
endif ()

if (AZOTH_RHI_BACKEND_METAL OR AZOTH_RHI_BACKEND_METAL4)
    target_compile_definitions(rhi_adoption PRIVATE AZOTH_RHI_TEST_ADOPTION_METAL)

    # One define per generation as well, since the two are separate backends with separate entry
    # points and a case naming one only links where that backend compiled.
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

# Discovered per case, not registered as one, so a backend with no driver skips its own cases
# instead of taking the whole suite down. The run then reads as an inventory of what was reachable.
gtest_discover_tests(rhi_adoption
        DISCOVERY_MODE PRE_TEST
        PROPERTIES LABELS conformance
)

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

# The unit-test module registry.
#
# One entry per directory under tests/unit. Covering a new part of the API means creating that
# directory, dropping <name>_test.cpp into it, adding the target name here and appending the module
# to the order list. Nothing else has to change: tests/unit/CMakeLists.txt walks this registry.
#
# A module needing its own compile flags cannot go here, since one target cannot be built twice with
# two definitions. Leave it out of the registry and wire it by hand in tests/unit instead.

set(AZOTH_RHI_UNIT_MODULE_core rhi_unit_core)
set(AZOTH_RHI_UNIT_MODULE_detail rhi_unit_detail)
set(AZOTH_RHI_UNIT_MODULE_api rhi_unit_api)
set(AZOTH_RHI_UNIT_MODULE_builders rhi_unit_builders)
set(AZOTH_RHI_UNIT_MODULE_device rhi_unit_device)
set(AZOTH_RHI_UNIT_MODULE_resources rhi_unit_resources)
set(AZOTH_RHI_UNIT_MODULE_descriptors rhi_unit_descriptors)
set(AZOTH_RHI_UNIT_MODULE_pipeline rhi_unit_pipeline)
set(AZOTH_RHI_UNIT_MODULE_sync rhi_unit_sync)
set(AZOTH_RHI_UNIT_MODULE_command rhi_unit_command)
set(AZOTH_RHI_UNIT_MODULE_swapchain rhi_unit_swapchain)
set(AZOTH_RHI_UNIT_MODULE_allocator rhi_unit_allocator)
set(AZOTH_RHI_UNIT_MODULE_profiler rhi_unit_profiler)
set(AZOTH_RHI_UNIT_MODULE_unique rhi_unit_unique)
set(AZOTH_RHI_UNIT_MODULE_validation rhi_unit_validation)

set(AZOTH_RHI_UNIT_MODULE_ORDER
        core
        detail
        api
        builders
        device
        resources
        descriptors
        pipeline
        sync
        command
        swapchain
        allocator
        profiler
        unique
        validation
)

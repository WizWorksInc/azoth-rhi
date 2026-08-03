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

# Two directories, because the code that supports the samples and the samples themselves answer to
# different rules.
#
# lib/ is the framework every sample links: it may grow abstractions, take dependencies and be reused.
# samples/ is what a reader came for, one directory each, no sample depending on another. A sample
# that hides an RHI call behind a framework helper has made itself worse at the one thing it is for,
# so what belongs in lib/ is the part that is not about the RHI: windowing, logging, assets, scene
# loading.

set(AZOTH_RHI_EXAMPLES_BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR})
include(azoth_rhi_examples)

# Ahead of the samples, not beside them, so it is defined before the first one asks for it.
add_subdirectory(lib)
add_subdirectory(samples)

# One target that builds the lot, for checking that a public API change did not leave a sample behind.
get_property(_example_targets GLOBAL PROPERTY AZOTH_RHI_EXAMPLE_TARGETS)
if(_example_targets)
    add_custom_target(rhi_examples)
    add_dependencies(rhi_examples ${_example_targets})
endif()

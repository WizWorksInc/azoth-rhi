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

# Both dependencies belong to this sample alone so they are looked for here, not anywhere the
# other samples would pay for them. Missing either skips the sample and says which one.
azoth_rhi_provide_sdl3()

if(NOT SDL3_FOUND)
    message(STATUS "AzothRHI examples: skipping hello_triangle, SDL3 was not found")
    return()
endif()

# Slang turns the one shader source into SPIR-V, MSL or DXIL at startup, which is what lets the
# sample carry a readable shader instead of a precompiled blob per backend. It ships with the Vulkan
# SDK so a machine set up to build the Vulkan backend usually already has it.
azoth_rhi_provide_slang()

if(NOT slang_FOUND)
    message(STATUS "AzothRHI examples: skipping hello_triangle, Slang was not found")
    return()
endif()

azoth_rhi_add_example(hello_triangle
        SOURCES main.cpp
        LINK_LIBRARIES azoth::rhi-examples-sdl3 slang::slang
        ASSETS shaders:shaders)

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

include(azoth_rhi_shader_tools)

azoth_rhi_provide_slang()

if(NOT slang_FOUND)
    message(STATUS "AzothRHI examples: skipping shader_reflection, Slang was not found")
    return()
endif()

azoth_rhi_add_example(shader_reflection
        SOURCES main.cpp
        LINK_LIBRARIES slang::slang
        ASSETS shaders:shaders)

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

# Every dependency below belongs to this sample alone, which is why it is looked for here and not
# anywhere the other samples would pay for it. Missing any of them skips the sample and says which
# one, since a windowing library the examples merely demonstrate is not something a build should fail
# over.
find_package(glfw3 QUIET CONFIG)

if(NOT glfw3_FOUND)
    message(STATUS "AzothRHI examples: skipping windowed_present_glfw, GLFW was not found")
    return()
endif()

# The SDL3 sample needs no equivalent of this. GLFW declares glfwCreateWindowSurface only when it is
# allowed to include the Vulkan headers so a machine with GLFW but no Vulkan headers still skips.
find_package(Vulkan QUIET)

if(NOT Vulkan_FOUND)
    message(STATUS "AzothRHI examples: skipping windowed_present_glfw, the Vulkan headers were not found")
    return()
endif()

set(_glfw_sources main.cpp native/glfw_natives.cpp)
set(_glfw_libraries glfw Vulkan::Headers)

# And on macOS it needs Objective-C because glfwGetCocoaWindow stops at the NSWindow and the
# CAMetalLayer has to be made and attached by hand.
if(APPLE)
    list(APPEND _glfw_sources native/glfw_metal_layer.mm)
    list(APPEND _glfw_libraries "-framework AppKit" "-framework QuartzCore")
endif()

azoth_rhi_add_example(windowed_present_glfw
        SOURCES ${_glfw_sources}
        LINK_LIBRARIES ${_glfw_libraries})

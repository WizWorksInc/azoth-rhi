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

# The renderer this shows is azoth::rhi-imgui, which is a shipped target, not part of the sample.
# Turn AZOTH_RHI_BUILD_IMGUI on to get it, and AZOTH_RHI_FETCH_IMGUI on if the host has no Dear ImGui of
# its own, which is what a build of this repository alone will want.
if(NOT TARGET azoth::rhi-imgui)
    message(STATUS "AzothRHI examples: skipping imgui_overlay, azoth::rhi-imgui was not built. Turn on AZOTH_RHI_BUILD_IMGUI.")
    return()
endif()

azoth_rhi_provide_sdl3()

if(NOT SDL3_FOUND)
    message(STATUS "AzothRHI examples: skipping imgui_overlay, SDL3 was not found")
    return()
endif()

# ImGui's own SDL3 platform backend, which is the input half and is nothing to do with the RHI, and its demo
# window, which is what this draws. Both are compiled here, not shipped by azoth::rhi-imgui: which
# platform backend an application wants is its choice, and nobody wants the demo in a library.
if(NOT AZOTH_RHI_IMGUI_SOURCE_DIR)
    message(STATUS "AzothRHI examples: skipping imgui_overlay, the Dear ImGui sources were not fetched, so its SDL3 backend is not here to compile")
    return()
endif()

azoth_rhi_add_example(imgui_overlay
        SOURCES main.cpp
        ${AZOTH_RHI_IMGUI_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
        ${AZOTH_RHI_IMGUI_SOURCE_DIR}/imgui_demo.cpp
        LINK_LIBRARIES azoth::rhi-examples-sdl3 azoth::rhi-imgui)

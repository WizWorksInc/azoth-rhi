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

# SDL3 is this sample's dependency and nobody else's so it is looked for here, not anywhere
# the other samples would pay for it. Not having it skips the sample and says so, since a windowing
# library the examples merely demonstrate is not something a build should fail over.
azoth_rhi_provide_sdl3()

if(NOT SDL3_FOUND)
    message(STATUS "AzothRHI examples: skipping windowed_present_sdl3, SDL3 was not found")
    return()
endif()

azoth_rhi_add_example(windowed_present_sdl3
        SOURCES main.cpp native/sdl_natives.cpp
        LINK_LIBRARIES SDL3::SDL3)

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

# One directory per sample, each with its own CMakeLists.txt calling azoth_rhi_add_example. They are
# found by looking and not by listing, so adding a sample is dropping a directory in beside its
# siblings and reconfiguring.
#
# Most run headless, so they are meaningful on a machine with no GPU by way of the Null backend, and
# those are the ones carrying a smoke test. A sample wanting a window links examples/lib's SDL3 window,
# except windowing_sdl3 and windowing_glfw, which implement rhi::SurfaceSource themselves because
# showing how that is done is what they are for. The two that draw also need Slang for their shaders,
# and deccer_cubes needs a glTF it fetches at configure time. Each is skipped when what it needs is not
# installed, and none of them carries a smoke test, since they want a display.

# CONFIGURE_DEPENDS so a new sample directory is picked up by an already configured tree, not
# waiting for someone to reconfigure by hand.
file(GLOB _sample_dirs
        CONFIGURE_DEPENDS
        LIST_DIRECTORIES true
        RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/*)
list(SORT _sample_dirs)

foreach(_dir IN LISTS _sample_dirs)
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${_dir}/CMakeLists.txt)
        add_subdirectory(${_dir})
    endif()
endforeach()

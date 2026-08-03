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

# The windowing and shader dependencies hello_triangle also has, looked for here so a machine
# without them skips this sample without failing the configure.
azoth_rhi_provide_sdl3()

if(NOT SDL3_FOUND)
    message(STATUS "AzothRHI examples: skipping deccer_cubes, SDL3 was not found")
    return()
endif()

azoth_rhi_provide_slang()

if(NOT slang_FOUND)
    message(STATUS "AzothRHI examples: skipping deccer_cubes, Slang was not found")
    return()
endif()

# The scene, from the deccer-cubes repository. This one glTF carries its buffers and its five
# textures inside itself as base64 so the sample needs the one file and nothing beside it. The hash
# pins the contents: a changed asset upstream is a failed configure and not a changed picture.
set(_asset ${CMAKE_CURRENT_BINARY_DIR}/SM_Deccer_Cubes_Textured_Complex.gltf)
file(DOWNLOAD
        https://raw.githubusercontent.com/GraphicsProgramming/deccer-cubes/585610a802decc8193078f04618d4327fb122bdd/SM_Deccer_Cubes_Textured_Complex.gltf
        ${_asset}
        EXPECTED_HASH SHA256=66db6e8d051676ef7e8eacc1ca4a78dd4f64b40a168bf7d1bd01f85d854b0181
        STATUS _download
        TLS_VERIFY ON
)

list(GET _download 0 _download_code)
if(NOT _download_code EQUAL 0)
    list(GET _download 1 _download_message)
    message(STATUS "AzothRHI examples: skipping deccer_cubes, the scene could not be fetched (${_download_message})")
    return()
endif()

azoth_rhi_add_example(deccer_cubes
        SOURCES main.cpp scene.cpp ibl.cpp shaders.cpp
        LINK_LIBRARIES azoth::rhi-examples-sdl3 slang::slang azoth::rhi-utils fastgltf::fastgltf azoth_rhi_stb
        ASSETS ${_asset} shaders:shaders assets/grasslands_sunset_4k.hdr:environment.hdr)

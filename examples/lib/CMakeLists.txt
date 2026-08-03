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

# What the samples are built on, laid out the way the library at the top of the tree is: headers under
# include/ mirroring the namespaces, sources under src/ mirroring the headers, and one CMakeLists
# naming all of them.
#
# Two targets come out of it. The first is the library every sample links. The second is the SDL3
# window, which is separate for one reason: every sample links the first, including the headless ones,
# and making all of them drag in SDL3 to serve the three that open a window is the wrong trade.

azoth_rhi_provide_glm()
azoth_rhi_provide_quill()
azoth_rhi_provide_asset_libraries()

add_library(AzothRHIExamplesLib STATIC
        src/assets/ImageAsset.cpp
        src/render/BufferPool.cpp
        src/scene/Aabb.cpp
        src/scene/BuiltinPrimitives.cpp
        src/scene/Camera.cpp
        src/scene/CameraController.cpp
        src/scene/GltfSceneLoader.cpp
        src/scene/Light.cpp
        src/scene/Scene.cpp
        src/scene/SceneGpuBufferStorage.cpp
        src/scene/SceneGpuImageStorage.cpp
        src/scene/SceneMesh.cpp
        src/scene/SceneObject.cpp
        src/scene/Transform.cpp
        src/utility/AssetPath.cpp
        src/utility/ExecutablePath.cpp
        src/native/MetalCapture.cpp
        src/utility/Log.cpp
        src/utility/Sample.cpp)
add_library(azoth::rhi-examples-lib ALIAS AzothRHIExamplesLib)

# A plain include directory and not a HEADERS file set, because a file set is the thing an install
# rule ships and nothing here is meant to be shipped. Only the libraries in lib, utils and extras carry
# one. The headers still reach an IDE through the sources that include them, which for a target nobody
# installs is the whole of what a file set was buying.
#
# BUILD_INTERFACE for the same reason: there is no install tree for this to have a path in.
target_include_directories(AzothRHIExamplesLib PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

# Public where a sample sees the type in a header it includes, private where it does not. fastgltf and
# stb are read by this library's own translation units and by nothing a sample compiles, so a sample
# that loads a glTF sees fw::scene::LoadGltf and never the parser behind it.
target_link_libraries(AzothRHIExamplesLib
        PUBLIC azoth::rhi azoth::rhi-utils glm::glm quill::quill
        PRIVATE fastgltf::fastgltf azoth_rhi_stb)

target_compile_features(AzothRHIExamplesLib PUBLIC cxx_std_23)

# Metal frame capture, which is the only tool that answers "what did this frame bind" on this platform:
# RenderDoc has no Metal backend. The native target is what puts metal-cpp on the include path, since
# metal_native.hpp forward declares the MTL types and cannot be called through on its own.
#
# Either Metal backend will do. MTLCaptureManager captures an MTLDevice, which both of them have.
if(AZOTH_RHI_BACKEND_METAL OR AZOTH_RHI_BACKEND_METAL4)
    target_link_libraries(AzothRHIExamplesLib PRIVATE azoth::rhi-native-metal)
    target_compile_definitions(AzothRHIExamplesLib PRIVATE AZOTH_RHI_EXAMPLES_HAVE_METAL)

    # One define per backend as well, because the two native accessors are separate entry points and each
    # is only linkable when its own backend compiled.
    if(AZOTH_RHI_BACKEND_METAL)
        target_compile_definitions(AzothRHIExamplesLib PRIVATE AZOTH_RHI_EXAMPLES_HAVE_METAL3)
    endif()
    if(AZOTH_RHI_BACKEND_METAL4)
        target_compile_definitions(AzothRHIExamplesLib PRIVATE AZOTH_RHI_EXAMPLES_HAVE_METAL4)
    endif()
endif()

set_target_properties(AzothRHIExamplesLib PROPERTIES FOLDER examples)

# The Slang compiler, separate for the reason the SDL3 window below is: every sample links the first
# target and most of them compile no shader, so the ones that do name this and the rest pay nothing.
#
# Ahead of the SDL3 block because that one returns early when SDL3 is missing, and a host without a
# window library still builds the headless samples that compile a shader.
azoth_rhi_provide_slang()

if(slang_FOUND)
    add_library(AzothRHIExamplesSlang STATIC src/shader/SlangCompiler.cpp)
    add_library(azoth::rhi-examples-slang ALIAS AzothRHIExamplesSlang)

    target_include_directories(AzothRHIExamplesSlang PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

    # Slang is private: a sample hands this a path and gets a ShaderBinary, and never sees a Slang type.
    # The two samples that are about compilation itself link slang::slang directly and keep their own.
    target_link_libraries(AzothRHIExamplesSlang
            PUBLIC azoth::rhi azoth::rhi-examples-lib
            PRIVATE slang::slang)

    target_compile_features(AzothRHIExamplesSlang PUBLIC cxx_std_23)

    set_target_properties(AzothRHIExamplesSlang PROPERTIES FOLDER examples)
endif()

# The SDL3 window, skipped when SDL3 is not installed the same way the samples that need it are, so a
# host without it still configures and builds the rest of the tree.
azoth_rhi_provide_sdl3()

if(NOT SDL3_FOUND)
    return()
endif()

# Creating the Vulkan surface is the one part that cannot be written without naming Vulkan's types, so
# it sits under a native/ directory, which is where the API boundary check permits them.
add_library(AzothRHIExamplesSdl3 STATIC
        src/platform/Sdl3Window.cpp
        src/platform/native/Sdl3Natives.cpp)
add_library(azoth::rhi-examples-sdl3 ALIAS AzothRHIExamplesSdl3)

target_include_directories(AzothRHIExamplesSdl3 PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

# SDL3 is public because a sample that owns a window still calls SDL for what this does not wrap.
target_link_libraries(AzothRHIExamplesSdl3
        PUBLIC azoth::rhi azoth::rhi-examples-lib SDL3::SDL3)

target_compile_features(AzothRHIExamplesSdl3 PUBLIC cxx_std_23)

set_target_properties(AzothRHIExamplesSdl3 PROPERTIES FOLDER examples)

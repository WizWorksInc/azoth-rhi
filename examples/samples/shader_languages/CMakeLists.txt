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
    message(STATUS "AzothRHI examples: skipping shader_languages, Slang was not found")
    return()
endif()

azoth_rhi_add_example(shader_languages
        SOURCES main.cpp compiler.cpp
        LINK_LIBRARIES slang::slang
        ASSETS shaders:shaders)

# Everything past here compiles the three non-Slang kernels with the compiler that language is normally
# built by: dxc for HLSL, glslc for GLSL, and Apple's metal for MSL. Slang stays a runtime compile because
# reading a language it was not designed around is the thing that sample is showing.
#
# The output lands in bin/ beside the executable and not in shaders/, because the staging rule clears a
# directory before copying it and the two would race over the same one.
set(_binaries)
set(_build_dir ${CMAKE_CURRENT_BINARY_DIR}/bin)

# Compiles one source and records the result. A tool that is not installed drops its language, not
# failing the build, which is what lets the sample report the gap per language at run time.
#
# outflag because dxc spells its output -Fo where the other two spell it -o, these being three compilers
# from three vendors and not one with three front ends.
function(_shader_languages_compile source output tool outflag)
    if(NOT ${tool})
        return()
    endif()

    add_custom_command(OUTPUT ${_build_dir}/${output}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${_build_dir}
            COMMAND ${${tool}} ${ARGN} ${outflag} ${_build_dir}/${output} ${CMAKE_CURRENT_SOURCE_DIR}/shaders/${source}
            DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/${source}
            COMMENT "Compiling ${source} with ${tool}"
            VERBATIM)

    set(_binaries ${_binaries} ${_build_dir}/${output} PARENT_SCOPE)
endfunction()

# The Vulkan SDK ahead of PATH, because Windows has two dxc and only one of them emits SPIR-V.
find_program(AZOTH_RHI_DXC NAMES dxc
        HINTS $ENV{VULKAN_SDK}/Bin $ENV{VULKAN_SDK}/bin
        DOC "The DirectX shader compiler used to build this sample's HLSL")
find_program(AZOTH_RHI_GLSLC NAMES glslc DOC "The Shaderc GLSL compiler used to build this sample's GLSL")
mark_as_advanced(AZOTH_RHI_DXC AZOTH_RHI_GLSLC)

# Its own lookup and not the one above, because find_program caches and a tree first configured on a
# machine whose PATH dxc was the DXIL-only one would keep answering with it however the hints changed.
find_program(AZOTH_RHI_DXC_SPIRV NAMES dxc
        HINTS $ENV{VULKAN_SDK}/Bin $ENV{VULKAN_SDK}/bin
        DOC "A dxc that can emit SPIR-V, which is not every dxc")
mark_as_advanced(AZOTH_RHI_DXC_SPIRV)

# Proven, not assumed, by compiling the very file and flags the build will. The dxc in the Windows
# SDK is built for DXIL alone and answers a request for SPIR-V by failing, which takes the whole build with
# it instead of only that one language. Asking its help does not settle it: the option is listed either way.
#
# Not cached, so a machine that grows a Vulkan SDK later is asked again, not held to the old answer.
if(AZOTH_RHI_DXC_SPIRV)
    execute_process(
            COMMAND ${AZOTH_RHI_DXC_SPIRV} -T cs_6_0 -E computeMain -spirv
                    -Fo ${CMAKE_CURRENT_BINARY_DIR}/dxc-spirv-probe.spv
                    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/reduce.hlsl
            RESULT_VARIABLE _dxc_spirv_result
            OUTPUT_QUIET
            ERROR_QUIET)

    if(NOT _dxc_spirv_result EQUAL 0)
        message(STATUS "AzothRHI examples: shader_languages has no HLSL on Vulkan, ${AZOTH_RHI_DXC_SPIRV} emits DXIL only")
        set(AZOTH_RHI_DXC_SPIRV "")
    endif()
endif()

# -fspv-entrypoint-name so the SPIR-V names the entry point the sample asks for. Without it dxc renames it
# to main, and the sample would have to know that dxc had been the one to compile it.
_shader_languages_compile(reduce.hlsl reduce.hlsl.spv AZOTH_RHI_DXC_SPIRV -Fo
        -T cs_6_0 -E computeMain -spirv -fspv-target-env=vulkan1.2 -fspv-entrypoint-name=computeMain)

# DXIL only where dxc can sign it. An unsigned container is refused by Direct3D 12 outside developer mode,
# and dxil.dll ships with the Windows SDK and not with dxc.
if(WIN32)
    _shader_languages_compile(reduce.hlsl reduce.hlsl.dxil AZOTH_RHI_DXC -Fo -T cs_6_0 -E computeMain)
endif()

# glslc names every entry point main, GLSL having no way to say otherwise, which is what the sample reads
# back when it asks this kernel for its entry point.
_shader_languages_compile(pattern.glsl pattern.glsl.spv AZOTH_RHI_GLSLC -o -fshader-stage=compute --target-env=vulkan1.2)

# MSL is compiled by the platform's own toolchain or not at all, there being no cross compiler for it.
if(APPLE)
    find_program(AZOTH_RHI_XCRUN NAMES xcrun DOC "Used to reach Apple's metal compiler for this sample's MSL")
    mark_as_advanced(AZOTH_RHI_XCRUN)
    _shader_languages_compile(simd.metal simd.metal.metallib AZOTH_RHI_XCRUN -o -sdk macosx metal -std=metal3.0)
endif()

if(_binaries)
    # Staged the way the sample's other assets are, through a stamp, since the destination is only reachable
    # through a generator expression and one naming a target cannot be a custom command's OUTPUT.
    set(_stamp ${CMAKE_CURRENT_BINARY_DIR}/rhi_shader_languages-bin.stamp)
    set(_staged $<TARGET_FILE_DIR:rhi_shader_languages>/assets/shader_languages/bin)

    add_custom_command(OUTPUT ${_stamp}
            COMMAND ${CMAKE_COMMAND} -E rm -rf ${_staged}
            COMMAND ${CMAKE_COMMAND} -E copy_directory ${_build_dir} ${_staged}
            COMMAND ${CMAKE_COMMAND} -E touch ${_stamp}
            DEPENDS ${_binaries}
            COMMENT "Staging bin for rhi_shader_languages"
            VERBATIM)

    add_custom_target(rhi_shader_languages-binaries DEPENDS ${_stamp})
    add_dependencies(rhi_shader_languages rhi_shader_languages-binaries)
endif()

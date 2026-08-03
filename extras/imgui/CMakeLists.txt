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

# The Dear ImGui renderer, which is the half of an ImGui backend that belongs to a graphics API.
#
# Optional and off by default, because ImGui is a dependency this library does not otherwise have and a
# consumer that wants no interface should not be made to carry one.
#
# The ImGui itself is yours. This links a target you already have without fetching one, because an
# application almost always has an ImGui already, and a second copy in the same process is two contexts,
# two font atlases and one very confusing afternoon. Point AZOTH_RHI_IMGUI_TARGET at whatever yours is
# called if it is not the usual name.

set(AZOTH_RHI_IMGUI_TARGET "imgui::imgui" CACHE STRING "The Dear ImGui target azoth::rhi-imgui links")
set(AZOTH_RHI_FETCH_IMGUI OFF CACHE BOOL "Fetch Dear ImGui when the host provides none. Off so a consumer never ends up with two")
set(AZOTH_RHI_IMGUI_TAG "v1.92.4" CACHE STRING "Dear ImGui tag fetched when AZOTH_RHI_FETCH_IMGUI is on")
mark_as_advanced(AZOTH_RHI_IMGUI_TARGET AZOTH_RHI_IMGUI_TAG)

# Three ways in, cheapest first, and the fetch only when it was asked for. Same shape as the sample
# dependencies, judged by whether the target exists and not by a _FOUND of its own.
if(NOT TARGET ${AZOTH_RHI_IMGUI_TARGET})
    find_package(imgui QUIET CONFIG)
endif()

if(NOT TARGET ${AZOTH_RHI_IMGUI_TARGET} AND AZOTH_RHI_FETCH_IMGUI)
    message(STATUS "AzothRHI: no Dear ImGui found, fetching ${AZOTH_RHI_IMGUI_TAG}")
    include(FetchContent)

    FetchContent_Declare(imgui
            GIT_REPOSITORY https://github.com/ocornut/imgui.git
            GIT_TAG ${AZOTH_RHI_IMGUI_TAG}
            GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(imgui)

    # ImGui ships no CMakeLists, so a fetched one is wrapped here. Only the core: the platform backends are
    # the consumer's choice and the renderer backend is what this library replaces.
    add_library(azoth_rhi_imgui_vendored STATIC
            ${imgui_SOURCE_DIR}/imgui.cpp
            ${imgui_SOURCE_DIR}/imgui_draw.cpp
            ${imgui_SOURCE_DIR}/imgui_tables.cpp
            ${imgui_SOURCE_DIR}/imgui_widgets.cpp)

    # SYSTEM so the warning levels this build turns up do not apply to headers nobody here can fix.
    target_include_directories(azoth_rhi_imgui_vendored SYSTEM PUBLIC ${imgui_SOURCE_DIR})
    add_library(imgui::imgui ALIAS azoth_rhi_imgui_vendored)

    # Where the platform backends live, for whoever wants one. The sample compiles imgui_impl_sdl3.cpp from here.
    set(AZOTH_RHI_IMGUI_SOURCE_DIR ${imgui_SOURCE_DIR} CACHE INTERNAL "Where a fetched Dear ImGui was unpacked")
endif()

if(NOT TARGET ${AZOTH_RHI_IMGUI_TARGET})
    message(STATUS "AzothRHI: skipping azoth::rhi-imgui, no Dear ImGui target called ${AZOTH_RHI_IMGUI_TARGET}. "
            "Provide one, set AZOTH_RHI_IMGUI_TARGET, or turn on AZOTH_RHI_FETCH_IMGUI.")
    return()
endif()

# The shader compiles here at build time and is embedded, the same way the utility target's is, so this
# links no shader compiler and is skipped, not degraded when there is none to compile with.
#
# Four sources say the same thing. Slang is the default because one file reaches all three targets, which
# means one thing to keep correct. The other three are there for a consumer who would rather not add Slang
# to their build, or who wants the shader in a language their own already speaks: GLSL for Vulkan, HLSL for
# Direct3D 12, MSL for Metal. Whichever is chosen, the binaries are the same shape and the renderer does not
# know which compiler produced them.
set(AZOTH_RHI_IMGUI_SHADER_LANGUAGE "slang" CACHE STRING
        "What the imgui shader is compiled from: slang, or native for each backend's own language")
set_property(CACHE AZOTH_RHI_IMGUI_SHADER_LANGUAGE PROPERTY STRINGS slang native)

if(NOT AZOTH_RHI_IMGUI_SHADER_LANGUAGE MATCHES "^(slang|native)$")
    message(FATAL_ERROR "AzothRHI: AZOTH_RHI_IMGUI_SHADER_LANGUAGE is '${AZOTH_RHI_IMGUI_SHADER_LANGUAGE}', expected slang or native")
endif()

set(_shader_targets "")
if(AZOTH_RHI_BACKEND_VULKAN)
    list(APPEND _shader_targets "spirv")
endif()
if(AZOTH_RHI_BACKEND_D3D12)
    list(APPEND _shader_targets "dxil")
endif()
if(AZOTH_RHI_BACKEND_METAL OR AZOTH_RHI_BACKEND_METAL4)
    list(APPEND _shader_targets "metallib")
endif()

if(NOT _shader_targets)
    message(STATUS "AzothRHI: skipping azoth::rhi-imgui, no backend it can compile a shader for is enabled")
    return()
endif()

if(AZOTH_RHI_IMGUI_SHADER_LANGUAGE STREQUAL "slang")
    if(TARGET slang::slang)
        get_target_property(_slang_config slang::slang IMPORTED_CONFIGURATIONS)
        get_target_property(_slang_lib slang::slang IMPORTED_LOCATION_${_slang_config})
        if(_slang_lib)
            get_filename_component(_slang_dir "${_slang_lib}" DIRECTORY)
        endif()
    endif()

    find_program(AZOTH_RHI_SLANGC
            NAMES slangc
            HINTS ${_slang_dir} ${_slang_dir}/../bin ${slang_prebuilt_SOURCE_DIR}/bin
            DOC "The Slang ahead-of-time compiler used to build the shipped shaders")
    mark_as_advanced(AZOTH_RHI_SLANGC)

    if(NOT AZOTH_RHI_SLANGC)
        message(STATUS "AzothRHI: skipping azoth::rhi-imgui, no slangc was found to compile its shader")
        return()
    endif()
else()
    # One compiler per language, each the one that language is normally built by, and each found the way the
    # shader_languages sample finds it. A target whose compiler is missing loses its shader, not
    # failing the build, the same as a backend that was never enabled: the renderer refuses to create against
    # a backend it holds no binary for.
    find_program(AZOTH_RHI_GLSLC NAMES glslc
            HINTS $ENV{VULKAN_SDK}/Bin $ENV{VULKAN_SDK}/bin
            DOC "The Shaderc GLSL compiler, used when the imgui shader is built from GLSL")
    find_program(AZOTH_RHI_DXC NAMES dxc
            HINTS $ENV{VULKAN_SDK}/Bin $ENV{VULKAN_SDK}/bin
            DOC "The DirectX shader compiler, used when the imgui shader is built from HLSL")
    find_program(AZOTH_RHI_XCRUN NAMES xcrun DOC "Used to reach Apple's metal compiler")
    mark_as_advanced(AZOTH_RHI_GLSLC AZOTH_RHI_DXC AZOTH_RHI_XCRUN)

    set(_available "")
    foreach(_target IN LISTS _shader_targets)
        set(_tool "")
        if(_target STREQUAL "spirv")
            set(_tool ${AZOTH_RHI_GLSLC})
            set(_language GLSL)
        elseif(_target STREQUAL "dxil")
            set(_tool ${AZOTH_RHI_DXC})
            set(_language HLSL)
        elseif(_target STREQUAL "metallib")
            set(_tool ${AZOTH_RHI_XCRUN})
            set(_language MSL)
        endif()

        if(_tool)
            list(APPEND _available ${_target})
        else()
            message(STATUS "AzothRHI: azoth::rhi-imgui has no ${_target}, no compiler was found for its ${_language}")
        endif()
    endforeach()

    set(_shader_targets ${_available})

    if(NOT _shader_targets)
        message(STATUS "AzothRHI: skipping azoth::rhi-imgui, no compiler was found for any of its native shader sources")
        return()
    endif()
endif()

set(_generated ${CMAKE_CURRENT_BINARY_DIR}/generated/azoth/rhi/imgui)
file(MAKE_DIRECTORY ${_generated})

set(_headers "")
set(_entry_definitions "")

# Compiles one stage for one target and embeds it, whichever language and compiler that combination uses.
# The entry point name is recorded, not assumed, because it is a property of the compiler that ran:
# glslc renames every entry point to main, slangc does the same for the two targets that carry one container
# per entry, and dxc and Apple's metal keep whatever the source called it.
function(_azoth_rhi_imgui_shader target stage entry comment)
    set(_binary ${CMAKE_CURRENT_BINARY_DIR}/imgui_${stage}.${target})
    set(_header ${_generated}/imgui_${stage}_${target}.hpp)

    add_custom_command(OUTPUT ${_header}
            COMMAND ${ARGN}
            COMMAND ${CMAKE_COMMAND} -DIN=${_binary} -DOUT=${_header} -DSYMBOL=kImgui_${stage}_${target} -DNAMESPACE=azo::rhi::imgui::shaders
            -P ${CMAKE_SOURCE_DIR}/cmake/azoth_rhi_embed_shader.cmake
            DEPENDS ${_source} ${CMAKE_SOURCE_DIR}/cmake/azoth_rhi_embed_shader.cmake
            COMMENT ${comment}
            VERBATIM)

    string(TOUPPER ${target} _upper)
    string(TOUPPER ${stage} _upper_stage)

    set(_headers ${_headers} ${_header} PARENT_SCOPE)
    set(_entry_definitions ${_entry_definitions} AZOTH_RHI_IMGUI_${_upper}_${_upper_stage}_ENTRY="${entry}" PARENT_SCOPE)
endfunction()

foreach(_target IN LISTS _shader_targets)
    foreach(_stage IN ITEMS vertex fragment)
        set(_binary ${CMAKE_CURRENT_BINARY_DIR}/imgui_${_stage}.${_target})

        if(AZOTH_RHI_IMGUI_SHADER_LANGUAGE STREQUAL "slang")
            set(_source ${CMAKE_CURRENT_SOURCE_DIR}/shaders/imgui.slang)

            # dxil is the one target that will not pick a profile for itself.
            set(_profile "")
            if(_target STREQUAL "dxil")
                set(_profile -profile sm_6_0)
            endif()

            # Slang renames the entry point to main for a container that carries one, which spirv and dxil
            # are and a Metal library is not.
            set(_entry main)
            if(_target STREQUAL "metallib")
                set(_entry ${_stage}Main)
            endif()

            _azoth_rhi_imgui_shader(${_target} ${_stage} ${_entry} "Compiling imgui.slang ${_stage} to ${_target}"
                    ${AZOTH_RHI_SLANGC} ${_source} -target ${_target} ${_profile}
                    -entry ${_stage}Main -stage ${_stage} -o ${_binary})

        elseif(_target STREQUAL "spirv")
            set(_source ${CMAKE_CURRENT_SOURCE_DIR}/shaders/imgui.glsl)

            # Both stages live in the one file behind a define, GLSL having no way to carry two entry points
            # and glslc no way to pick between them. It names the survivor main either way.
            set(_define "")
            if(_stage STREQUAL "vertex")
                set(_define -DAZOTH_RHI_IMGUI_VERTEX_STAGE)
            endif()

            _azoth_rhi_imgui_shader(${_target} ${_stage} main "Compiling imgui.glsl ${_stage} to spirv"
                    ${AZOTH_RHI_GLSLC} -fshader-stage=${_stage} --target-env=vulkan1.2 ${_define} -o ${_binary} ${_source})

        elseif(_target STREQUAL "dxil")
            set(_source ${CMAKE_CURRENT_SOURCE_DIR}/shaders/imgui.hlsl)

            set(_profile vs_6_0)
            if(_stage STREQUAL "fragment")
                set(_profile ps_6_0)
            endif()

            # The vk:: attributes beside each register() are what let the same file reach SPIR-V. Compiling
            # for DXIL warns about every one of them, which is noise, not news.
            _azoth_rhi_imgui_shader(${_target} ${_stage} ${_stage}Main "Compiling imgui.hlsl ${_stage} to dxil"
                    ${AZOTH_RHI_DXC} -T ${_profile} -E ${_stage}Main -Wno-ignored-attributes -Fo ${_binary} ${_source})

        elseif(_target STREQUAL "metallib")
            set(_source ${CMAKE_CURRENT_SOURCE_DIR}/shaders/imgui.metal)

            # Compiled once per stage, so the generated headers are the same shape whatever the language.
            # Apple's compiler has no way to emit one stage, so each library holds both entry points and the
            # name is what selects between them. It costs a few kilobytes twice over, which is cheaper than a
            # second shape for the renderer to know about.
            _azoth_rhi_imgui_shader(${_target} ${_stage} ${_stage}Main "Compiling imgui.metal ${_stage} to metallib"
                    ${AZOTH_RHI_XCRUN} -sdk macosx metal -std=metal3.0 -o ${_binary} ${_source})
        endif()
    endforeach()
endforeach()

add_custom_target(AzothRHI-imgui-shaders DEPENDS ${_headers})

add_library(AzothRHI-imgui STATIC src/renderer.cpp)
add_library(azoth::rhi-imgui ALIAS AzothRHI-imgui)
add_dependencies(AzothRHI-imgui AzothRHI-imgui-shaders)

target_sources(AzothRHI-imgui PRIVATE ${_headers})

# The include directory alone, the way azoth::rhi and azoth::rhi-utils do it. No HEADERS file set, this
# tree having no install rules for one to feed.
target_include_directories(AzothRHI-imgui
        PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        PRIVATE
        ${CMAKE_CURRENT_BINARY_DIR}/generated)

# ImGui is public because the renderer's own header names ImDrawData and ImTextureID.
target_link_libraries(AzothRHI-imgui PUBLIC azoth::rhi ${AZOTH_RHI_IMGUI_TARGET})

target_compile_features(AzothRHI-imgui PUBLIC cxx_std_23)

# Which containers actually got compiled in, so the renderer binds the one the running backend takes and
# refuses the rest without reading past an array that was never built. The entry point names come with
# them, since which one a container carries is the compiler's business and not the renderer's.
foreach(_target IN LISTS _shader_targets)
    string(TOUPPER ${_target} _upper)
    target_compile_definitions(AzothRHI-imgui PRIVATE AZOTH_RHI_IMGUI_HAVE_${_upper})
endforeach()

target_compile_definitions(AzothRHI-imgui PRIVATE ${_entry_definitions})

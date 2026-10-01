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

set(AZOTH_RHI_IMGUI_TARGET "imgui::imgui" CACHE STRING "The Dear ImGui target azoth::rhi-imgui links")
set(AZOTH_RHI_FETCH_IMGUI OFF CACHE BOOL "Fetch Dear ImGui when the host provides none. Off so a consumer never ends up with two")
set(AZOTH_RHI_IMGUI_TAG "v1.92.4" CACHE STRING "Dear ImGui tag fetched when AZOTH_RHI_FETCH_IMGUI is on")
mark_as_advanced(AZOTH_RHI_IMGUI_TARGET AZOTH_RHI_IMGUI_TAG)

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

    add_library(azoth_rhi_imgui_vendored STATIC
            ${imgui_SOURCE_DIR}/imgui.cpp
            ${imgui_SOURCE_DIR}/imgui_draw.cpp
            ${imgui_SOURCE_DIR}/imgui_tables.cpp
            ${imgui_SOURCE_DIR}/imgui_widgets.cpp)

    target_include_directories(azoth_rhi_imgui_vendored SYSTEM PUBLIC ${imgui_SOURCE_DIR})
    add_library(imgui::imgui ALIAS azoth_rhi_imgui_vendored)

    set(AZOTH_RHI_IMGUI_SOURCE_DIR ${imgui_SOURCE_DIR} CACHE INTERNAL "Where a fetched Dear ImGui was unpacked")
endif()

if(NOT TARGET ${AZOTH_RHI_IMGUI_TARGET})
    message(STATUS "AzothRHI: skipping azoth::rhi-imgui, no Dear ImGui target called ${AZOTH_RHI_IMGUI_TARGET}. "
            "Provide one, set AZOTH_RHI_IMGUI_TARGET, or turn on AZOTH_RHI_FETCH_IMGUI.")
    return()
endif()

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
    include(azoth_rhi_shader_tools)
    azoth_rhi_provide_slang_if_needed()

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

function(_azoth_rhi_imgui_shader target stage entry comment)
    set(_binary ${CMAKE_CURRENT_BINARY_DIR}/imgui_${stage}.${target})
    set(_header ${_generated}/imgui_${stage}_${target}.hpp)

    add_custom_command(OUTPUT ${_header}
            COMMAND ${ARGN}
            COMMAND ${CMAKE_COMMAND} -DIN=${_binary} -DOUT=${_header} -DSYMBOL=kImgui_${stage}_${target} -DNAMESPACE=azo::rhi::imgui::shaders
            -P ${AzothRHI_SOURCE_DIR}/cmake/azoth_rhi_embed_shader.cmake
            DEPENDS ${_source} ${AzothRHI_SOURCE_DIR}/cmake/azoth_rhi_embed_shader.cmake
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

            set(_profile "")
            if(_target STREQUAL "dxil")
                set(_profile -profile sm_6_0)
            endif()

            set(_entry main)
            if(_target STREQUAL "metallib")
                set(_entry ${_stage}Main)
            endif()

            _azoth_rhi_imgui_shader(${_target} ${_stage} ${_entry} "Compiling imgui.slang ${_stage} to ${_target}"
                    ${AZOTH_RHI_SLANGC} ${_source} -target ${_target} ${_profile}
                    -entry ${_stage}Main -stage ${_stage} -o ${_binary})

        elseif(_target STREQUAL "spirv")
            set(_source ${CMAKE_CURRENT_SOURCE_DIR}/shaders/imgui.glsl)

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


            _azoth_rhi_imgui_shader(${_target} ${_stage} ${_stage}Main "Compiling imgui.hlsl ${_stage} to dxil"
                    ${AZOTH_RHI_DXC} -T ${_profile} -E ${_stage}Main -Wno-ignored-attributes -Fo ${_binary} ${_source})

        elseif(_target STREQUAL "metallib")
            set(_source ${CMAKE_CURRENT_SOURCE_DIR}/shaders/imgui.metal)


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

target_include_directories(AzothRHI-imgui
        PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        PRIVATE
        ${CMAKE_CURRENT_BINARY_DIR}/generated)

target_link_libraries(AzothRHI-imgui PUBLIC azoth::rhi ${AZOTH_RHI_IMGUI_TARGET})

target_compile_features(AzothRHI-imgui PUBLIC cxx_std_23)

foreach(_target IN LISTS _shader_targets)
    string(TOUPPER ${_target} _upper)
    target_compile_definitions(AzothRHI-imgui PRIVATE AZOTH_RHI_IMGUI_HAVE_${_upper})
endforeach()

target_compile_definitions(AzothRHI-imgui PRIVATE ${_entry_definitions})

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

# The utility target: what left the core when the RHI stopped owning shaders.
#
# It depends on the public headers alone, which the boundary checker proves, not this file asserting.
# Shaders compile here at build time and are embedded, so the library links no shader compiler and the
# target is skipped, not degraded when there is none to compile with.

# slangc and not the Slang library, because this compiles ahead of time and wants the tool. Looked for
# beside a found Slang package first, then on PATH, which is where a Vulkan SDK install puts it.
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
        DOC "The Slang ahead-of-time compiler used to build the utility target's shaders")
mark_as_advanced(AZOTH_RHI_SLANGC)

if(NOT AZOTH_RHI_SLANGC)
    message(STATUS "AzothRHI: skipping azoth::rhi-utils, no slangc was found to compile its shaders")
    return()
endif()

# One target per enabled backend, because each takes a different container and nothing portable spans them.
# metallib needs the Metal toolchain, which only exists on macOS, and that is the reason these are compiled
# here, not checked in.
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
    message(STATUS "AzothRHI: skipping azoth::rhi-utils, no backend it can compile a shader for is enabled")
    return()
endif()

set(_generated ${CMAKE_CURRENT_BINARY_DIR}/generated/azoth/rhi/utils)
file(MAKE_DIRECTORY ${_generated})

set(_source ${CMAKE_CURRENT_SOURCE_DIR}/shaders/resample.slang)
set(_headers "")

foreach(_target IN LISTS _shader_targets)
    set(_binary ${CMAKE_CURRENT_BINARY_DIR}/resample.${_target})
    set(_header ${_generated}/resample_${_target}.hpp)

    # dxil is the one target that will not pick a profile for itself.
    set(_profile "")
    if(_target STREQUAL "dxil")
        set(_profile -profile sm_6_0)
    endif()

    add_custom_command(OUTPUT ${_header}
            COMMAND ${AZOTH_RHI_SLANGC} ${_source} -target ${_target} ${_profile}
            -entry resampleMain -stage compute -o ${_binary}
            COMMAND ${CMAKE_COMMAND} -DIN=${_binary} -DOUT=${_header} -DSYMBOL=kResample_${_target}
            -P ${AzothRHI_SOURCE_DIR}/cmake/azoth_rhi_embed_shader.cmake
            DEPENDS ${_source} ${AzothRHI_SOURCE_DIR}/cmake/azoth_rhi_embed_shader.cmake
            COMMENT "Compiling resample.slang to ${_target}"
            VERBATIM)

    list(APPEND _headers ${_header})
endforeach()

add_custom_target(AzothRHI-utils-shaders DEPENDS ${_headers})

add_library(AzothRHI-utils STATIC src/resampler.cpp)
add_library(azoth::rhi-utils ALIAS AzothRHI-utils)
add_dependencies(AzothRHI-utils AzothRHI-utils-shaders)

target_sources(AzothRHI-utils PRIVATE ${_headers})

target_include_directories(AzothRHI-utils
        PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        PRIVATE
        ${CMAKE_CURRENT_BINARY_DIR}/generated)

# Public headers only. The boundary checker scans this tree, so the layering is proven, not promised.
target_link_libraries(AzothRHI-utils PUBLIC azoth::rhi)

target_compile_features(AzothRHI-utils PUBLIC cxx_std_23)

# Which containers actually got compiled in, so the resampler binds the one the running backend takes and
# refuses the rest without reading past an array that was never built.
foreach(_target IN LISTS _shader_targets)
    string(TOUPPER ${_target} _upper)
    target_compile_definitions(AzothRHI-utils PRIVATE AZOTH_RHI_UTILS_HAVE_${_upper})
endforeach()

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

# Providing the shader tooling the samples and the shader ABI test share.
#
# Split out of azoth_rhi_examples so a test can ask for Slang without pulling in the sample registration
# machinery, which needs a binary directory a test tree has no reason to set.

include_guard(GLOBAL)

# Fetching SDL3 and Slang when the host has neither.
#
# Both are needed only by the samples, and a machine without them used to skip those samples silently.
# That is how the Direct3D 12 shader path went unexercised: the Windows box has neither, so the two
# samples that compile and bind a shader never ran there and the only end-to-end check of the binding
# ABI was macOS. Fetching them makes the samples the same set everywhere and not a set that varies
# by what happens to be installed.
#
# Either can be turned off, for a build that would rather skip a sample than pull a dependency.

set(AZOTH_RHI_FETCH_SDL3 ON CACHE BOOL "Fetch SDL3 for the samples when the host has none")
set(AZOTH_RHI_FETCH_SLANG ON CACHE BOOL "Fetch a prebuilt Slang for the samples when the host has none")

set(AZOTH_RHI_SDL3_TAG "release-3.4.12" CACHE STRING "SDL3 tag fetched when the host has no SDL3")
set(AZOTH_RHI_SLANG_TAG "2026.14.1" CACHE STRING "Slang release fetched when the host has no Slang")
mark_as_advanced(AZOTH_RHI_SDL3_TAG AZOTH_RHI_SLANG_TAG)

macro(azoth_rhi_provide_sdl3)
    azoth_rhi_find_sdl3()

    if(NOT SDL3_FOUND AND AZOTH_RHI_FETCH_SDL3)
        message(STATUS "AzothRHI examples: no SDL3 found, fetching ${AZOTH_RHI_SDL3_TAG}")
        include(FetchContent)

        # Built as a static library so a sample runs from the build tree without a shared library beside it,
        # which is what the ctest smoke runs need.
        set(SDL_SHARED OFF CACHE BOOL "" FORCE)
        set(SDL_STATIC ON CACHE BOOL "" FORCE)
        set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)

        FetchContent_Declare(SDL3
                GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
                GIT_TAG ${AZOTH_RHI_SDL3_TAG}
                GIT_SHALLOW TRUE
        )
        FetchContent_MakeAvailable(SDL3)

        if(TARGET SDL3::SDL3-static AND NOT TARGET SDL3::SDL3)
            add_library(SDL3::SDL3 ALIAS SDL3::SDL3-static)
        endif()
        if(TARGET SDL3::SDL3)
            set(SDL3_FOUND TRUE)
        endif()
    endif()
endmacro()

# The archive and not the source, because building Slang means building its whole compiler stack and
# the samples want the tool, not a copy of it. The release ships a package config, so once it is unpacked
# the lookup is the same find_package a host install would have answered.
macro(azoth_rhi_provide_slang)
    find_package(slang QUIET CONFIG)

    if(NOT slang_FOUND AND AZOTH_RHI_FETCH_SLANG)
        set(_slang_arch "")
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|ARM64)$")
            set(_slang_arch "aarch64")
        elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$")
            set(_slang_arch "x86_64")
        endif()

        set(_slang_os "")
        if(WIN32)
            set(_slang_os "windows")
        elseif(APPLE)
            set(_slang_os "macos")
        elseif(UNIX)
            set(_slang_os "linux")
        endif()

        if(_slang_os STREQUAL "" OR _slang_arch STREQUAL "")
            message(STATUS "AzothRHI examples: no prebuilt Slang for ${CMAKE_SYSTEM_NAME} ${CMAKE_SYSTEM_PROCESSOR}, samples needing it will skip")
        else()
            message(STATUS "AzothRHI examples: no Slang found, fetching ${AZOTH_RHI_SLANG_TAG} for ${_slang_os}-${_slang_arch}")
            include(FetchContent)

            FetchContent_Declare(slang_prebuilt
                    URL https://github.com/shader-slang/slang/releases/download/v${AZOTH_RHI_SLANG_TAG}/slang-${AZOTH_RHI_SLANG_TAG}-${_slang_os}-${_slang_arch}.zip
            )
            FetchContent_MakeAvailable(slang_prebuilt)

            # Two candidate paths because the release archives do not agree on where the config goes: the
            # Windows one puts it at <root>/cmake and the macOS one at <root>/lib/cmake/slang. Both are named
            #, not globbed so a layout change fails loudly here instead of silently skipping a sample.
            #
            # NO_DEFAULT_PATH so this finds the archive just unpacked without falling back to whatever a
            # Vulkan SDK on PATH offers, which is the lookup that already failed above.
            find_package(slang QUIET CONFIG
                    PATHS
                    ${slang_prebuilt_SOURCE_DIR}/cmake
                    ${slang_prebuilt_SOURCE_DIR}/lib/cmake/slang
                    NO_DEFAULT_PATH)

            if(NOT slang_FOUND)
                message(WARNING "AzothRHI examples: fetched Slang ${AZOTH_RHI_SLANG_TAG} but found no package config under "
                        "${slang_prebuilt_SOURCE_DIR}, so the samples needing it will skip")
            endif()
        endif()

        unset(_slang_os)
        unset(_slang_arch)
    endif()

    # Where slang.h actually sits, which the two installs do not agree on: a system install puts the headers
    # in a slang/ directory and the release archive puts them straight in include/. Adding the directory that
    # holds the header lets a sample write <slang.h> and have it resolve either way and not the include
    # spelling depending on how Slang got here.
    if(slang_FOUND AND TARGET slang::slang)
        find_path(AZOTH_RHI_SLANG_INCLUDE_DIR slang.h
                HINTS
                ${slang_prebuilt_SOURCE_DIR}/include
                PATH_SUFFIXES slang)
        if(AZOTH_RHI_SLANG_INCLUDE_DIR)
            target_include_directories(slang::slang INTERFACE ${AZOTH_RHI_SLANG_INCLUDE_DIR})
        endif()
        mark_as_advanced(AZOTH_RHI_SLANG_INCLUDE_DIR)
    endif()
endmacro()

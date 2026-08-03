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

# Registration helper for the samples.
#
# One directory under examples/ per sample, each with its own CMakeLists.txt calling
# azoth_rhi_add_example. examples/CMakeLists.txt finds them by looking, not by listing so adding a
# sample is dropping a directory in beside its siblings and reconfiguring.

include_guard(GLOBAL)

include(azoth_rhi_diagnostics)
include(azoth_rhi_shader_tools)

if(NOT DEFINED AZOTH_RHI_EXAMPLES_BINARY_DIR)
    message(FATAL_ERROR "azoth_rhi_examples: set AZOTH_RHI_EXAMPLES_BINARY_DIR before including this module.")
endif()

# The SDL3 lookup the samples that want a window share, wrapped for one reason.
#
# CMake turns every PATH entry into a package search prefix, so a Vulkan SDK on PATH offers the SDL3
# package config it ships. On macOS that config looks for an SDL3.xcframework the SDK does not ship,
# disqualifies itself and prints an author warning naming a layout nothing here asked for. CMake then
# carries on to a real SDL3, so the only cost is the noise, which is charged to whichever sample
# happens to look first. Developer warnings go off across the call and are put back after, so a
# third-party config cannot editorialise about every configure while -Wdev still works everywhere else.
#
# A macro and not a function because find_package sets variables the caller reads.
macro(azoth_rhi_find_sdl3)
    if(DEFINED CACHE{CMAKE_SUPPRESS_DEVELOPER_WARNINGS})
        set(_azoth_rhi_dev_warnings "$CACHE{CMAKE_SUPPRESS_DEVELOPER_WARNINGS}")
    endif()
    set(CMAKE_SUPPRESS_DEVELOPER_WARNINGS ON CACHE INTERNAL "" FORCE)

    find_package(SDL3 QUIET CONFIG)

    if(DEFINED _azoth_rhi_dev_warnings)
        set(CMAKE_SUPPRESS_DEVELOPER_WARNINGS "${_azoth_rhi_dev_warnings}" CACHE INTERNAL "" FORCE)
        unset(_azoth_rhi_dev_warnings)
    else()
        unset(CMAKE_SUPPRESS_DEVELOPER_WARNINGS CACHE)
    endif()
endmacro()

# glm, which every sample gets by linking examples/lib without looking it up for itself.
#
# Attached there, not here because the framework is already what every sample links, so one link
# line carries both and a sample that wants vectors and matrices writes the include and nothing else.
#
# Unlike SDL3 and Slang there is no degrading gracefully: a sample that cannot see glm does not skip,
# it fails to compile. So a host without glm fetches one, which keeps the samples the same set
# everywhere instead of a set that varies by what happens to be installed. Turning the fetch off on a
# host that has no glm is the one combination that cannot work, and it says so here and not at the
# link.
set(AZOTH_RHI_FETCH_GLM ON CACHE BOOL "Fetch glm for the samples when the host has none")
set(AZOTH_RHI_GLM_TAG "1.0.3" CACHE STRING "glm tag fetched when the host has no glm")
mark_as_advanced(AZOTH_RHI_GLM_TAG)

# Three ways in, cheapest first, and a fetch only when the host has nothing.
#
# A find_package hit, a bare header directory and a fetch leave different variables behind, so every
# step here is judged by whether glm::glm exists and not by a _FOUND of its own.
macro(azoth_rhi_provide_glm)
    # A packaged glm, which is what a package manager install leaves behind.
    find_package(glm QUIET CONFIG)

    # Then the headers on their own, which is how glm arrives when it was unpacked by hand or when the
    # distribution ships the headers without the package config. There is nothing to build or link, so
    # an interface library over the include directory is the same dependency the config would hand back.
    if(NOT TARGET glm::glm)
        find_path(AZOTH_RHI_GLM_INCLUDE_DIR glm/glm.hpp)
        mark_as_advanced(AZOTH_RHI_GLM_INCLUDE_DIR)

        if(AZOTH_RHI_GLM_INCLUDE_DIR)
            message(STATUS "AzothRHI examples: using the glm headers at ${AZOTH_RHI_GLM_INCLUDE_DIR}")
            add_library(azoth_rhi_glm_headers INTERFACE)
            target_include_directories(azoth_rhi_glm_headers SYSTEM INTERFACE ${AZOTH_RHI_GLM_INCLUDE_DIR})
            add_library(glm::glm ALIAS azoth_rhi_glm_headers)
        endif()
    endif()

    if(NOT TARGET glm::glm AND AZOTH_RHI_FETCH_GLM)
        message(STATUS "AzothRHI examples: no glm found, fetching ${AZOTH_RHI_GLM_TAG}")
        include(FetchContent)

        # glm 1.0.1 still declares compatibility with CMake 3.6, which CMake 4 deprecates loudly, twice,
        # on every configure that fetches it. The warning is about glm's own CMakeLists and there is
        # nothing to fix from here, so deprecation warnings go off across the fetch and are put back
        # after, the way the SDL3 lookup above quiets its config. Ours still warn everywhere else.
        if(DEFINED CACHE{CMAKE_WARN_DEPRECATED})
            set(_azoth_rhi_glm_deprecated "$CACHE{CMAKE_WARN_DEPRECATED}")
        endif()
        set(CMAKE_WARN_DEPRECATED OFF CACHE INTERNAL "" FORCE)

        # Its tests and install rules key off GLM_IS_MASTER_PROJECT, which a fetched copy is not, so
        # neither needs turning off from here.
        FetchContent_Declare(glm
                GIT_REPOSITORY https://github.com/g-truc/glm.git
                GIT_TAG ${AZOTH_RHI_GLM_TAG}
                GIT_SHALLOW TRUE
        )
        FetchContent_MakeAvailable(glm)

        if(DEFINED _azoth_rhi_glm_deprecated)
            set(CMAKE_WARN_DEPRECATED "${_azoth_rhi_glm_deprecated}" CACHE INTERNAL "" FORCE)
            unset(_azoth_rhi_glm_deprecated)
        else()
            unset(CMAKE_WARN_DEPRECATED CACHE)
        endif()
    endif()

    if(NOT TARGET glm::glm)
        message(FATAL_ERROR "AzothRHI examples: every sample links glm. Install it, or leave AZOTH_RHI_FETCH_GLM on to fetch it.")
    endif()
endmacro()

# Quill, which is what every sample logs through.
#
# Header only, so there is nothing to build and the fetch is the whole of the fallback. Same three ways
# in as glm above and judged the same way, by whether quill::quill exists and not by a _FOUND of
# its own.
set(AZOTH_RHI_FETCH_QUILL ON CACHE BOOL "Fetch Quill for the samples when the host has none")
set(AZOTH_RHI_QUILL_TAG "v12.1.0" CACHE STRING "Quill tag fetched when the host has none")
mark_as_advanced(AZOTH_RHI_QUILL_TAG)

macro(azoth_rhi_provide_quill)
    find_package(quill QUIET CONFIG)

    if(NOT TARGET quill::quill AND AZOTH_RHI_FETCH_QUILL)
        message(STATUS "AzothRHI examples: no Quill found, fetching ${AZOTH_RHI_QUILL_TAG}")
        include(FetchContent)

        # Its examples and tests are off by default for a consumer, so neither needs turning off here.
        FetchContent_Declare(quill
                GIT_REPOSITORY https://github.com/odygrd/quill.git
                GIT_TAG ${AZOTH_RHI_QUILL_TAG}
                GIT_SHALLOW TRUE
        )
        FetchContent_MakeAvailable(quill)
    endif()

    if(NOT TARGET quill::quill)
        message(FATAL_ERROR "AzothRHI examples: every sample logs through Quill. Install it, or leave AZOTH_RHI_FETCH_QUILL on to fetch it.")
    endif()
endmacro()

# fastgltf and stb_image, which the scene loader reads glTF documents and decodes textures with.
#
# fastgltf builds itself, so it arrives as a target and not a header to wrap. stb ships no
# CMakeLists, being one header, so it is populated and wrapped in an interface library over the
# directory holding it. Unlike SDL3 and Slang there is no skipping: a sample never asks for these,
# examples/lib does, and every sample links that.
set(AZOTH_RHI_FASTGLTF_TAG "v0.9.0" CACHE STRING "fastgltf version the scene loader is built against")
# stb cuts no releases, so this is the commit the decoder was written against.
set(AZOTH_RHI_STB_TAG "31c1ad37456438565541f4919958214b6e762fb4" CACHE STRING "stb commit the image decoder is built against")
mark_as_advanced(AZOTH_RHI_FASTGLTF_TAG AZOTH_RHI_STB_TAG)

macro(azoth_rhi_provide_asset_libraries)
    include(FetchContent)

    FetchContent_Declare(fastgltf
            GIT_REPOSITORY https://github.com/spnda/fastgltf.git
            GIT_TAG ${AZOTH_RHI_FASTGLTF_TAG}
            GIT_SHALLOW TRUE
    )
    FetchContent_Declare(stb
            GIT_REPOSITORY https://github.com/nothings/stb.git
            GIT_TAG ${AZOTH_RHI_STB_TAG}
    )
    FetchContent_MakeAvailable(fastgltf stb)

    # SYSTEM so the warning levels this build turns up do not apply to a header nobody here can fix.
    if(NOT TARGET azoth_rhi_stb)
        add_library(azoth_rhi_stb INTERFACE)
        target_include_directories(azoth_rhi_stb SYSTEM INTERFACE ${stb_SOURCE_DIR})
    endif()
endmacro()

# Copies what a sample reads at run time to assets/<sample>/ beside the executable.
#
# TARGET_FILE_DIR and not the output directory because a multi-config generator puts the
# executable one level further down, in a directory named for the configuration, and the assets have
# to follow it there.
#
# Each copy is a build output depending on its source, not a POST_BUILD step. POST_BUILD runs only
# when the target relinks, so editing a shader and rebuilding left the old one staged and the sample
# went on reading it, which reads as a shader change that did nothing.
#
# Per sample and not one shared assets/ so two samples can carry a file of the same name, and so
# deleting a sample takes its data with it.
function(azoth_rhi_stage_example_assets name target)
    set(_destination $<TARGET_FILE_DIR:${target}>/assets/${name})
    set(_staged)

    foreach(_entry IN LISTS ARGN)
        # <source>:<destination> when the name beside the executable differs from the name on disk.
        #
        # Two characters before the colon, not one, so a Windows drive letter is not read as a source. A
        # bare E:/dir/file.gltf has exactly one character ahead of its colon and matched the one-character
        # form, which left the source as E and the destination as the rest of the path.
        if(_entry MATCHES "^(..[^:]*):(.+)$")
            set(_source ${CMAKE_MATCH_1})
            set(_relative ${CMAKE_MATCH_2})
        else()
            set(_source ${_entry})
            get_filename_component(_relative ${_entry} NAME)
        endif()

        if(NOT IS_ABSOLUTE ${_source})
            set(_source ${CMAKE_CURRENT_SOURCE_DIR}/${_source})
        endif()

        if(NOT EXISTS ${_source})
            message(FATAL_ERROR "azoth_rhi_add_example(${name}): the asset ${_source} does not exist.")
        endif()

        # A stamp in the build tree and not the staged file itself, because the destination is only
        # known through TARGET_FILE_DIR and a generator expression naming a target cannot be an OUTPUT.
        # It can be a COMMAND, which is where the copy below spells it.
        string(REPLACE "/" "-" _stamp_name ${_relative})
        set(_stamp ${CMAKE_CURRENT_BINARY_DIR}/${target}-${_stamp_name}.stamp)

        if(IS_DIRECTORY ${_source})
            # CONFIGURE_DEPENDS so adding or deleting a file in the directory re-runs the configure and
            # lands in the list below and never waiting for the next unrelated CMake edit.
            file(GLOB_RECURSE _files CONFIGURE_DEPENDS ${_source}/*)

            # Cleared before the copy so a file deleted from the source stops being staged. Copying over
            # the top leaves it behind, and a sample that still finds it goes on reading it.
            add_custom_command(OUTPUT ${_stamp}
                    COMMAND ${CMAKE_COMMAND} -E rm -rf ${_destination}/${_relative}
                    COMMAND ${CMAKE_COMMAND} -E copy_directory ${_source} ${_destination}/${_relative}
                    COMMAND ${CMAKE_COMMAND} -E touch ${_stamp}
                    DEPENDS ${_files}
                    COMMENT "Staging ${_relative} for ${target}"
                    VERBATIM)
        else()
            add_custom_command(OUTPUT ${_stamp}
                    COMMAND ${CMAKE_COMMAND} -E make_directory ${_destination}
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_source} ${_destination}/${_relative}
                    COMMAND ${CMAKE_COMMAND} -E touch ${_stamp}
                    DEPENDS ${_source}
                    COMMENT "Staging ${_relative} for ${target}"
                    VERBATIM)
        endif()

        list(APPEND _staged ${_stamp})
    endforeach()

    add_custom_target(${target}-assets DEPENDS ${_staged})
    add_dependencies(${target} ${target}-assets)
endfunction()

# One executable per sample, named rhi_<name>.
#
# Samples are built by the test suite and not run by it. A sample is written to be read and to be
# run by hand, so it prints, it waits on a window, it picks a backend from a command line. None of
# that is a test, and a suite that runs them ends up asserting on the wording of an explanation. The
# build is what catches a sample that stopped compiling, which is the thing about a sample that
# actually rots. What the RHI does is covered by tests/, against the RHI, not against prose.
#
# ASSETS names files and directories the sample reads at run time. Each is copied under an assets/
# directory beside the executable after every build, which is where fw::util::AssetPath looks, so a
# sample names its data and never a path out of the build tree. An entry may be a source-relative
# path, an absolute path (which is what a file downloaded at configure time is), or a
# <source>:<destination> pair when the name beside the executable should differ from the name on
# disk. A directory is copied whole, keeping its shape underneath.
function(azoth_rhi_add_example name)
    cmake_parse_arguments(ARG "" "" "SOURCES;LINK_LIBRARIES;ASSETS" ${ARGN})

    if(NOT ARG_SOURCES)
        message(FATAL_ERROR "azoth_rhi_add_example(${name}): SOURCES is required.")
    endif()

    set(_target rhi_${name})

    add_executable(${_target} ${ARG_SOURCES})

    # examples/lib is linked by every sample, not named by each one, so what is put there is
    # reachable from all of them. A sample that uses none of it pays nothing: the linker drops the objects
    # it never references out of the archive.
    #
    # It carries azoth::rhi and glm publicly and is the only thing named here for that reason. Naming the
    # library again beside it puts the same archive on the link line twice, which the Apple linker warns
    # about, and the test suites reach azoth::rhi the same way through azoth::rhi-test.
    #
    # Last, not first, because a caller's library may depend on it too, the sdl3 one being exactly
    # that. Named first, CMake has to repeat the archive after that library to keep a static link resolvable,
    # and the same warning arrives by the other road. Named last, the one mention already sits where both
    # want it.
    target_link_libraries(${_target} PRIVATE ${ARG_LINK_LIBRARIES} azoth::rhi-examples-lib)

    # Every sample binary lands beside its siblings and not in its own source-shaped directory
    # so running one is a name and not a path to remember.
    set_target_properties(${_target} PROPERTIES
            FOLDER examples
            RUNTIME_OUTPUT_DIRECTORY ${AZOTH_RHI_EXAMPLES_BINARY_DIR})

    azoth_rhi_apply_diagnostic_scheme(${_target})

    if(ARG_ASSETS)
        azoth_rhi_stage_example_assets(${name} ${_target} ${ARG_ASSETS})
    endif()

    # A sample linking something that ships as a shared library, Slang being the one here, needs it beside the
    # executable on Windows or the process dies at load with no output at all. TARGET_RUNTIME_DLLS resolves
    # whatever the imported targets actually need, which is nothing on the platforms that use an rpath.
    #
    # Here and not in the one sample that first needed it: every sample is launched the same way, and a
    # smoke test that cannot start reports as a failure with an exit code and no diagnostic.
    # Most samples link nothing shared and the list comes out empty, which copy_if_different answers by printing
    # its usage and failing. The command becomes a no-op in that case and not the copy being skipped, since
    # whether the list is empty is only known when the build runs.
    if(WIN32)
        add_custom_command(TARGET ${_target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E
                        $<IF:$<BOOL:$<TARGET_RUNTIME_DLLS:${_target}>>,copy_if_different,true>
                        $<TARGET_RUNTIME_DLLS:${_target}> $<TARGET_FILE_DIR:${_target}>
                COMMAND_EXPAND_LISTS)
    endif()

    set_property(GLOBAL APPEND PROPERTY AZOTH_RHI_EXAMPLE_TARGETS ${_target})
endfunction()

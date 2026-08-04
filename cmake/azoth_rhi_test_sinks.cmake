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

# The profiler clients this repository fetches to test its own sinks with.
#
# Neither sink is reachable from a plain configure. Tracy is found, not fetched and PIX links
# a WinPixEventRuntime that is yours to supply so left alone both are compiled by nothing here and
# their tests are compiled out. These options stand in for the host: turning one on produces the
# target the library is looking for, which is what a CI job needs to build the sink and run what
# covers it.
#
# They are not a way to get a profiler into a build of your own. Add the client to your own build
# instead, which is the arrangement these two are imitating.

include_guard(GLOBAL)

include(FetchContent)

option(AZOTH_RHI_TESTS_FETCH_TRACY "Fetch a Tracy client for this build to link, standing in for one a host supplies" OFF)
option(AZOTH_RHI_TESTS_FETCH_PIX "Fetch WinPixEventRuntime for this build to link, standing in for one a host supplies" OFF)

set(AZOTH_RHI_TRACY_TAG "v0.13.1" CACHE STRING "Tracy version AZOTH_RHI_TESTS_FETCH_TRACY brings in")
# Named like every other pinned dependency, not written into the URL twice. The hash below pins the
# contents, so this decides which package is asked for and the hash decides whether it was the right one.
set(AZOTH_RHI_WINPIX_VERSION "1.0.240308001" CACHE STRING "WinPixEventRuntime version AZOTH_RHI_TESTS_FETCH_PIX brings in")
mark_as_advanced(AZOTH_RHI_TRACY_TAG AZOTH_RHI_WINPIX_VERSION)

# A macro and not a function, for the reason the dependency fetches are: FetchContent sets
# <name>_SOURCE_DIR in the calling scope and the imported target below is built out of those paths.
#
# Called before the profiling detection, since the point is for that detection to find these the same
# way it would find a host's.
macro(azoth_rhi_fetch_test_sinks)
    if(AZOTH_RHI_TESTS_FETCH_TRACY)
        # On demand because a client with no server listening keeps every zone it records for the
        # life of the process and a suite records a great many. Fibers on, since the fiber half of
        # the sink compiles to nothing without them and would go untested by the job that exists to
        # test it.
        set(TRACY_ENABLE ON CACHE BOOL "" FORCE)
        set(TRACY_ON_DEMAND ON CACHE BOOL "" FORCE)
        set(TRACY_FIBERS ON CACHE BOOL "" FORCE)

        FetchContent_Declare(tracy
                GIT_REPOSITORY https://github.com/wolfpld/tracy.git
                GIT_TAG ${AZOTH_RHI_TRACY_TAG}
                GIT_SHALLOW TRUE
        )
        FetchContent_MakeAvailable(tracy)
    endif()

    if(AZOTH_RHI_TESTS_FETCH_PIX)
        if(NOT WIN32)
            message(FATAL_ERROR
                    "AzothRHI: AZOTH_RHI_TESTS_FETCH_PIX is on, but the package carries Windows binaries only.")
        endif()

        # Read from the cache and not from the option, which is declared later. A job that fetches
        # the runtime and forgets to turn the sink on would otherwise build and test nothing new.
        if(NOT AZOTH_RHI_PIX)
            message(FATAL_ERROR
                    "AzothRHI: AZOTH_RHI_TESTS_FETCH_PIX is on but AZOTH_RHI_PIX is off, so the runtime would be "
                    "fetched and never linked. Set AZOTH_RHI_PIX=ON as well.")
        endif()

        FetchContent_Declare(winpix
                URL https://api.nuget.org/v3-flatcontainer/winpixeventruntime/${AZOTH_RHI_WINPIX_VERSION}/winpixeventruntime.${AZOTH_RHI_WINPIX_VERSION}.nupkg
                URL_HASH SHA256=726acc93d6968e2146261a1e415521747d50ad69894c2b42b5d0d4c29fd66ec4
                DOWNLOAD_NO_PROGRESS TRUE
        )
        FetchContent_MakeAvailable(winpix)

        string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" _winpix_proc)
        if(_winpix_proc MATCHES "arm64|aarch64")
            set(_winpix_arch ARM64)
        else()
            set(_winpix_arch x64)
        endif()

        # Named for what AZOTH_RHI_PIX_TARGET already defaults to so nothing has to be pointed at it.
        add_library(winpix SHARED IMPORTED GLOBAL)
        set_target_properties(winpix PROPERTIES
                IMPORTED_IMPLIB "${winpix_SOURCE_DIR}/bin/${_winpix_arch}/WinPixEventRuntime.lib"
                IMPORTED_LOCATION "${winpix_SOURCE_DIR}/bin/${_winpix_arch}/WinPixEventRuntime.dll"
        )
        target_include_directories(winpix SYSTEM INTERFACE "${winpix_SOURCE_DIR}/Include/WinPixEventRuntime")
    endif()
endmacro()

# Windows resolves a DLL from the directory the executable is in. An imported sink sits wherever it was
# fetched and a shared AzothRHI sits in the library's own output directory, so neither is there. Without
# this a suite cannot start, which PRE_TEST discovery hits before any case runs and reports as a CMake
# error out of DiscoverTests rather than as a test failure.
#
# Elsewhere the loader finds both through the rpath a target already carries, so this is Windows only.
function(azoth_rhi_stage_test_runtime target)
    if(NOT WIN32)
        return()
    endif()

    if(AZOTH_RHI_TESTS_FETCH_PIX)
        add_custom_command(TARGET ${target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:winpix>" "$<TARGET_FILE_DIR:${target}>"
                VERBATIM
        )
    endif()

    if(BUILD_SHARED_LIBS)
        add_custom_command(TARGET ${target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:AzothRHI>" "$<TARGET_FILE_DIR:${target}>"
                VERBATIM
        )
    endif()
endfunction()

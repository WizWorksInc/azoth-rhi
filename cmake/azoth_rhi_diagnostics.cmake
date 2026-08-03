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

# The run-time half of Xcode's scheme Diagnostics panel, as configure flags.
#
# None of these is compiled in. Each is an environment variable the process reads on the way up, which
# is why Xcode can offer them without a rebuild and why the sanitizers next door cannot live here. A
# build tree configured with one of these on carries it three ways: ctest sets it for every test, the
# Xcode generator writes it into the scheme so the panel in Xcode reads back what CMake was told, and
# a launcher script beside the build applies it to a sample run by hand.
#
# The launcher exists because a cache option cannot reach into a bare ./examples/rhi_deccer_cubes. It
# is the one route that has to be spelled at the point of running, so it is generated and not
# written down in a wiki nobody reloads.
#
# Every mechanism here is Apple's. Turning one on anywhere else is refused, not ignored, since a
# checkbox that silently does nothing is worse than one that is not offered.

include_guard(GLOBAL)

# Runtime API checking.
option(AZOTH_RHI_DIAGNOSTIC_MAIN_THREAD_CHECKER "Load the Main Thread Checker, which reports AppKit and UIKit calls made off the main thread" OFF)

# Memory management. Guard Malloc and the scribble and edge flags are different tools for the same
# question and are meant to be turned on one at a time, since Guard Malloc replaces the allocator the
# other two configure.
option(AZOTH_RHI_DIAGNOSTIC_GUARD_MALLOC "Replace the allocator with libgmalloc, which faults on the access that overruns instead of later" OFF)
option(AZOTH_RHI_DIAGNOSTIC_MALLOC_SCRIBBLE "Fill freshly allocated and freed memory, so reading either reads garbage and not the last value" OFF)
option(AZOTH_RHI_DIAGNOSTIC_MALLOC_GUARD_EDGES "Add a guard page before and after each large block" OFF)
option(AZOTH_RHI_DIAGNOSTIC_ZOMBIE_OBJECTS "Turn released Objective-C objects into zombies that name themselves when messaged" OFF)
option(AZOTH_RHI_DIAGNOSTIC_MALLOC_STACK_LOGGING "Record an allocation stack per live block, which is what the leaks and heap tools read" OFF)

# Metal. API validation is the one that answers whether the backends are using Metal correctly, so it
# is the one worth reaching for first.
option(AZOTH_RHI_DIAGNOSTIC_METAL_API_VALIDATION "Enable the Metal validation layer" OFF)
option(AZOTH_RHI_DIAGNOSTIC_METAL_SHADER_VALIDATION "Instrument shader memory access and report out of bounds reads and writes" OFF)
option(AZOTH_RHI_DIAGNOSTIC_METAL_HUD "Show the Metal performance HUD over the sample's window" OFF)

set(AZOTH_RHI_DIAGNOSTIC_OPTIONS
        AZOTH_RHI_DIAGNOSTIC_MAIN_THREAD_CHECKER
        AZOTH_RHI_DIAGNOSTIC_GUARD_MALLOC
        AZOTH_RHI_DIAGNOSTIC_MALLOC_SCRIBBLE
        AZOTH_RHI_DIAGNOSTIC_MALLOC_GUARD_EDGES
        AZOTH_RHI_DIAGNOSTIC_ZOMBIE_OBJECTS
        AZOTH_RHI_DIAGNOSTIC_MALLOC_STACK_LOGGING
        AZOTH_RHI_DIAGNOSTIC_METAL_API_VALIDATION
        AZOTH_RHI_DIAGNOSTIC_METAL_SHADER_VALIDATION
        AZOTH_RHI_DIAGNOSTIC_METAL_HUD)

# The library a checker is inserted through, or a fatal error naming what is missing.
#
# Looked up, not assumed. The Main Thread Checker ships inside Xcode and not with the Command
# Line Tools, so a machine with only the latter can configure this on and get a process that dies at
# load with nothing said about why.
function(_azoth_rhi_diagnostic_insert_library option path out_var)
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR
                "AzothRHI: ${option} is on, but ${path} is not on this machine. "
                "The Main Thread Checker ships with Xcode, not with the Command Line Tools.")
    endif()

    set(${out_var} "${path}" PARENT_SCOPE)
endfunction()

# Reads the environment the enabled diagnostics amount to, as a list of NAME=VALUE.
#
# Empty when none is on, which is what lets every caller apply it unconditionally.
function(azoth_rhi_diagnostic_environment out_var)
    get_property(_environment GLOBAL PROPERTY AZOTH_RHI_DIAGNOSTIC_ENVIRONMENT)
    set(${out_var} "${_environment}" PARENT_SCOPE)
endfunction()

# Settles what the enabled diagnostics mean, and writes the launcher that applies them by hand.
#
# Called once, from the top level, because the environment is a property of the build tree and not of
# any one target.
function(azoth_rhi_apply_diagnostics)
    set(_enabled)
    foreach(_option IN LISTS AZOTH_RHI_DIAGNOSTIC_OPTIONS)
        if(${_option})
            list(APPEND _enabled ${_option})
        endif()
    endforeach()

    if(_enabled AND NOT APPLE)
        string(REPLACE ";" ", " _names "${_enabled}")
        message(FATAL_ERROR
                "AzothRHI: ${_names} name Apple's own diagnostics, which exist only on macOS. Turn them off to configure here.")
    endif()

    set(_environment)
    set(_inserted)

    if(AZOTH_RHI_DIAGNOSTIC_MAIN_THREAD_CHECKER)
        execute_process(COMMAND xcode-select -p
                OUTPUT_VARIABLE _developer_dir
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET)
        _azoth_rhi_diagnostic_insert_library(AZOTH_RHI_DIAGNOSTIC_MAIN_THREAD_CHECKER
                "${_developer_dir}/usr/lib/libMainThreadChecker.dylib" _checker)
        list(APPEND _inserted "${_checker}")

        # The checker takes itself back out of the environment for anything it launches. The launcher
        # below is exactly that, an exec through a shell, so without this the sample it starts runs
        # unchecked and looks like a clean run.
        list(APPEND _environment "MTC_RESET_INSERT_LIBRARIES=0")
    endif()

    if(AZOTH_RHI_DIAGNOSTIC_GUARD_MALLOC)
        _azoth_rhi_diagnostic_insert_library(AZOTH_RHI_DIAGNOSTIC_GUARD_MALLOC "/usr/lib/libgmalloc.dylib" _gmalloc)
        list(APPEND _inserted "${_gmalloc}")

        if(AZOTH_RHI_DIAGNOSTIC_MALLOC_SCRIBBLE OR AZOTH_RHI_DIAGNOSTIC_MALLOC_GUARD_EDGES)
            message(WARNING
                    "AzothRHI: AZOTH_RHI_DIAGNOSTIC_GUARD_MALLOC replaces the allocator that MallocScribble and "
                    "MallocGuardEdges configure, so those two do nothing while it is on.")
        endif()
    endif()

    if(AZOTH_RHI_DIAGNOSTIC_MALLOC_SCRIBBLE)
        list(APPEND _environment "MallocScribble=1")
    endif()
    if(AZOTH_RHI_DIAGNOSTIC_MALLOC_GUARD_EDGES)
        list(APPEND _environment "MallocGuardEdges=1")
    endif()
    if(AZOTH_RHI_DIAGNOSTIC_ZOMBIE_OBJECTS)
        list(APPEND _environment "NSZombieEnabled=YES")
    endif()
    if(AZOTH_RHI_DIAGNOSTIC_MALLOC_STACK_LOGGING)
        list(APPEND _environment "MallocStackLogging=1")
    endif()

    if(AZOTH_RHI_DIAGNOSTIC_METAL_API_VALIDATION)
        # Stopping on the first broken rule, since a validation run that carries on prints the same
        # complaint every frame and buries the first one.
        list(APPEND _environment "MTL_DEBUG_LAYER=1" "MTL_DEBUG_LAYER_ERROR_MODE=assert")
    endif()
    if(AZOTH_RHI_DIAGNOSTIC_METAL_SHADER_VALIDATION)
        list(APPEND _environment "MTL_SHADER_VALIDATION=1")
    endif()
    if(AZOTH_RHI_DIAGNOSTIC_METAL_HUD)
        list(APPEND _environment "MTL_HUD_ENABLED=1")
    endif()

    # Colon separated and appended to whatever the caller already had, since two of these insert a
    # library and a build with both on has to load both.
    if(_inserted)
        string(JOIN ":" _libraries ${_inserted})
        list(APPEND _environment "DYLD_INSERT_LIBRARIES=${_libraries}")
    endif()

    set_property(GLOBAL PROPERTY AZOTH_RHI_DIAGNOSTIC_ENVIRONMENT "${_environment}")

    if(_enabled)
        string(REPLACE ";" ", " _names "${_enabled}")
        string(REPLACE "AZOTH_RHI_DIAGNOSTIC_" "" _names "${_names}")
        message(STATUS "AzothRHI: diagnostics on: ${_names}")
    endif()

    _azoth_rhi_write_diagnostic_launcher("${_environment}")
endfunction()

# The script that puts the configured environment in front of a command run by hand.
#
# Written for every build tree and not only for one with something turned on, so the way a sample is
# launched does not change when a diagnostic is toggled. With nothing on it is an exec and nothing else.
function(_azoth_rhi_write_diagnostic_launcher environment)
    if(WIN32)
        return()
    endif()

    set(_path "${CMAKE_BINARY_DIR}/azoth-diagnostics")
    set(_body "#!/bin/sh\n")
    string(APPEND _body "# Generated by CMake from the AZOTH_RHI_DIAGNOSTIC_* options. Reconfigure to change it.\n")
    string(APPEND _body "#\n")
    string(APPEND _body "# usage: azoth-diagnostics <command> [args...]\n\n")
    string(APPEND _body "if [ $# -eq 0 ]; then\n")
    string(APPEND _body "    echo \"usage: $0 <command> [args...]\" >&2\n")

    if(environment)
        string(APPEND _body "    echo \"applies:\" >&2\n")
        foreach(_pair IN LISTS environment)
            string(APPEND _body "    echo \"    ${_pair}\" >&2\n")
        endforeach()
    else()
        string(APPEND _body "    echo \"no diagnostic is on in this build tree, so this only runs the command\" >&2\n")
    endif()

    string(APPEND _body "    exit 2\n")
    string(APPEND _body "fi\n\n")

    # Split on the first = only, since a value may carry one and a path list does.
    foreach(_pair IN LISTS environment)
        string(REGEX MATCH "^([^=]+)=(.*)$" _matched "${_pair}")
        string(APPEND _body "export ${CMAKE_MATCH_1}=\"${CMAKE_MATCH_2}\"\n")
    endforeach()

    string(APPEND _body "\nexec \"$@\"\n")

    file(WRITE "${_path}" "${_body}")
    file(CHMOD "${_path}" PERMISSIONS
            OWNER_READ OWNER_WRITE OWNER_EXECUTE
            GROUP_READ GROUP_EXECUTE
            WORLD_READ WORLD_EXECUTE)
endfunction()

# Writes the panel back into the Xcode scheme for one target.
#
# So that opening the Xcode build tree shows the same boxes ticked that this build tree was configured
# with, and not two places to keep in step by hand. Set on every target and not only under the
# Xcode generator: the properties are inert everywhere else, and gating them would mean the Xcode tree
# only agreed when it was also the tree someone happened to configure last.
#
# The sanitizer rows come from AZOTH_RHI_SANITIZER, which is a compile flag, so a scheme cannot turn
# one on for a build that was not compiled for it. They are written here to describe the build, not to
# change it.
function(azoth_rhi_apply_diagnostic_scheme target)
    # Plain ON and OFF, settled here. These properties are read as written when the scheme is generated
    # and a generator expression in one arrives in the scheme file verbatim.
    string(TOUPPER "${AZOTH_RHI_SANITIZER}" _sanitizer)

    set(_address OFF)
    set(_thread OFF)
    set(_undefined OFF)
    if(_sanitizer STREQUAL "ADDRESS")
        set(_address ON)
    elseif(_sanitizer STREQUAL "THREAD")
        set(_thread ON)
    elseif(_sanitizer STREQUAL "UNDEFINED")
        set(_undefined ON)
    endif()

    # Xcode's own checkbox is the inverse of this one and is ticked by default, so it is written either
    # way and not only when the diagnostic is asked for.
    set(_disable_main_thread_checker ON)
    if(AZOTH_RHI_DIAGNOSTIC_MAIN_THREAD_CHECKER)
        set(_disable_main_thread_checker OFF)
    endif()

    set_target_properties(${target} PROPERTIES
            XCODE_SCHEME_ADDRESS_SANITIZER ${_address}
            XCODE_SCHEME_ADDRESS_SANITIZER_USE_AFTER_RETURN ${AZOTH_RHI_SANITIZER_USE_AFTER_RETURN}
            XCODE_SCHEME_THREAD_SANITIZER ${_thread}
            XCODE_SCHEME_UNDEFINED_BEHAVIOUR_SANITIZER ${_undefined}
            XCODE_SCHEME_DISABLE_MAIN_THREAD_CHECKER ${_disable_main_thread_checker}
            XCODE_SCHEME_GUARD_MALLOC ${AZOTH_RHI_DIAGNOSTIC_GUARD_MALLOC}
            XCODE_SCHEME_MALLOC_SCRIBBLE ${AZOTH_RHI_DIAGNOSTIC_MALLOC_SCRIBBLE}
            XCODE_SCHEME_MALLOC_GUARD_EDGES ${AZOTH_RHI_DIAGNOSTIC_MALLOC_GUARD_EDGES}
            XCODE_SCHEME_MALLOC_STACK ${AZOTH_RHI_DIAGNOSTIC_MALLOC_STACK_LOGGING}
            XCODE_SCHEME_ZOMBIE_OBJECTS ${AZOTH_RHI_DIAGNOSTIC_ZOMBIE_OBJECTS}
            XCODE_SCHEME_ENABLE_GPU_API_VALIDATION ${AZOTH_RHI_DIAGNOSTIC_METAL_API_VALIDATION}
            XCODE_SCHEME_ENABLE_GPU_SHADER_VALIDATION ${AZOTH_RHI_DIAGNOSTIC_METAL_SHADER_VALIDATION})

    # The HUD has no checkbox of its own in the scheme, so it arrives the way Xcode's own panel would
    # pass it, as a scheme environment entry.
    if(AZOTH_RHI_DIAGNOSTIC_METAL_HUD)
        set_target_properties(${target} PROPERTIES XCODE_SCHEME_ENVIRONMENT "MTL_HUD_ENABLED=1")
    endif()
endfunction()

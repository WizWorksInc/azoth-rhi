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

# PIX is ON or OFF and that decides one thing: whether the sink compiles. Nothing here fetches
# WinPixEventRuntime, since its DLL has to be staged beside the executable and that is the host's
# build to do, not ours so an ON build links the copy you already have.
#
# Tracy is not even that. Two Tracy clients in one process do not work so the client is the
# host's to build and ours only to find: the sink is compiled when the target named by
# AZOTH_RHI_TRACY_TARGET is there and was built with TRACY_ENABLE, which Tracy puts on that target's
# interface. Nothing here can turn Tracy on, only notice that the host did.

# Whether a target carries one of Tracy's switches on its interface, which is where a Tracy client
# advertises what it was built with. Read through the alias, since Tracy::TracyClient is one.
function(_azoth_rhi_target_defines target definition out)
    set(${out} FALSE PARENT_SCOPE)

    if(NOT TARGET ${target})
        return()
    endif()

    get_target_property(_aliased ${target} ALIASED_TARGET)
    if(_aliased)
        set(target ${_aliased})
    endif()

    get_target_property(_definitions ${target} INTERFACE_COMPILE_DEFINITIONS)
    if(_definitions MATCHES "(^|;)${definition}(=[^;]*)?(;|$)")
        set(${out} TRUE PARENT_SCOPE)
    endif()
endfunction()

macro(azoth_rhi_declare_profiling)
    option(AZOTH_RHI_PIX "Compile the PIX event sink, linking the WinPixEventRuntime you supply" OFF)

    # Tracy has a canonical target name and PIX has none so the PIX default is a name of ours that a
    # build turning PIX on almost always replaces.
    set(AZOTH_RHI_TRACY_TARGET "Tracy::TracyClient" CACHE STRING "Target providing the Tracy client, if this build has one")
    set(AZOTH_RHI_PIX_TARGET "winpix" CACHE STRING "Target providing WinPixEventRuntime and pix3.h when PIX is on")

    # A host adds Tracy before it adds us so this is the whole of the decision. Nothing between
    # here and the library can produce the target later.
    _azoth_rhi_target_defines(${AZOTH_RHI_TRACY_TARGET} TRACY_ENABLE AZOTH_RHI_TRACY_ENABLED)

    if(AZOTH_RHI_PIX AND NOT AZOTH_RHI_BACKEND_D3D12)
        message(FATAL_ERROR "AzothRHI: PIX events come from the bundled Direct3D 12 backend, which is not being built.")
    endif()
endmacro()

# Checked after the fetches. A PIX build missing its target is reported here by name, instead of
# surfacing later as an unresolved link.
function(azoth_rhi_validate_profiling_targets)
    if(AZOTH_RHI_PIX AND NOT TARGET ${AZOTH_RHI_PIX_TARGET})
        message(FATAL_ERROR
                "AzothRHI: AZOTH_RHI_PIX is on and needs the target ${AZOTH_RHI_PIX_TARGET}, which does not exist. "
                "It has to supply pix3.h on the include path and link WinPixEventRuntime. Point "
                "AZOTH_RHI_PIX_TARGET at the one you have.")
    endif()
endfunction()

function(azoth_rhi_report_profiling)
    message(STATUS "AzothRHI profiling:")
    if(AZOTH_RHI_ENABLE_PROFILING)
        message(STATUS "  profiling: on")
    else()
        message(STATUS "  profiling: off")
    endif()

    # Fibers are the client's answer too and without them a zone spanning a cooperative wait lands
    # on the worker that resumed it without staying with its task.
    if(AZOTH_RHI_TRACY_ENABLED)
        _azoth_rhi_target_defines(${AZOTH_RHI_TRACY_TARGET} TRACY_FIBERS _fibers)
        if(_fibers)
            message(STATUS "  TRACY: linking ${AZOTH_RHI_TRACY_TARGET}, fibers on")
        else()
            message(STATUS "  TRACY: linking ${AZOTH_RHI_TRACY_TARGET}, fibers off")
        endif()
    elseif(TARGET ${AZOTH_RHI_TRACY_TARGET})
        message(STATUS "  TRACY: off, ${AZOTH_RHI_TRACY_TARGET} was built without TRACY_ENABLE")
    else()
        message(STATUS "  TRACY: off")
    endif()

    if(AZOTH_RHI_PIX)
        message(STATUS "  PIX: linking ${AZOTH_RHI_PIX_TARGET}")
    else()
        message(STATUS "  PIX: off")
    endif()
endfunction()

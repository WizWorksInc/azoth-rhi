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

# Builds a loadable backend, which is a shared library with one exported symbol and a predictable name.
#
# MODULE, not SHARED, which is the distinction CMake draws between something to link against and
# something to dlopen. A module library gets no import library on Windows and no version symlinks
# elsewhere because nothing is ever supposed to link it.
#
#     azoth_rhi_add_backend_module(my_backend SOURCES my_backend.cpp)
function(azoth_rhi_add_backend_module target)
    cmake_parse_arguments(ARG "" "OUTPUT_NAME" "SOURCES;LINK_LIBRARIES" ${ARGN})

    if(NOT ARG_SOURCES)
        message(FATAL_ERROR "azoth_rhi_add_backend_module(${target}): SOURCES is required.")
    endif()

    add_library(${target} MODULE ${ARG_SOURCES})
    target_link_libraries(${target} PRIVATE azoth::rhi-module-sdk ${ARG_LINK_LIBRARIES})
    target_compile_features(${target} PRIVATE cxx_std_23)

    if(ARG_OUTPUT_NAME)
        set_target_properties(${target} PROPERTIES OUTPUT_NAME "${ARG_OUTPUT_NAME}")
    endif()

    # No lib prefix so the file a host is told to load is the name it was given and not one this
    # decorated. A host loading by an absolute path it was handed does not care and one building the
    # path from a backend's name does.
    set_target_properties(${target} PROPERTIES PREFIX "")

    # Hidden by default so the entry point is the only thing a host can resolve. AZO_RHI_DEFINE_MODULE
    # marks it visible and everything else staying private is what keeps two modules that happen to
    # share a symbol name from resolving into each other.
    set_target_properties(${target} PROPERTIES
            CXX_VISIBILITY_PRESET hidden
            VISIBILITY_INLINES_HIDDEN ON)
endfunction()

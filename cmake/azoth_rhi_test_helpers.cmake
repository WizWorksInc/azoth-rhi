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

# Registration helpers for the test suites.
#
# Sources under tests/unit mirror include/azoth/rhi one directory per module, the shared harness
# lives in tests/support and the wiring that turns a directory into a target lives in tests/unit and
# tests/rigorous. A module target collects its own directory by glob so adding a case is dropping a
# <name>_test.cpp beside its siblings and rebuilding.

include_guard(GLOBAL)

include(azoth_rhi_diagnostics)
include(azoth_rhi_test_sinks)

if(NOT DEFINED AZOTH_RHI_TESTS_ROOT)
    message(FATAL_ERROR "azoth_rhi_test_helpers: set AZOTH_RHI_TESTS_ROOT before including this module.")
endif()

# Collects the test sources of one directory, relative to the tests root so callers can prefix them.
function(azoth_rhi_collect_test_sources source_dir out_var)
    cmake_parse_arguments(ARG "" "" "EXCLUDE;PATTERNS" ${ARGN})

    if(IS_ABSOLUTE "${source_dir}")
        set(_dir "${source_dir}")
    else()
        set(_dir "${AZOTH_RHI_TESTS_ROOT}/${source_dir}")
    endif()

    if(NOT EXISTS "${_dir}")
        message(FATAL_ERROR "azoth_rhi_collect_test_sources: no such directory: ${_dir}")
    endif()

    if(NOT ARG_PATTERNS)
        set(ARG_PATTERNS "*_test.cpp")
    endif()

    set(_found "")
    foreach(_pattern IN LISTS ARG_PATTERNS)
        # CONFIGURE_DEPENDS so dropping a new case into a module directory builds it. Without it a
        # configured tree keeps the list it globbed once and a test file added later is silently not
        # compiled: the suite goes on reporting green over coverage that is not there.
        file(GLOB _matches
                CONFIGURE_DEPENDS
                LIST_DIRECTORIES false
                RELATIVE "${AZOTH_RHI_TESTS_ROOT}"
                "${_dir}/${_pattern}")
        list(APPEND _found ${_matches})
    endforeach()

    foreach(_path IN LISTS ARG_EXCLUDE)
        list(REMOVE_ITEM _found "${_path}")
    endforeach()

    list(REMOVE_DUPLICATES _found)
    list(SORT _found)
    set(${out_var} ${_found} PARENT_SCOPE)
endfunction()

function(azoth_rhi_prefix_test_sources prefix sources out_var)
    set(_prefixed "")
    foreach(_source IN LISTS sources)
        list(APPEND _prefixed "${prefix}${_source}")
    endforeach()
    set(${out_var} ${_prefixed} PARENT_SCOPE)
endfunction()

# One executable, one ctest entry per TEST it declares.
#
# Exactly one label per suite. gtest_discover_tests writes PROPERTIES straight into a generated
# set_tests_properties call, where a CMake list arrives as separate arguments so LABELS carrying two
# values would be read as an extra property name and not as a second label. One label also keeps
# the roll-up targets honest: a suite belongs to the set it is run with.
function(azoth_rhi_add_gtest_suite target)
    cmake_parse_arguments(ARG "" "TIMEOUT;LABEL" "SOURCES;COMPILE_DEFINITIONS;COMPILE_OPTIONS;LINK_LIBRARIES" ${ARGN})

    if(NOT ARG_SOURCES)
        message(FATAL_ERROR "azoth_rhi_add_gtest_suite(${target}): SOURCES is required.")
    endif()
    if(NOT ARG_LABEL)
        message(FATAL_ERROR "azoth_rhi_add_gtest_suite(${target}): LABEL is required.")
    endif()

    add_executable(${target})

    # The entry point is compiled into each suite and not into the shared harness library because
    # the validation-mode definitions are applied per target and the banner it prints has to describe
    # the suite that is about to run, not whatever the harness was built with.
    target_sources(${target} PRIVATE ${ARG_SOURCES} "${AZOTH_RHI_TESTS_ROOT}/main.cpp")
    target_link_libraries(${target} PRIVATE azoth::rhi-test ${ARG_LINK_LIBRARIES})

    if(ARG_COMPILE_DEFINITIONS)
        target_compile_definitions(${target} PRIVATE ${ARG_COMPILE_DEFINITIONS})
    endif()
    if(ARG_COMPILE_OPTIONS)
        target_compile_options(${target} PRIVATE ${ARG_COMPILE_OPTIONS})
    endif()

    set(_properties LABELS ${ARG_LABEL})
    if(ARG_TIMEOUT)
        list(APPEND _properties TIMEOUT ${ARG_TIMEOUT})
    endif()

    # The diagnostics are environment, so ctest has to set them per test. A suite run through ctest is
    # then checked the same way a sample run through the launcher is, off the one configure flag.
    azoth_rhi_diagnostic_environment(_diagnostic_environment)
    if(_diagnostic_environment)
        list(APPEND _properties ENVIRONMENT "${_diagnostic_environment}")
    endif()

    azoth_rhi_apply_diagnostic_scheme(${target})

    azoth_rhi_stage_test_sink_runtime(${target})

    # Discovery at test time, not build time. A device probe belongs in the test run, not in
    # the middle of a build and it keeps cross-compiled builds configurable.
    gtest_discover_tests(${target}
            DISCOVERY_MODE PRE_TEST
            PROPERTIES ${_properties}
    )

    set_property(GLOBAL APPEND PROPERTY AZOTH_RHI_TEST_TARGETS ${target})
endfunction()

# A module is a directory under tests/unit whose name mirrors the part of the public API it covers.
function(azoth_rhi_add_unit_module)
    cmake_parse_arguments(ARG "" "TARGET;MODULE;LABEL" "EXCLUDE;PATTERNS;EXTRA_SOURCES;COMPILE_DEFINITIONS" ${ARGN})

    if(NOT ARG_TARGET OR NOT ARG_MODULE)
        message(FATAL_ERROR "azoth_rhi_add_unit_module: TARGET and MODULE are both required.")
    endif()
    if(NOT ARG_LABEL)
        set(ARG_LABEL unit)
    endif()

    azoth_rhi_collect_test_sources("unit/${ARG_MODULE}" _sources
            EXCLUDE ${ARG_EXCLUDE}
            PATTERNS ${ARG_PATTERNS})
    azoth_rhi_prefix_test_sources("../" "${_sources}" _prefixed)

    if(ARG_EXTRA_SOURCES)
        list(APPEND _prefixed ${ARG_EXTRA_SOURCES})
    endif()

    if(NOT _prefixed)
        message(FATAL_ERROR "azoth_rhi_add_unit_module(${ARG_TARGET}): tests/unit/${ARG_MODULE} holds no test sources.")
    endif()

    azoth_rhi_add_gtest_suite(${ARG_TARGET}
            SOURCES ${_prefixed}
            LABEL ${ARG_LABEL}
            COMPILE_DEFINITIONS ${ARG_COMPILE_DEFINITIONS})
endfunction()

# The conformance directory is flat, since its cases are about the library as a whole, not
# about one module: header hygiene, ABI shape, the error-reporting contract, backend parity.
function(azoth_rhi_add_conformance_suite target)
    cmake_parse_arguments(ARG "" "LABEL" "EXCLUDE;EXTRA_SOURCES;COMPILE_DEFINITIONS" ${ARGN})

    if(NOT ARG_LABEL)
        set(ARG_LABEL conformance)
    endif()

    azoth_rhi_collect_test_sources(conformance _sources EXCLUDE ${ARG_EXCLUDE})
    azoth_rhi_prefix_test_sources("../" "${_sources}" _prefixed)

    if(ARG_EXTRA_SOURCES)
        list(APPEND _prefixed ${ARG_EXTRA_SOURCES})
    endif()
    if(NOT _prefixed)
        message(FATAL_ERROR "azoth_rhi_add_conformance_suite(${target}): tests/conformance holds no test sources.")
    endif()

    azoth_rhi_add_gtest_suite(${target}
            SOURCES ${_prefixed}
            LABEL ${ARG_LABEL}
            COMPILE_DEFINITIONS ${ARG_COMPILE_DEFINITIONS})
endfunction()

# One translation unit per public header, each including only that header.
#
# A header that quietly depends on something an earlier include happened to pull in works everywhere
# inside this repo and breaks for the first consumer who includes it on its own. The only way to
# catch that is to compile each header alone, which is a build-time property and not a runtime
# one so this is a library the conformance suite links and not a ctest case.
#
# native/ is left out because those headers exist to reach the graphics SDKs and cannot compile
# without them. Everything else compiles on its own in every build, with the Tracy seam the one
# exception, guarded below.
function(azoth_rhi_add_header_self_containment_library target)
    set(_include_root "${PROJECT_SOURCE_DIR}/lib/include")

    # Same reason as the test sources: a public header added to an already configured tree would
    # otherwise never get the translation unit that proves it stands alone.
    file(GLOB_RECURSE _headers
            CONFIGURE_DEPENDS
            LIST_DIRECTORIES false
            RELATIVE "${_include_root}"
            "${_include_root}/azoth/*.hpp")

    # The conformance headers are checked too, even though they are not public and not installed. A
    # header a backend author is meant to include has to stand alone wherever it lives and one that
    # names a type without including the header that declares it compiles anyway for as long as every
    # consumer happens to include that header first.
    set(_conformance_root "${PROJECT_SOURCE_DIR}/tests/support")
    file(GLOB_RECURSE _conformance_headers
            CONFIGURE_DEPENDS
            LIST_DIRECTORIES false
            RELATIVE "${_conformance_root}"
            "${_conformance_root}/conformance/*.hpp")

    list(SORT _headers)
    list(SORT _conformance_headers)

    set(_generated_dir "${CMAKE_CURRENT_BINARY_DIR}/header_self_containment")
    file(MAKE_DIRECTORY "${_generated_dir}")

    set(_sources "")
    set(_covered 0)
    foreach(_header IN LISTS _headers)
        if(_header MATCHES "/native/")
            continue()
        endif()

        # Every public header compiles on its own in every build. The one exception is the Tracy
        # seam, where without the sink built the include is a deliberate hard error and not a
        # self-containment failure.
        set(_guard "")
        if(_header MATCHES "/host/tracy_profiler\\.hpp$")
            set(_guard "TRACY_ENABLE")
        endif()

        string(REGEX REPLACE "[/.]" "_" _stem "${_header}")
        set(_generated "${_generated_dir}/${_stem}.cpp")

        set(_body "// Generated by azoth_rhi_add_header_self_containment_library. Do not edit.\n")
        if(_guard)
            string(APPEND _body "#ifdef ${_guard}\n")
        endif()
        string(APPEND _body "#include \"${_header}\"\n")
        if(_guard)
            string(APPEND _body "#endif\n")
        endif()
        # A translation unit that ends up empty is ill-formed so every one gets a symbol.
        string(APPEND _body "namespace\n{\n\t[[maybe_unused]] constexpr int azothRhiHeaderIsSelfContained = ${_covered};\n} // namespace\n")

        file(CONFIGURE OUTPUT "${_generated}" CONTENT "${_body}" @ONLY)
        list(APPEND _sources "${_generated}")
        math(EXPR _covered "${_covered} + 1")
    endforeach()

    foreach(_header IN LISTS _conformance_headers)
        string(REGEX REPLACE "[/.]" "_" _stem "${_header}")
        set(_generated "${_generated_dir}/${_stem}.cpp")

        set(_body "// Generated by azoth_rhi_add_header_self_containment_library. Do not edit.\n")
        string(APPEND _body "#include \"${_header}\"\n")
        string(APPEND _body "namespace\n{\n\t[[maybe_unused]] constexpr int azothRhiHeaderIsSelfContained = ${_covered};\n} // namespace\n")

        file(CONFIGURE OUTPUT "${_generated}" CONTENT "${_body}" @ONLY)
        list(APPEND _sources "${_generated}")
        math(EXPR _covered "${_covered} + 1")
    endforeach()

    if(NOT _sources)
        message(FATAL_ERROR "azoth_rhi_add_header_self_containment_library(${target}): no public headers were found under ${_include_root}.")
    endif()

    if(NOT _conformance_headers)
        message(FATAL_ERROR "azoth_rhi_add_header_self_containment_library(${target}): no conformance headers were found under ${_conformance_root}.")
    endif()

    add_library(${target} STATIC ${_sources})
    target_include_directories(${target} PRIVATE "${_conformance_root}")

    # Links the backend SDK and not the library so the claim that linking that one target is
    # enough to include everything under include is the thing being compiled and not an
    # assumption. GoogleTest comes along because the conformance headers are written in its
    # assertions and the check compiles them the same way a caller of the suite would.
    target_link_libraries(${target} PRIVATE azoth::rhi-backend-sdk GTest::gtest)
    message(STATUS "AzothRHI: ${_covered} public headers checked for self-containment")
endfunction()

# Rigorous suites are the slow ones: exhaustion, reuse under pressure, concurrency and the cross-backend campaigns.
# They carry a longer default timeout because a real driver is a great deal slower than the null one.
function(azoth_rhi_add_rigorous_suite target)
    cmake_parse_arguments(ARG "" "MODULE;TIMEOUT;LABEL" "SOURCES;EXCLUDE;PATTERNS;COMPILE_DEFINITIONS" ${ARGN})

    if(NOT ARG_LABEL)
        set(ARG_LABEL rigorous)
    endif()
    if(NOT ARG_TIMEOUT)
        set(ARG_TIMEOUT 600)
    endif()

    set(_prefixed ${ARG_SOURCES})
    if(ARG_MODULE)
        azoth_rhi_collect_test_sources("rigorous/${ARG_MODULE}" _sources
                EXCLUDE ${ARG_EXCLUDE}
                PATTERNS ${ARG_PATTERNS})
        azoth_rhi_prefix_test_sources("../" "${_sources}" _module_sources)
        list(APPEND _prefixed ${_module_sources})
    endif()

    if(NOT _prefixed)
        message(FATAL_ERROR "azoth_rhi_add_rigorous_suite(${target}): no sources.")
    endif()

    azoth_rhi_add_gtest_suite(${target}
            SOURCES ${_prefixed}
            LABEL ${ARG_LABEL}
            TIMEOUT ${ARG_TIMEOUT}
            COMPILE_DEFINITIONS ${ARG_COMPILE_DEFINITIONS})
endfunction()

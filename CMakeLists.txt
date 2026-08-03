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

cmake_minimum_required(VERSION 3.24)


set(AZOTH_RHI_BUILD_VERSION 1.0.0)

project(AzothRHI
        VERSION ${AZOTH_RHI_BUILD_VERSION}
        DESCRIPTION "A C++23 render hardware interface with Vulkan, Direct3D 12 and Metal backends"
        HOMEPAGE_URL "https://github.com/WizWorksInc/AzothRHI"
        LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(AZOTH_RHI_IS_TOP_LEVEL ${PROJECT_IS_TOP_LEVEL})

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(azoth_rhi_backends)
include(azoth_rhi_version)

azoth_rhi_generate_version_header()

azoth_rhi_compute_platform_support()
azoth_rhi_declare_backend(VULKAN "Vulkan" AZOTH_RHI_VULKAN_SUPPORTED ON)
azoth_rhi_declare_backend(D3D12 "Direct3D 12" AZOTH_RHI_D3D12_SUPPORTED ON)
azoth_rhi_declare_backend(METAL "Metal 3" AZOTH_RHI_METAL_SUPPORTED ON)
azoth_rhi_declare_backend(METAL4 "Metal 4" AZOTH_RHI_METAL4_SUPPORTED ON)

option(AZOTH_RHI_DETECT_PLATFORM_APIS "Check at configure time that each enabled backend's SDK is installed" ON)
azoth_rhi_detect_platform_apis()

azoth_rhi_report_backends()

include(azoth_rhi_compiler_cache)
azoth_rhi_apply_compiler_cache()

include(azoth_rhi_sanitizers)
azoth_rhi_apply_sanitizer()

include(azoth_rhi_diagnostics)
azoth_rhi_apply_diagnostics()

option(AZOTH_RHI_ENABLE_PROFILING "Compile in the profiler instrumentation points" ON)

# A host that compiles without exceptions, which most engines do, cannot link a library that needs them.
#
# PUBLIC on purpose, because it changes HostAllocatorAdapter in a header a consumer includes: a consumer built without it against a library built
# with it would see two different allocate bodies. It joins the module ABI stamp for the same reason, since that is the switch a loadable backend has to
# agree on before anything in it is called.
#
# What it costs is stated here. A host allocation that fails aborts instead of reporting, which is what the standard library already does
# under -fno-exceptions. eOutOfHostMemory survives only where refusal is explicit and already checked: TypedObjectPool::New, SlotMap's chunk growth, and
# the TryPushBack and TryReserve helpers.
option(AZOTH_RHI_NO_EXCEPTIONS "Build a library that neither throws nor requires exceptions, for hosts compiled with -fno-exceptions" OFF)

include(azoth_rhi_test_sinks)
azoth_rhi_fetch_test_sinks()

include(azoth_rhi_profiling)
azoth_rhi_declare_profiling()
azoth_rhi_report_profiling()

option(AZOTH_RHI_BUILD_TESTS "Build the test suite" ${AZOTH_RHI_IS_TOP_LEVEL})
option(AZOTH_RHI_BUILD_UNIT_TESTS "Build the per-module unit suites (ctest label unit)" ON)
option(AZOTH_RHI_BUILD_RIGOROUS_TESTS "Build the conformance and cross-backend suites (ctest labels conformance and rigorous)" ON)
option(AZOTH_RHI_BUILD_STRESS_TESTS "Build the long-running stress suites (ctest label stress)" OFF)
option(AZOTH_RHI_BUILD_EXAMPLES "Build the examples" OFF)
# Off by default: it needs a Dear ImGui, which is a dependency nothing else here has, and a consumer that
# wants no interface should not be made to carry one. See extras/imgui/CMakeLists.txt for how to point it at yours.
option(AZOTH_RHI_BUILD_IMGUI "Build azoth::rhi-imgui, the Dear ImGui renderer" OFF)
option(AZOTH_RHI_BUILD_BENCHMARKS "Build the benchmarks the non-functional budgets are measured with" ${AZOTH_RHI_IS_TOP_LEVEL})
option(AZOTH_RHI_INSTALL "Generate the install and export rules" ${AZOTH_RHI_IS_TOP_LEVEL})

# The platform's own API, which is what a caller who names nothing should get. Apple ships Metal and the Vulkan beside it is MoltenVK, Vulkan translated
# onto Metal, so defaulting there would charge every caller for a translation the platform does not need. Windows ships Direct3D 12 and drives its tooling
# and driver testing from it. Everywhere else Vulkan is the only one there is. Overridable, and AZOTH_RHI_BACKEND still wins at runtime.
#
# On Apple that is Metal 4 where the build has it: a device on it fails cleanly on a machine that does not
# report the family, so naming it as the default costs an older Mac one refused creation and then Metal 3,
# and costs a newer one nothing.
if(APPLE AND AZOTH_RHI_BACKEND_METAL4)
    set(_azoth_rhi_default_backend "metal4")
elseif(APPLE)
    set(_azoth_rhi_default_backend "metal")
elseif(WIN32)
    set(_azoth_rhi_default_backend "d3d12")
else()
    set(_azoth_rhi_default_backend "vulkan")
endif()

set(AZOTH_RHI_DEFAULT_BACKEND "${_azoth_rhi_default_backend}" CACHE STRING "Backend chosen when nothing overrides it at runtime")

set_property(CACHE AZOTH_RHI_DEFAULT_BACKEND PROPERTY STRINGS vulkan d3d12 metal metal4 null)

if(NOT AZOTH_RHI_BACKEND_VULKAN AND NOT AZOTH_RHI_BACKEND_D3D12
        AND NOT AZOTH_RHI_BACKEND_METAL AND NOT AZOTH_RHI_BACKEND_METAL4)
    message(STATUS "AzothRHI: no hardware backend is enabled.")
endif()

if(AZOTH_RHI_IS_TOP_LEVEL)
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
endif()

include(GNUInstallDirs)

include(azoth_rhi_dependencies)
azoth_rhi_fetch_dependencies()
azoth_rhi_validate_profiling_targets()

add_subdirectory(lib)
add_subdirectory(utils)

if(AZOTH_RHI_BUILD_IMGUI)
    add_subdirectory(extras/imgui)
endif()

if(AZOTH_RHI_BUILD_TESTS)
    enable_testing()
endif()

include(azoth_rhi_source_gates)

azoth_rhi_add_source_gate(boundary gate_api_boundary
        "Checking the AzothRHI API boundary")
azoth_rhi_add_source_gate(host_allocations gate_host_allocation_contract
        "Checking that the AzothRHI library allocates only through the host allocator seam")
azoth_rhi_add_source_gate(impl_casts gate_impl_cast_contract
        "Checking that no facade impl pointer is cast without resolving what is in front of it")
azoth_rhi_add_source_gate(backend_locks gate_backend_lock_contract
        "Checking that no bundled backend synchronizes itself")
azoth_rhi_add_source_gate(public_headers gate_public_header_contract
        "Checking that no AzothRHI public header branches on which backends this build has")

if(AZOTH_RHI_BUILD_TESTS)
    string(TOUPPER "${AZOTH_RHI_SANITIZER}" _azoth_rhi_sanitizer_mode)
    if(_azoth_rhi_sanitizer_mode STREQUAL "")
        set(_azoth_rhi_sanitizer_mode "OFF")
    endif()

    if(AZOTH_RHI_INSTALL AND _azoth_rhi_sanitizer_mode STREQUAL "OFF")
        add_test(NAME gate_OutOfTreeBackend
                COMMAND ${CMAKE_COMMAND}
                -DAZOTH_RHI_SOURCE_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
                -DAZOTH_RHI_BUILD_ROOT=${CMAKE_CURRENT_BINARY_DIR}
                -DAZOTH_RHI_CMAKE=${CMAKE_COMMAND}
                -DAZOTH_RHI_CONFIG=$<CONFIG>
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/azoth_rhi_consumer_check.cmake
        )
        set_tests_properties(gate_OutOfTreeBackend PROPERTIES LABELS conformance TIMEOUT 900 RUN_SERIAL ON)
    endif()

    add_subdirectory(tests)


    include(azoth_rhi_test_targets)
endif()

if(AZOTH_RHI_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(AZOTH_RHI_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()

if(AZOTH_RHI_INSTALL)
    include(CMakePackageConfigHelpers)

    azoth_rhi_install_dependencies()

    set(AZOTH_RHI_INSTALL_TARGETS AzothRHI AzothRHI-backend-sdk AzothRHI-module-sdk)
    if(TARGET AzothRHI-native-vulkan)
        list(APPEND AZOTH_RHI_INSTALL_TARGETS AzothRHI-native-vulkan)
    endif()

    install(TARGETS ${AZOTH_RHI_INSTALL_TARGETS}
            EXPORT AzothRHITargets
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )

    install(DIRECTORY lib/include/azoth
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
            FILES_MATCHING PATTERN "*.hpp"
    )

    install(DIRECTORY "${PROJECT_BINARY_DIR}/include/azoth"
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
            FILES_MATCHING PATTERN "*.hpp"
    )

    install(EXPORT AzothRHITargets
            FILE AzothRHITargets.cmake
            NAMESPACE azoth::
            DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/AzothRHI
    )

    configure_package_config_file(
            cmake/AzothRHIConfig.cmake.in
            "${CMAKE_CURRENT_BINARY_DIR}/AzothRHIConfig.cmake"
            INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/AzothRHI
    )

    write_basic_package_version_file(
            "${CMAKE_CURRENT_BINARY_DIR}/AzothRHIConfigVersion.cmake"
            VERSION ${PROJECT_VERSION}
            COMPATIBILITY SameMinorVersion
    )

    install(FILES
            "${CMAKE_CURRENT_BINARY_DIR}/AzothRHIConfig.cmake"
            "${CMAKE_CURRENT_BINARY_DIR}/AzothRHIConfigVersion.cmake"
            DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/AzothRHI
    )

    install(FILES LICENSE NOTICE
            DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/licenses/AzothRHI
    )
endif()

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

# One target per module directory, driven entirely by the registry. Nothing here names a test file.

include(azoth_rhi_test_modules)

foreach(_module IN LISTS AZOTH_RHI_UNIT_MODULE_ORDER)
    set(_exclude_var AZOTH_RHI_UNIT_MODULE_${_module}_EXCLUDE)
    set(_module_args "")
    if(DEFINED ${_exclude_var})
        list(APPEND _module_args EXCLUDE ${${_exclude_var}})
    endif()

    azoth_rhi_add_unit_module(
            TARGET ${AZOTH_RHI_UNIT_MODULE_${_module}}
            MODULE ${_module}
            LABEL unit
            ${_module_args})
endforeach()

# The one reach into src left in the whole test tree and it belongs to one target and not to
# every suite through the shared harness. Everything a backend is written against lives in
# azoth::rhi-backend-sdk now so what is left here is the validation decorator: wrapper internals that
# are deliberately not part of that surface and still worth testing directly. Scoped this narrowly, a
# second suite reaching for src fails to compile instead of quietly inheriting the path.
target_include_directories(${AZOTH_RHI_UNIT_MODULE_validation} PRIVATE ${PROJECT_SOURCE_DIR}/lib/src)

# Both self-registering backends, linked into the one suite that asks whether they arrived. The device
# module and not a suite of their own because what is being tested is a backend turning up in a
# process that never named it and a suite built around them would be naming them in its own CMake.
target_link_libraries(${AZOTH_RHI_UNIT_MODULE_device} PRIVATE rhi_test_object_backend rhi_test_static_backend)

# The loadable backend is deliberately not linked. What the suite needs is where it landed and a guarantee
# it was built first, so the path arrives as a definition and the dependency is explicit.
add_dependencies(${AZOTH_RHI_UNIT_MODULE_device} rhi_test_loadable_backend)
target_compile_definitions(${AZOTH_RHI_UNIT_MODULE_device} PRIVATE
        AZOTH_RHI_TEST_LOADABLE_MODULE_PATH="$<TARGET_FILE:rhi_test_loadable_backend>")

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

# AZOTH_RHI_BUILD_VERSION is the one place the version is written. This writes it into the build
# tree as core/version_numbers.hpp and version.hpp builds the constants and the version string from
# it so bumping a release is one edit and nothing downstream can disagree with it.
function(azoth_rhi_generate_version_header)
    set(AZOTH_RHI_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
    set(AZOTH_RHI_VERSION_MINOR ${PROJECT_VERSION_MINOR})
    set(AZOTH_RHI_VERSION_PATCH ${PROJECT_VERSION_PATCH})

    configure_file(
            "${PROJECT_SOURCE_DIR}/cmake/in/version_numbers.hpp.in"
            "${PROJECT_BINARY_DIR}/include/azoth/rhi/core/version_numbers.hpp"
            @ONLY
    )

    message(STATUS "AzothRHI: version ${PROJECT_VERSION}")
endfunction()

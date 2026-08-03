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

# A separate project, configured against an installed AzothRHI and nothing else.
#
# Deliberately not part of the main build. It finds the package the way a stranger would, links the two
# SDK targets by name and includes only what installing put there. Anything the install rules forgot is
# a configure or link failure here and not something noticed by the first person outside this
# repository to try it.
cmake_minimum_required(VERSION 3.24)
project(AzothRHIConsumer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(AzothRHI REQUIRED)

# The backend SDK on its own. Linking azoth::rhi as well would hide a header that the SDK target failed
# to carry, which is the thing most likely to be wrong and the reason this exists.
add_executable(consumer_backend out_of_tree_backend.cpp)
target_link_libraries(consumer_backend PRIVATE azoth::rhi-backend-sdk)

# And the module SDK, built as a real loadable module through the shipped helper so the helper is
# exercised from outside the tree that defines it.
add_library(consumer_module MODULE out_of_tree_module.cpp)
target_link_libraries(consumer_module PRIVATE azoth::rhi-module-sdk)
set_target_properties(consumer_module PROPERTIES PREFIX "" CXX_VISIBILITY_PRESET hidden)

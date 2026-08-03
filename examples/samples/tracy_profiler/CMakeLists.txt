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

# The sink this shows is compiled into the library only when the build found a Tracy client, so without
# one there is no rhi::TracyProfiler to install and its header says as much with an #error. Skipping is
# the same answer the samples needing SDL3 give, since a profiler the examples merely demonstrate is not
# something a build should fail over.
#
# A host builds its own client and adds it before it adds this library. A build of this repository alone
# has none to add, which is what AZOTH_RHI_TESTS_FETCH_TRACY stands in for.
if(NOT AZOTH_RHI_TRACY_ENABLED)
    message(STATUS "AzothRHI examples: skipping tracy_profiler, no Tracy client was found. Turn on AZOTH_RHI_TESTS_FETCH_TRACY to fetch one.")
    return()
endif()

# Tracy is named by nothing here. The library links the client publicly, so its headers and the
# TRACY_ENABLE it was built with reach this sample the same way they reach the sink.
azoth_rhi_add_example(tracy_profiler
        SOURCES main.cpp)

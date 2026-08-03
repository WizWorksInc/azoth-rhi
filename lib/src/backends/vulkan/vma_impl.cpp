// Copyright 2026 Ian Pike
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// VK_NO_PROTOTYPES requires VMA to use the Vulkan function table supplied at allocator creation.
#define VMA_IMPLEMENTATION			   // NOLINT(cppcoreguidelines-macro-usage): enables the VMA implementation body
#define VMA_STATIC_VULKAN_FUNCTIONS	 0 // NOLINT(cppcoreguidelines-macro-usage): Vulkan functions are not linked directly
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1 // NOLINT(cppcoreguidelines-macro-usage): Vulkan functions are loaded from supplied pointers

#include <vk_mem_alloc.h>

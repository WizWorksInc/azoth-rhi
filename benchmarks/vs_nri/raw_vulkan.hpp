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

#pragma once

#include "harness.hpp"
#include "scene.hpp"

#include <cstdint>

// Plain Vulkan on the library's own device and command buffer, so a difference between the two processes that shows up here is the environment's.
namespace vsnri::raw
{

	struct Handles final
	{
		void * instance				 = nullptr;
		void * physicalDevice		 = nullptr;
		void * device				 = nullptr;
		void * getInstanceProcAddr	 = nullptr;
		void * getDeviceProcAddr	 = nullptr;
		std::uint64_t barrierImage	 = 0;
		std::uint64_t materialBuffer = 0;
	};

	[[nodiscard]] bool Prepare(const Handles & handles);

	void Release();

	[[nodiscard]] std::uint64_t RecordShape(Shape shape, void * commandBuffer, std::uint32_t commands);

	// Fills the device and driver fields from VkPhysicalDeviceDriverProperties, which is what tells MoltenVK from KosmicKrisp.
	[[nodiscard]] bool Identify(const Handles & handles, Identity & identity);

}

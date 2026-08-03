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

#include "azoth/rhi/host/surface_source.hpp"
#include "azoth/rhi/native/surface_payloads.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>

namespace rhi = azo::rhi;

namespace
{

	template <class Payload>
	[[nodiscard]] rhi::SurfaceRequest RequestFor(Payload & payload)
	{
		return rhi::SurfaceRequest{
			.id		  = Payload::kId,
			.byteSize = sizeof(Payload),
			.payload  = &payload,
		};
	}

	TEST(SurfaceRequest, EveryPayloadAnswersToItsOwnId)
	{
		EXPECT_NE(rhi::native::VulkanLoaderPayload::kId, rhi::native::VulkanSurfacePayload::kId);
		EXPECT_NE(rhi::native::VulkanSurfacePayload::kId, rhi::native::MetalSurfacePayload::kId);
		EXPECT_NE(rhi::native::MetalSurfacePayload::kId, rhi::native::Win32SurfacePayload::kId);
		EXPECT_NE(rhi::native::Win32SurfacePayload::kId, rhi::native::VulkanLoaderPayload::kId);
	}

	TEST(SurfaceRequest, APayloadIsHandedBackOnlyForTheRequestThatNamesIt)
	{
		rhi::native::MetalSurfacePayload metal{};
		const rhi::SurfaceRequest request = RequestFor(metal);

		EXPECT_EQ(rhi::SurfacePayloadOf<rhi::native::MetalSurfacePayload>(request), &metal);

		EXPECT_EQ(rhi::SurfacePayloadOf<rhi::native::VulkanSurfacePayload>(request), nullptr);
		EXPECT_EQ(rhi::SurfacePayloadOf<rhi::native::Win32SurfacePayload>(request), nullptr);
	}

	TEST(SurfaceRequest, ARequestShorterThanThePayloadIsRefused)
	{
		rhi::native::VulkanSurfacePayload payload{};

		rhi::SurfaceRequest older = RequestFor(payload);
		older.byteSize			  = sizeof(payload) - 1;
		EXPECT_EQ(rhi::SurfacePayloadOf<rhi::native::VulkanSurfacePayload>(older), nullptr) << "a request too short to hold the payload was accepted";

		rhi::SurfaceRequest newer = RequestFor(payload);
		newer.byteSize			  = sizeof(payload) + 64;
		EXPECT_EQ(rhi::SurfacePayloadOf<rhi::native::VulkanSurfacePayload>(newer), &payload)
			<< "a request from newer headers was refused, not read as a prefix";
	}

	TEST(SurfaceRequest, ARequestCarryingNothingIsRefused)
	{
		rhi::SurfaceRequest empty{};
		empty.id	   = rhi::native::MetalSurfacePayload::kId;
		empty.byteSize = sizeof(rhi::native::MetalSurfacePayload);

		EXPECT_EQ(rhi::SurfacePayloadOf<rhi::native::MetalSurfacePayload>(empty), nullptr) << "a request with no payload behind it was accepted";
	}

	TEST(SurfaceSource, AHostAnswersOneRequestAndDeclinesTheRest)
	{
		class MetalOnlyWindow final : public rhi::SurfaceSource
		{
		public:
			[[nodiscard]] bool Provide(const rhi::SurfaceRequest & request) override
			{
				auto * metal = rhi::SurfacePayloadOf<rhi::native::MetalSurfacePayload>(request);
				if (metal == nullptr)
				{
					return false;
				}

				metal->layer = &m_layer;
				return true;
			}

			std::uint8_t m_layer = 0;
		};

		MetalOnlyWindow window;

		rhi::native::MetalSurfacePayload metal{};
		EXPECT_TRUE(window.Provide(RequestFor(metal)));
		EXPECT_EQ(metal.layer, &window.m_layer);

		rhi::native::VulkanSurfacePayload vulkan{};
		EXPECT_FALSE(window.Provide(RequestFor(vulkan))) << "a window that has no Vulkan claimed to have answered";
		EXPECT_EQ(vulkan.surface, 0u);
	}

	TEST(ResolveVulkanLoader, AnswersTheSameThingEveryTime)
	{
		EXPECT_EQ(rhi::native::ResolveVulkanLoader(), rhi::native::ResolveVulkanLoader());
	}

} // namespace

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

#include "azoth/rhi/backend/dispatch.hpp"
#include "azoth/rhi/core/result.hpp"
#include "azoth/rhi/device/device.hpp"
#include "azoth/rhi/resources/resources.hpp"

#include <gtest/gtest.h>

#include <memory>

namespace rhi = azo::rhi;

namespace
{

	rhi::BufferHandle g_handle{};
	rhi::MemoryInfo g_memoryInfo{};
	rhi::MappedMemory g_mapped{};
	rhi::Error g_reported{};

	void Answer(const rhi::Error & reported) noexcept
	{
		if (reported.code != rhi::ErrorCode::eOk)
		{
			g_reported = reported;
		}
	}

	rhi::BufferHandle CreateBuffer(void *, const rhi::BufferDesc &, rhi::Error * error) noexcept
	{
		if (error != nullptr)
		{
			*error = g_reported;
		}

		return g_handle;
	}

	bool GetBufferMemoryInfo(void *, const rhi::BufferDesc &, rhi::MemoryInfo * out, rhi::Error * error) noexcept
	{
		if (error != nullptr)
		{
			*error = g_reported;
		}

		*out = g_memoryInfo;
		return g_memoryInfo.size != 0;
	}

	rhi::MappedMemory Map(void *, rhi::BufferHandle, const rhi::MapDesc &, rhi::Error * error) noexcept
	{
		if (error != nullptr)
		{
			*error = g_reported;
		}

		return g_mapped;
	}

	rhi::DeviceCaps g_caps{};

	const rhi::DeviceCaps & GetCaps(void *) noexcept
	{
		return g_caps;
	}

	rhi::CoreDeviceApi g_core{};
	rhi::PlacedMemoryApi g_placed{};

	const rhi::CoreDeviceApi & CoreBlock() noexcept
	{
		return g_core;
	}

	const rhi::PlacedMemoryApi & PlacedBlock() noexcept
	{
		return g_placed;
	}

	struct FakeDevice final
	{
		const rhi::BackendObject * object = nullptr;
	};

	class ErrorRoutingTest : public ::testing::Test
	{
	protected:
		void SetUp() override
		{
			g_handle	 = {};
			g_memoryInfo = {};
			g_mapped	 = {};
			g_reported	 = {};

			g_core				= rhi::CoreDeviceApi{};
			g_core.createBuffer = &CreateBuffer;
			g_core.map			= &Map;

			g_core.getCaps	= &GetCaps;
			g_core.getQueue = [](void *, rhi::QueueType, std::uint32_t, rhi::Error *) noexcept
			{
				return static_cast<void *>(nullptr);
			};
			g_core.createCommandPool = [](void *, const rhi::CommandPoolDesc &, rhi::Error *) noexcept
			{
				return static_cast<void *>(nullptr);
			};

			g_placed					 = rhi::PlacedMemoryApi{};
			g_placed.getBufferMemoryInfo = &GetBufferMemoryInfo;

			m_device.object = rhi::PublishingObject<rhi::Published<rhi::CoreDeviceApi, &CoreBlock>, rhi::Published<rhi::PlacedMemoryApi, &PlacedBlock>>();
			m_blocks		= std::make_unique<rhi::BackendBlockSet>(&m_device, rhi::DeviceDesc{});
		}

		[[nodiscard]] rhi::Device Dev()
		{
			return rhi::detail::FacadeBuilder::MakeDevice(&m_device, m_blocks.get());
		}

		FakeDevice m_device{};
		std::unique_ptr<rhi::BackendBlockSet> m_blocks;
	};

	constexpr rhi::BufferHandle kPlausible{ .index = 7, .generation = 1 };

	TEST_F(ErrorRoutingTest, AHandleHandedBackBesideAFailureIsRefusedByAllThreeForms)
	{
		g_handle = kPlausible;
		Answer(rhi::Error{ .code = rhi::ErrorCode::eOutOfDeviceMemory, .message = "out of memory, here is a handle anyway" });

		EXPECT_FALSE(Dev().CreateBuffer(rhi::BufferDesc{}).IsValid()) << "the plain form handed back a handle the backend also failed on";

		rhi::Error error{};
		EXPECT_FALSE(Dev().CreateBuffer(rhi::BufferDesc{}, error).IsValid());
		EXPECT_EQ(error.code, rhi::ErrorCode::eOutOfDeviceMemory);

		const rhi::Result<rhi::BufferHandle> asResult = Dev().CreateBufferWithResult(rhi::BufferDesc{});
		EXPECT_FALSE(asResult.HasValue());
		EXPECT_EQ(asResult.GetError().code, rhi::ErrorCode::eOutOfDeviceMemory);
	}

	TEST_F(ErrorRoutingTest, AFailureWithNoCodeStillReadsAsAFailure)
	{
		g_handle = {};

		rhi::Error error{};
		EXPECT_FALSE(Dev().CreateBuffer(rhi::BufferDesc{}, error).IsValid());
		EXPECT_NE(error.code, rhi::ErrorCode::eOk);
		EXPECT_NE(error.message, nullptr);

		const rhi::Result<rhi::BufferHandle> asResult = Dev().CreateBufferWithResult(rhi::BufferDesc{});
		EXPECT_FALSE(asResult.HasValue());
		EXPECT_EQ(asResult.GetError().code, error.code);
	}

	TEST_F(ErrorRoutingTest, ACleanSuccessIsStillASuccessInAllThreeForms)
	{
		g_handle = kPlausible;

		EXPECT_TRUE(Dev().CreateBuffer(rhi::BufferDesc{}).IsValid());

		rhi::Error error{};
		EXPECT_TRUE(Dev().CreateBuffer(rhi::BufferDesc{}, error).IsValid());
		EXPECT_EQ(error.code, rhi::ErrorCode::eOk);

		const rhi::Result<rhi::BufferHandle> asResult = Dev().CreateBufferWithResult(rhi::BufferDesc{});
		ASSERT_TRUE(asResult.HasValue());
		EXPECT_EQ(asResult.Value(), kPlausible);
	}

	TEST_F(ErrorRoutingTest, AnAnsweredQueryBesideAFailureIsRefusedAndClearsItsOutput)
	{
		g_memoryInfo = rhi::MemoryInfo{ .size = 256, .alignment = 256 };
		Answer(rhi::Error{ .code = rhi::ErrorCode::eNativeApiError, .message = "the driver refused, here is a footprint anyway" });

		rhi::MemoryInfo info{ .size = 1 };
		rhi::Error error{};
		EXPECT_FALSE(Dev().GetBufferMemoryInfo(rhi::BufferDesc{}, info, error));
		EXPECT_EQ(info.size, 0u) << "a refused query left a plausible footprint behind";
		EXPECT_EQ(error.code, rhi::ErrorCode::eNativeApiError);

		const rhi::Result<rhi::MemoryInfo> asResult = Dev().GetBufferMemoryInfoWithResult(rhi::BufferDesc{});
		EXPECT_FALSE(asResult.HasValue());
		EXPECT_EQ(asResult.GetError().code, rhi::ErrorCode::eNativeApiError);
	}

	TEST_F(ErrorRoutingTest, AMappingHandedBackBesideAFailureIsRefused)
	{
		int storage	  = 0;
		g_mapped.data = &storage;
		g_mapped.size = sizeof(storage);
		Answer(rhi::Error{ .code = rhi::ErrorCode::eInvalidState, .message = "not mappable, here is a pointer anyway" });

		EXPECT_EQ(Dev().Map(rhi::BufferHandle{}, rhi::MapDesc{}).data, nullptr);

		const rhi::Result<rhi::MappedMemory> asResult = Dev().MapWithResult(rhi::BufferHandle{}, rhi::MapDesc{});
		EXPECT_FALSE(asResult.HasValue());
		EXPECT_EQ(asResult.GetError().code, rhi::ErrorCode::eInvalidState);
	}

} // namespace

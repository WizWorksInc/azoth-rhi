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

#include "azoth/rhi/commands/command.hpp"
#include "azoth/rhi/device/device.hpp"
#include "azoth/rhi/resources/resources.hpp"

#include "conformance/matchers.hpp"
#include "conformance/recording.hpp"
#include "conformance/samples.hpp"
#include "harness/backends.hpp"
#include "harness/environment.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>

namespace rhi  = azo::rhi;
namespace test = azo::rhi::test;

namespace
{

	class ClearUsageTest : public test::BackendTest
	{
	};

	AZO_RHI_BACKEND_SUITE(ClearUsageTest);

	TEST_P(ClearUsageTest, RefusesAClearOfABufferThatDeclaredNoStorageUsage)
	{
		AZO_RHI_REQUIRE_HANDLE_VALIDATION();

		rhi::Error error{};
		const rhi::BufferHandle readback = Dev().create_buffer(test::samples::ReadbackBuffer(), error);
		ASSERT_TRUE(test::Ok(readback.is_valid(), error));
		const rhi::BufferHandle storage = Dev().create_buffer(test::samples::StorageBuffer(), error);
		ASSERT_TRUE(test::Ok(storage.is_valid(), error));

		{
			test::Recording recording(Dev());
			ASSERT_TRUE(test::Ok(recording.IsRecording(), recording.GetError()));

			rhi::Error clearError{};
			EXPECT_FALSE(recording.List().clear_buffer(readback, 0, test::samples::kBufferSize, 0u, clearError))
				<< "a buffer created without BufferUsage::eStorage was cleared";
			EXPECT_TRUE(test::ErrorIsPopulated(clearError));

			EXPECT_TRUE(test::Ok(recording.List().clear_buffer(storage, 0, test::samples::kBufferSize, 0u, error), error));

			EXPECT_TRUE(test::Ok(recording.End(), recording.GetError()));
		}

		EXPECT_TRUE(test::Ok(Dev().destroy(storage, {}, error), error));
		EXPECT_TRUE(test::Ok(Dev().destroy(readback, {}, error), error));
	}

	TEST_P(ClearUsageTest, RefusesAClearOfATextureThatDeclaredNoColorAttachmentUsage)
	{
		AZO_RHI_REQUIRE_HANDLE_VALIDATION();

		rhi::Error error{};
		const rhi::TextureHandle sampled = Dev().create_texture(test::samples::SampledTexture2D(), error);
		ASSERT_TRUE(test::Ok(sampled.is_valid(), error));

		rhi::TextureDesc targetDesc = test::samples::ColorTarget2D();
		targetDesc.usage |= rhi::TextureUsage::eCopyDst;
		const rhi::TextureHandle target = Dev().create_texture(targetDesc, error);
		ASSERT_TRUE(test::Ok(target.is_valid(), error));

		const std::array<rhi::TextureSubresourceRange, 1> ranges{ rhi::TextureSubresourceRange{} };
		const rhi::ClearColor color{ .r = 0.0f, .g = 0.0f, .b = 0.0f, .a = 1.0f };

		{
			test::Recording recording(Dev());
			ASSERT_TRUE(test::Ok(recording.IsRecording(), recording.GetError()));

			rhi::Error clearError{};
			EXPECT_FALSE(recording.List().clear_texture(sampled, color, ranges, clearError))
				<< "a texture created without TextureUsage::eColorAttachment was cleared";
			EXPECT_TRUE(test::ErrorIsPopulated(clearError));

			EXPECT_TRUE(test::Ok(recording.List().clear_texture(target, color, ranges, error), error));

			EXPECT_TRUE(test::Ok(recording.End(), recording.GetError()));
		}

		EXPECT_TRUE(test::Ok(Dev().destroy(target, {}, error), error));
		EXPECT_TRUE(test::Ok(Dev().destroy(sampled, {}, error), error));
	}

}

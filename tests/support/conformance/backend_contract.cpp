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

#include "conformance/backend_contract.hpp"

#include "azoth/rhi/commands/command.hpp"
#include "azoth/rhi/resources/resources.hpp"

#include "conformance/matchers.hpp"
#include "conformance/overload_contract.hpp"
#include "conformance/recording.hpp"
#include "conformance/samples.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <string_view>
#include <vector>

namespace azo::rhi::test::oracle
{

	void CheckDeviceIdentity(Device device, const BackendIdentity & expected)
	{
		EXPECT_EQ(device.get_graphics_api_id(), expected.id);
		EXPECT_EQ(device.get_graphics_api_name(), std::string_view{ expected.displayName });

		EXPECT_EQ(device.get_caps().apiId, expected.id);
		EXPECT_EQ(device.get_adapter_info().apiId, expected.id);
		EXPECT_NE(device.get_adapter_info().name, nullptr) << "the adapter record has no name";
	}

	void CheckQueueAvailability(Device device)
	{
		const std::uint32_t graphicsCount = device.get_queue_count(QueueType::eGraphics);
		ASSERT_GE(graphicsCount, 1u) << "a device created with the default queue set has no graphics queue";

		Error error{};
		const Queue queue = device.get_queue(QueueType::eGraphics, 0, error);
		EXPECT_TRUE(Ok(queue.is_valid(), error));
		EXPECT_EQ(queue.get_type(), QueueType::eGraphics);

		for (std::uint32_t index = 0; index < graphicsCount; ++index)
		{
			Error indexError{};
			const Queue indexed = device.get_queue(QueueType::eGraphics, index, indexError);
			EXPECT_TRUE(Ok(indexed.is_valid(), indexError)) << "queue index " << index << " of " << graphicsCount << " did not resolve";
		}
	}

	void CheckResourceLifecycle(Device device)
	{
		Error error{};

		const BufferHandle first = device.create_buffer(samples::StorageBuffer(), error);
		ASSERT_TRUE(Ok(first.is_valid(), error));

		const BufferHandle second = device.create_buffer(samples::StorageBuffer(), error);
		ASSERT_TRUE(Ok(second.is_valid(), error));

		EXPECT_NE(first, second) << "two live buffers were handed the same handle";

		const TextureHandle texture = device.create_texture(samples::SampledTexture2D(), error);
		ASSERT_TRUE(Ok(texture.is_valid(), error));

		EXPECT_TRUE(Ok(device.destroy(texture, {}, error), error));
		EXPECT_TRUE(Ok(device.destroy(second, {}, error), error));
		EXPECT_TRUE(Ok(device.destroy(first, {}, error), error));
	}

	void CheckFailuresCarryDiagnostics(Device device)
	{
		Error error{};
		const Queue queue = device.get_queue(QueueType::eGraphics, device.get_queue_count(QueueType::eGraphics), error);
		ASSERT_FALSE(queue.is_valid());
		EXPECT_TRUE(ErrorIsPopulated(error));

		const Queue good = device.get_queue(QueueType::eGraphics, 0, error);
		ASSERT_TRUE(good.is_valid());
		EXPECT_EQ(error.code, ErrorCode::eOk) << "a successful call left the previous failure's error behind";
	}

	void CheckCommandListLifecycle(Device device)
	{
		Recording recording(device);
		ASSERT_TRUE(Ok(recording.IsRecording(), recording.GetError()));

		EXPECT_TRUE(recording.End()) << Describe(recording.GetError());

		Error error{};
		CommandList second = recording.Pool().allocate("azoth.rhi.test.secondList", error);
		ASSERT_TRUE(Ok(second.is_valid(), error));
		EXPECT_TRUE(Ok(second.begin(error), error));
		EXPECT_TRUE(Ok(second.end(error), error));
	}

	void CheckGarbageCollection(Device device)
	{
		Error error{};
		std::vector<BufferHandle> buffers;
		buffers.reserve(8);
		for (int index = 0; index < 8; ++index)
		{
			const BufferHandle buffer = device.create_buffer(samples::StorageBuffer(), error);
			ASSERT_TRUE(Ok(buffer.is_valid(), error));
			buffers.push_back(buffer);
		}

		for (const BufferHandle buffer : buffers)
		{
			ASSERT_TRUE(Ok(device.destroy(buffer, {}, error), error));
		}

		EXPECT_TRUE(Ok(device.collect_garbage(error), error));
	}

	void CheckWholeContract(Device device, const BackendIdentity & expected)
	{
		CheckDeviceIdentity(device, expected);
		CheckQueueAvailability(device);
		CheckResourceLifecycle(device);
		CheckOverloadsAgree(device);
		CheckFailuresCarryDiagnostics(device);
		CheckCommandListLifecycle(device);
		CheckGarbageCollection(device);
	}

}

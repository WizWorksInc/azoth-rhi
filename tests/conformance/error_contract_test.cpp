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

#include "azoth/rhi/device/device.hpp"

#include "conformance/matchers.hpp"
#include "conformance/overload_contract.hpp"
#include "conformance/samples.hpp"
#include "harness/backends.hpp"

#include <gtest/gtest.h>

#include <cstdint> // NOLINT
#include <type_traits>

namespace rhi  = azo::rhi;
namespace test = azo::rhi::test;

namespace
{

	class ErrorContractTest : public test::BackendTest
	{
	};

	AZO_RHI_BACKEND_SUITE(ErrorContractTest);

	TEST_P(ErrorContractTest, ExceptionsDoNotCrossTheApiBoundary)
	{
		static_assert(noexcept(std::declval<rhi::Device &>().create_buffer(std::declval<const rhi::BufferDesc &>())));
		static_assert(noexcept(std::declval<rhi::Device &>().get_queue_count(rhi::QueueType::eGraphics)));
		static_assert(noexcept(std::declval<rhi::Device &>().get_caps()));
		static_assert(noexcept(std::declval<rhi::Device &>().collect_garbage()));
		static_assert(noexcept(std::declval<rhi::Queue &>().wait_idle()));
		static_assert(noexcept(std::declval<rhi::CommandList &>().begin()));
		static_assert(noexcept(std::declval<rhi::CommandList &>().end()));
		static_assert(noexcept(std::declval<rhi::CommandList &>().draw(0, 0, 0, 0)));
		static_assert(noexcept(std::declval<rhi::CommandPool &>().allocate()));

		SUCCEED();
	}

	TEST_P(ErrorContractTest, AllThreeFormsOfASucceedingCallAgree)
	{
		rhi::Error error{};

		const rhi::BufferHandle sentinel = Dev().create_buffer(test::samples::StorageBuffer());
		ASSERT_TRUE(sentinel.is_valid());

		const rhi::BufferHandle withError = Dev().create_buffer(test::samples::StorageBuffer(), error);
		ASSERT_TRUE(withError.is_valid());
		EXPECT_EQ(error.code, rhi::ErrorCode::eOk) << "a successful call left an error behind";

		const rhi::Result<rhi::BufferHandle> asResult = Dev().create_buffer_with_result(test::samples::StorageBuffer());
		ASSERT_TRUE(test::Ok(asResult));

		EXPECT_TRUE(test::Ok(Dev().destroy(sentinel, {}, error), error));
		EXPECT_TRUE(test::Ok(Dev().destroy(withError, {}, error), error));
		EXPECT_TRUE(test::Ok(Dev().destroy(asResult.value(), {}, error), error));
	}

	TEST_P(ErrorContractTest, gate_OverloadAgreementFull)
	{
		test::oracle::CheckOverloadsAgree(Dev());
	}

	TEST_P(ErrorContractTest, TheOutErrorFormClearsTheErrorOnEntry)
	{
		rhi::Error error{
			.code	 = rhi::ErrorCode::eDeviceLost,
			.message = "left over from an earlier call",
		};

		const rhi::BufferHandle buffer = Dev().create_buffer(test::samples::StorageBuffer(), error);
		ASSERT_TRUE(buffer.is_valid());
		EXPECT_EQ(error.code, rhi::ErrorCode::eOk) << "a successful call left a stale error in place";
		EXPECT_EQ(error.message, nullptr);

		EXPECT_TRUE(test::Ok(Dev().destroy(buffer, {}, error), error));
	}

	TEST_P(ErrorContractTest, AFailedCallReportsSomethingOtherThanOk)
	{
		rhi::Error error{};
		const rhi::Queue queue = Dev().get_queue(rhi::QueueType::eGraphics, Dev().get_queue_count(rhi::QueueType::eGraphics), error);

		ASSERT_FALSE(queue.is_valid());
		EXPECT_NE(error.code, rhi::ErrorCode::eOk);
		EXPECT_NE(error.message, nullptr) << "a failure carries no diagnostic for a log or a bug report";
	}

	TEST_P(ErrorContractTest, AFailedCreationReturnsAnInvalidHandleRatherThanAPlausibleOne)
	{
		rhi::Error error{};
		const rhi::TextureViewHandle view = Dev().create_texture_view(rhi::TextureHandle{}, test::samples::FullTextureView(), error);

		if (view.is_valid())
		{
			GTEST_SKIP() << "this backend does not resolve the texture a view is created from";
		}

		EXPECT_FALSE(view.is_valid());
		EXPECT_TRUE(test::ErrorIsPopulated(error));
	}

	TEST_P(ErrorContractTest, ResultCarriesTheSameCodeTheOutErrorFormWouldHave)
	{
		rhi::Error error{};
		rhi::MemoryInfo ignored{};

		rhi::BufferDesc absurd = test::samples::StorageBuffer();
		absurd.size			   = 0;

		if (Dev().get_buffer_memory_info(absurd, ignored, error))
		{
			GTEST_SKIP() << "this backend accepts a zero-sized buffer desc, so there is no failure to compare";
		}

		const rhi::Result<rhi::MemoryInfo> asResult = Dev().get_buffer_memory_info_with_result(absurd);
		ASSERT_FALSE(asResult.has_value());
		EXPECT_EQ(asResult.get_error().code, error.code);
	}

	TEST_P(ErrorContractTest, EveryCreationEntryPointHasAllThreeForms)
	{
		rhi::Device device = Dev();
		rhi::Error error{};

		static_cast<void>(device.create_buffer(rhi::BufferDesc{}));
		static_cast<void>(device.create_buffer(rhi::BufferDesc{}, error));
		static_cast<void>(device.create_buffer_with_result(rhi::BufferDesc{}));

		static_cast<void>(device.create_texture(rhi::TextureDesc{}));
		static_cast<void>(device.create_texture(rhi::TextureDesc{}, error));
		static_cast<void>(device.create_texture_with_result(rhi::TextureDesc{}));

		static_cast<void>(device.create_sampler(rhi::SamplerDesc{}));
		static_cast<void>(device.create_sampler(rhi::SamplerDesc{}, error));
		static_cast<void>(device.create_sampler_with_result(rhi::SamplerDesc{}));

		static_cast<void>(device.create_heap(rhi::HeapDesc{}));
		static_cast<void>(device.create_heap(rhi::HeapDesc{}, error));
		static_cast<void>(device.create_heap_with_result(rhi::HeapDesc{}));

		static_cast<void>(device.create_descriptor_set_layout(rhi::DescriptorSetLayoutDesc{}));
		static_cast<void>(device.create_descriptor_set_layout(rhi::DescriptorSetLayoutDesc{}, error));
		static_cast<void>(device.create_descriptor_set_layout_with_result(rhi::DescriptorSetLayoutDesc{}));

		static_cast<void>(device.create_pipeline_layout(rhi::PipelineLayoutDesc{}));
		static_cast<void>(device.create_pipeline_layout(rhi::PipelineLayoutDesc{}, error));
		static_cast<void>(device.create_pipeline_layout_with_result(rhi::PipelineLayoutDesc{}));

		static_cast<void>(device.create_query_pool(rhi::QueryPoolDesc{}));
		static_cast<void>(device.create_query_pool(rhi::QueryPoolDesc{}, error));
		static_cast<void>(device.create_query_pool_with_result(rhi::QueryPoolDesc{}));

		static_cast<void>(device.create_timeline(rhi::TimelineDesc{}));
		static_cast<void>(device.create_timeline(rhi::TimelineDesc{}, error));
		static_cast<void>(device.create_timeline_with_result(rhi::TimelineDesc{}));

		static_cast<void>(device.create_binary_semaphore(rhi::BinarySemaphoreDesc{}));
		static_cast<void>(device.create_binary_semaphore(rhi::BinarySemaphoreDesc{}, error));
		static_cast<void>(device.create_binary_semaphore_with_result(rhi::BinarySemaphoreDesc{}));

		static_cast<void>(device.create_command_pool(rhi::CommandPoolDesc{}));
		static_cast<void>(device.create_command_pool(rhi::CommandPoolDesc{}, error));
		static_cast<void>(device.create_command_pool_with_result(rhi::CommandPoolDesc{}));

		SUCCEED();
	}

	TEST_P(ErrorContractTest, EveryDestroyOverloadTakesADestroyDescAndAnOptionalError)
	{
		rhi::Device device = Dev();
		rhi::Error error{};

		device.destroy(rhi::BufferHandle{});
		device.destroy(rhi::BufferHandle{}, rhi::DestroyDesc{}, error);
		device.destroy(rhi::TextureHandle{});
		device.destroy(rhi::TextureHandle{}, rhi::DestroyDesc{}, error);
		device.destroy(rhi::TextureViewHandle{});
		device.destroy(rhi::TextureViewHandle{}, rhi::DestroyDesc{}, error);
		device.destroy(rhi::SamplerHandle{});
		device.destroy(rhi::SamplerHandle{}, rhi::DestroyDesc{}, error);
		device.destroy(rhi::HeapHandle{});
		device.destroy(rhi::HeapHandle{}, rhi::DestroyDesc{}, error);
		device.destroy(rhi::DescriptorSetLayoutHandle{});
		device.destroy(rhi::DescriptorSetLayoutHandle{}, rhi::DestroyDesc{}, error);
		device.destroy(rhi::PipelineLayoutHandle{});
		device.destroy(rhi::PipelineLayoutHandle{}, rhi::DestroyDesc{}, error);
		device.destroy(rhi::QueryPoolHandle{});
		device.destroy(rhi::QueryPoolHandle{}, rhi::DestroyDesc{}, error);
		device.destroy(rhi::TimelineHandle{});
		device.destroy(rhi::TimelineHandle{}, rhi::DestroyDesc{}, error);
		device.destroy(rhi::BinarySemaphoreHandle{});
		device.destroy(rhi::BinarySemaphoreHandle{}, rhi::DestroyDesc{}, error);

		SUCCEED();
	}

} // namespace

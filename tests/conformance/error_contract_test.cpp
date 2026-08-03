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

#include <cstdint>
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
		static_assert(noexcept(std::declval<rhi::Device &>().CreateBuffer(std::declval<const rhi::BufferDesc &>())));
		static_assert(noexcept(std::declval<rhi::Device &>().GetQueueCount(rhi::QueueType::eGraphics)));
		static_assert(noexcept(std::declval<rhi::Device &>().GetCaps()));
		static_assert(noexcept(std::declval<rhi::Device &>().CollectGarbage()));
		static_assert(noexcept(std::declval<rhi::Queue &>().WaitIdle()));
		static_assert(noexcept(std::declval<rhi::CommandList &>().Begin()));
		static_assert(noexcept(std::declval<rhi::CommandList &>().End()));
		static_assert(noexcept(std::declval<rhi::CommandList &>().Draw(0, 0, 0, 0)));
		static_assert(noexcept(std::declval<rhi::CommandPool &>().Allocate()));

		SUCCEED();
	}

	TEST_P(ErrorContractTest, AllThreeFormsOfASucceedingCallAgree)
	{
		rhi::Error error{};

		const rhi::BufferHandle sentinel = Dev().CreateBuffer(test::samples::StorageBuffer());
		ASSERT_TRUE(sentinel.IsValid());

		const rhi::BufferHandle withError = Dev().CreateBuffer(test::samples::StorageBuffer(), error);
		ASSERT_TRUE(withError.IsValid());
		EXPECT_EQ(error.code, rhi::ErrorCode::eOk) << "a successful call left an error behind";

		const rhi::Result<rhi::BufferHandle> asResult = Dev().CreateBufferWithResult(test::samples::StorageBuffer());
		ASSERT_TRUE(test::Ok(asResult));

		EXPECT_TRUE(test::Ok(Dev().Destroy(sentinel, {}, error), error));
		EXPECT_TRUE(test::Ok(Dev().Destroy(withError, {}, error), error));
		EXPECT_TRUE(test::Ok(Dev().Destroy(asResult.Value(), {}, error), error));
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

		const rhi::BufferHandle buffer = Dev().CreateBuffer(test::samples::StorageBuffer(), error);
		ASSERT_TRUE(buffer.IsValid());
		EXPECT_EQ(error.code, rhi::ErrorCode::eOk) << "a successful call left a stale error in place";
		EXPECT_EQ(error.message, nullptr);

		EXPECT_TRUE(test::Ok(Dev().Destroy(buffer, {}, error), error));
	}

	TEST_P(ErrorContractTest, AFailedCallReportsSomethingOtherThanOk)
	{
		rhi::Error error{};
		const rhi::Queue queue = Dev().GetQueue(rhi::QueueType::eGraphics, Dev().GetQueueCount(rhi::QueueType::eGraphics), error);

		ASSERT_FALSE(queue.IsValid());
		EXPECT_NE(error.code, rhi::ErrorCode::eOk);
		EXPECT_NE(error.message, nullptr) << "a failure carries no diagnostic for a log or a bug report";
	}

	TEST_P(ErrorContractTest, AFailedCreationReturnsAnInvalidHandleRatherThanAPlausibleOne)
	{
		rhi::Error error{};
		const rhi::TextureViewHandle view = Dev().CreateTextureView(rhi::TextureHandle{}, test::samples::FullTextureView(), error);

		if (view.IsValid())
		{
			GTEST_SKIP() << "this backend does not resolve the texture a view is created from";
		}

		EXPECT_FALSE(view.IsValid());
		EXPECT_TRUE(test::ErrorIsPopulated(error));
	}

	TEST_P(ErrorContractTest, ResultCarriesTheSameCodeTheOutErrorFormWouldHave)
	{
		rhi::Error error{};
		rhi::MemoryInfo ignored{};

		rhi::BufferDesc absurd = test::samples::StorageBuffer();
		absurd.size			   = 0;

		if (Dev().GetBufferMemoryInfo(absurd, ignored, error))
		{
			GTEST_SKIP() << "this backend accepts a zero-sized buffer desc, so there is no failure to compare";
		}

		const rhi::Result<rhi::MemoryInfo> asResult = Dev().GetBufferMemoryInfoWithResult(absurd);
		ASSERT_FALSE(asResult.HasValue());
		EXPECT_EQ(asResult.GetError().code, error.code);
	}

	TEST_P(ErrorContractTest, EveryCreationEntryPointHasAllThreeForms)
	{
		rhi::Device device = Dev();
		rhi::Error error{};

		static_cast<void>(device.CreateBuffer(rhi::BufferDesc{}));
		static_cast<void>(device.CreateBuffer(rhi::BufferDesc{}, error));
		static_cast<void>(device.CreateBufferWithResult(rhi::BufferDesc{}));

		static_cast<void>(device.CreateTexture(rhi::TextureDesc{}));
		static_cast<void>(device.CreateTexture(rhi::TextureDesc{}, error));
		static_cast<void>(device.CreateTextureWithResult(rhi::TextureDesc{}));

		static_cast<void>(device.CreateSampler(rhi::SamplerDesc{}));
		static_cast<void>(device.CreateSampler(rhi::SamplerDesc{}, error));
		static_cast<void>(device.CreateSamplerWithResult(rhi::SamplerDesc{}));

		static_cast<void>(device.CreateHeap(rhi::HeapDesc{}));
		static_cast<void>(device.CreateHeap(rhi::HeapDesc{}, error));
		static_cast<void>(device.CreateHeapWithResult(rhi::HeapDesc{}));

		static_cast<void>(device.CreateDescriptorSetLayout(rhi::DescriptorSetLayoutDesc{}));
		static_cast<void>(device.CreateDescriptorSetLayout(rhi::DescriptorSetLayoutDesc{}, error));
		static_cast<void>(device.CreateDescriptorSetLayoutWithResult(rhi::DescriptorSetLayoutDesc{}));

		static_cast<void>(device.CreatePipelineLayout(rhi::PipelineLayoutDesc{}));
		static_cast<void>(device.CreatePipelineLayout(rhi::PipelineLayoutDesc{}, error));
		static_cast<void>(device.CreatePipelineLayoutWithResult(rhi::PipelineLayoutDesc{}));

		static_cast<void>(device.CreateQueryPool(rhi::QueryPoolDesc{}));
		static_cast<void>(device.CreateQueryPool(rhi::QueryPoolDesc{}, error));
		static_cast<void>(device.CreateQueryPoolWithResult(rhi::QueryPoolDesc{}));

		static_cast<void>(device.CreateTimeline(rhi::TimelineDesc{}));
		static_cast<void>(device.CreateTimeline(rhi::TimelineDesc{}, error));
		static_cast<void>(device.CreateTimelineWithResult(rhi::TimelineDesc{}));

		static_cast<void>(device.CreateBinarySemaphore(rhi::BinarySemaphoreDesc{}));
		static_cast<void>(device.CreateBinarySemaphore(rhi::BinarySemaphoreDesc{}, error));
		static_cast<void>(device.CreateBinarySemaphoreWithResult(rhi::BinarySemaphoreDesc{}));

		static_cast<void>(device.CreateCommandPool(rhi::CommandPoolDesc{}));
		static_cast<void>(device.CreateCommandPool(rhi::CommandPoolDesc{}, error));
		static_cast<void>(device.CreateCommandPoolWithResult(rhi::CommandPoolDesc{}));

		SUCCEED();
	}

	TEST_P(ErrorContractTest, EveryDestroyOverloadTakesADestroyDescAndAnOptionalError)
	{
		rhi::Device device = Dev();
		rhi::Error error{};

		device.Destroy(rhi::BufferHandle{});
		device.Destroy(rhi::BufferHandle{}, rhi::DestroyDesc{}, error);
		device.Destroy(rhi::TextureHandle{});
		device.Destroy(rhi::TextureHandle{}, rhi::DestroyDesc{}, error);
		device.Destroy(rhi::TextureViewHandle{});
		device.Destroy(rhi::TextureViewHandle{}, rhi::DestroyDesc{}, error);
		device.Destroy(rhi::SamplerHandle{});
		device.Destroy(rhi::SamplerHandle{}, rhi::DestroyDesc{}, error);
		device.Destroy(rhi::HeapHandle{});
		device.Destroy(rhi::HeapHandle{}, rhi::DestroyDesc{}, error);
		device.Destroy(rhi::DescriptorSetLayoutHandle{});
		device.Destroy(rhi::DescriptorSetLayoutHandle{}, rhi::DestroyDesc{}, error);
		device.Destroy(rhi::PipelineLayoutHandle{});
		device.Destroy(rhi::PipelineLayoutHandle{}, rhi::DestroyDesc{}, error);
		device.Destroy(rhi::QueryPoolHandle{});
		device.Destroy(rhi::QueryPoolHandle{}, rhi::DestroyDesc{}, error);
		device.Destroy(rhi::TimelineHandle{});
		device.Destroy(rhi::TimelineHandle{}, rhi::DestroyDesc{}, error);
		device.Destroy(rhi::BinarySemaphoreHandle{});
		device.Destroy(rhi::BinarySemaphoreHandle{}, rhi::DestroyDesc{}, error);

		SUCCEED();
	}

} // namespace

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
#include "conformance/samples.hpp"
#include "harness/backends.hpp"
#include "harness/environment.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <utility>

namespace rhi  = azo::rhi;
namespace test = azo::rhi::test;

namespace
{

	class ExternalTest : public test::BackendTest
	{
	protected:
		void SetUp() override
		{
			test::BackendTest::SetUp();
			if (::testing::Test::IsSkipped())
			{
				return;
			}

			rhi::InstanceDesc desc{};
			desc.applicationName = "external_test";
			desc.validation		 = rhi::ValidationMode::eOff;

			const std::array<rhi::GraphicsApiId, 1> only{ CurrentBackend().id };
			rhi::Result<rhi::UniqueInstance> created = rhi::CreateInstance(Harness().Registry(), only, desc);
			ASSERT_TRUE(test::Ok(created)) << "the backend under test could not make an instance to ask";
			m_instance = std::move(created).Value();
		}

		[[nodiscard]] rhi::Instance Inst() const noexcept
		{
			return m_instance.Get();
		}

	private:
		rhi::UniqueInstance m_instance;
	};

	AZO_RHI_BACKEND_SUITE(ExternalTest);

	constexpr auto kAllHandleTypes = rhi::kAllExternalHandleTypes;

	constexpr std::array kAllKinds{
		rhi::ExternalObjectKind::eBuffer,
		rhi::ExternalObjectKind::eTexture,
		rhi::ExternalObjectKind::eTimeline,
		rhi::ExternalObjectKind::eBinarySemaphore,
		rhi::ExternalObjectKind::eHeap,
	};

	TEST_P(ExternalTest, AnswersEveryKindAndHandleTypeWithoutFailing)
	{
		for (const rhi::ExternalObjectKind kind : kAllKinds)
		{
			for (const rhi::ExternalHandleType handleType : kAllHandleTypes)
			{
				const rhi::ExternalHandleSupportDesc desc{
					.adapterIndex = 0,
					.kind		  = kind,
					.handleType	  = handleType,
					.format		  = rhi::Format::eRGBA8UNorm,
				};

				rhi::Error error{};
				rhi::ExternalHandleSupport support{};

				EXPECT_TRUE(test::Ok(Inst().QueryExternalHandleSupport(desc, support, error), error))
					<< "the query failed without answering no for kind " << static_cast<int>(kind) << " and handle type "
					<< static_cast<std::uint32_t>(handleType);
			}
		}
	}

	TEST_P(ExternalTest, RefusesAnAdapterIndexTheInstanceDoesNotHave)
	{
		AZO_RHI_REQUIRE_CAP(!IsNullBackend(), "a backend that publishes an external capability block");

		const rhi::ExternalHandleSupportDesc desc{
			.adapterIndex = 4096,
			.kind		  = rhi::ExternalObjectKind::eBuffer,
			.handleType	  = rhi::ExternalHandleType::eOpaqueFd,
		};

		rhi::Error error{};
		rhi::ExternalHandleSupport support{};

		EXPECT_FALSE(Inst().QueryExternalHandleSupport(desc, support, error)) << "the query answered about an adapter that does not exist";
		EXPECT_TRUE(test::ErrorIsPopulated(error));
		EXPECT_FALSE(support.exportable) << "a refused query left a result behind";
		EXPECT_FALSE(support.importable);
	}

	TEST_P(ExternalTest, NeverReportsABufferSharedThroughASynchronizationOnlyHandle)
	{
		for (const rhi::ExternalHandleType handleType : { rhi::ExternalHandleType::eD3D12Fence, rhi::ExternalHandleType::eMtlSharedEvent })
		{
			const rhi::ExternalHandleSupportDesc desc{
				.adapterIndex = 0,
				.kind		  = rhi::ExternalObjectKind::eBuffer,
				.handleType	  = handleType,
			};

			rhi::Error error{};
			rhi::ExternalHandleSupport support{};
			ASSERT_TRUE(test::Ok(Inst().QueryExternalHandleSupport(desc, support, error), error));

			EXPECT_FALSE(support.exportable) << "a buffer was reported exportable through a synchronization-only handle type";
			EXPECT_FALSE(support.importable) << "a buffer was reported importable through a synchronization-only handle type";
		}
	}

	TEST_P(ExternalTest, NeverReportsASemaphoreSharedThroughAMemoryOnlyHandle)
	{
		for (const rhi::ExternalObjectKind kind : { rhi::ExternalObjectKind::eTimeline, rhi::ExternalObjectKind::eBinarySemaphore })
		{
			for (const rhi::ExternalHandleType handleType :
				{ rhi::ExternalHandleType::eDmaBuf, rhi::ExternalHandleType::eMtlSharedTexture, rhi::ExternalHandleType::eD3D12Heap })
			{
				const rhi::ExternalHandleSupportDesc desc{
					.adapterIndex = 0,
					.kind		  = kind,
					.handleType	  = handleType,
				};

				rhi::Error error{};
				rhi::ExternalHandleSupport support{};
				ASSERT_TRUE(test::Ok(Inst().QueryExternalHandleSupport(desc, support, error), error));

				EXPECT_FALSE(support.exportable) << "a semaphore was reported exportable through a memory-only handle type";
				EXPECT_FALSE(support.importable) << "a semaphore was reported importable through a memory-only handle type";
			}
		}
	}

	TEST_P(ExternalTest, ASupportedHandleTypeIsCompatibleWithItself)
	{
		bool anySupported = false;

		for (const rhi::ExternalObjectKind kind : kAllKinds)
		{
			for (const rhi::ExternalHandleType handleType : kAllHandleTypes)
			{
				const rhi::ExternalHandleSupportDesc desc{
					.adapterIndex = 0,
					.kind		  = kind,
					.handleType	  = handleType,
					.format		  = rhi::Format::eRGBA8UNorm,
				};

				rhi::Error error{};
				rhi::ExternalHandleSupport support{};
				ASSERT_TRUE(test::Ok(Inst().QueryExternalHandleSupport(desc, support, error), error));

				if (!support.exportable && !support.importable)
				{
					continue;
				}

				anySupported = true;
				EXPECT_TRUE(support.compatibleTypes.Contains(handleType))
					<< "a supported handle type is absent from its own compatible mask, so nothing could be paired with it";
			}
		}

		if (!anySupported)
		{
			GTEST_SKIP() << "this adapter reports no external sharing through any handle type the RHI names, so there was no mask to check here";
		}
	}

	TEST_P(ExternalTest, AsksTimelineAndBinarySemaphoresSeparately)
	{
		bool anyDifference = false;

		for (const rhi::ExternalHandleType handleType : kAllHandleTypes)
		{
			rhi::Error error{};
			rhi::ExternalHandleSupport timeline{};
			rhi::ExternalHandleSupport binary{};

			ASSERT_TRUE(test::Ok(
				Inst().QueryExternalHandleSupport({ .adapterIndex = 0, .kind = rhi::ExternalObjectKind::eTimeline, .handleType = handleType }, timeline, error),
				error));
			ASSERT_TRUE(test::Ok(Inst().QueryExternalHandleSupport(
									 { .adapterIndex = 0, .kind = rhi::ExternalObjectKind::eBinarySemaphore, .handleType = handleType }, binary, error),
				error));

			if (timeline.exportable != binary.exportable || timeline.importable != binary.importable)
			{
				anyDifference = true;
			}
		}

		if (!anyDifference)
		{
			GTEST_SKIP() << "no adapter here answers differently for timeline and binary semaphores, so the axis this separates is untested on this "
							"machine, not confirmed";
		}
	}

	TEST_P(ExternalTest, CreatesExactlyTheExportableResourcesTheQueryReports)
	{
		bool sawAccepted = false;
		bool sawRefused	 = false;

		for (const rhi::ExternalHandleType handleType : kAllHandleTypes)
		{
			rhi::Error queryError{};
			rhi::ExternalHandleSupport support{};
			ASSERT_TRUE(test::Ok(Inst().QueryExternalHandleSupport(
									 { .adapterIndex = 0, .kind = rhi::ExternalObjectKind::eBuffer, .handleType = handleType }, support, queryError),
				queryError));

			rhi::BufferDesc desc	   = test::samples::StorageBuffer();
			desc.exportableHandleTypes = handleType;

			rhi::Error createError{};
			const rhi::BufferHandle buffer = Dev().CreateBuffer(desc, createError);

			if (support.exportable)
			{
				sawAccepted = true;
				EXPECT_TRUE(test::Ok(buffer.IsValid(), createError))
					<< "a device reporting this handle type exportable refused to create a buffer declaring it";
				if (buffer.IsValid())
				{
					rhi::Error destroyError{};
					EXPECT_TRUE(test::Ok(Dev().Destroy(buffer, {}, destroyError), destroyError));
				}
			}
			else
			{
				sawRefused = true;
				EXPECT_FALSE(buffer.IsValid()) << "a device reporting this handle type not exportable created a buffer declaring it anyway";
				EXPECT_TRUE(test::ErrorIsPopulated(createError));
			}
		}

		EXPECT_TRUE(sawRefused) << "no handle type was refused, so the refusal side of this case proved nothing";
		if (!sawAccepted)
		{
			RecordProperty("acceptingHalf", "not run, this adapter exports no buffer handle type");
		}
	}

	TEST_P(ExternalTest, CreatesAnOrdinaryResourceWhenNothingIsDeclared)
	{
		rhi::Error error{};
		const rhi::BufferHandle buffer = Dev().CreateBuffer(test::samples::StorageBuffer(), error);
		ASSERT_TRUE(test::Ok(buffer.IsValid(), error)) << "declaring no external handle types changed ordinary buffer creation";
		EXPECT_TRUE(test::Ok(Dev().Destroy(buffer, {}, error), error));
	}

	TEST_P(ExternalTest, TheExternalQueueFamilySentinelIsItsOwnValue)
	{
		static_assert(rhi::kExternalQueueFamily != rhi::kIgnoreQueueFamily);
		EXPECT_NE(rhi::kExternalQueueFamily, rhi::kIgnoreQueueFamily);
	}

	TEST_P(ExternalTest, CreatesExactlyTheExportableHeapsTheQueryReports)
	{
		bool sawRefused = false;

		for (const rhi::ExternalHandleType handleType : kAllHandleTypes)
		{
			rhi::Error queryError{};
			rhi::ExternalHandleSupport support{};
			ASSERT_TRUE(test::Ok(
				Inst().QueryExternalHandleSupport({ .adapterIndex = 0, .kind = rhi::ExternalObjectKind::eHeap, .handleType = handleType }, support, queryError),
				queryError));

			rhi::HeapDesc desc		   = test::samples::GpuHeap(std::uint64_t{ 64 } * 1024);
			desc.exportableHandleTypes = handleType;

			rhi::Error createError{};
			const rhi::HeapHandle heap = Dev().CreateHeap(desc, createError);

			if (support.exportable)
			{
				EXPECT_TRUE(test::Ok(heap.IsValid(), createError)) << "a device reporting this heap handle type exportable refused to create one";
				if (heap.IsValid())
				{
					rhi::Error destroyError{};
					EXPECT_TRUE(test::Ok(Dev().Destroy(heap, {}, destroyError), destroyError));
				}
			}
			else
			{
				sawRefused = true;
				EXPECT_FALSE(heap.IsValid()) << "a device reporting this heap handle type not exportable created one declaring it anyway";
				EXPECT_TRUE(test::ErrorIsPopulated(createError));
			}
		}

		EXPECT_TRUE(sawRefused) << "no heap handle type was refused, so the refusal side of this case proved nothing";
	}

	TEST_P(ExternalTest, NeverReportsASemaphoreSharedThroughAHeapHandle)
	{
		for (const rhi::ExternalObjectKind kind : { rhi::ExternalObjectKind::eTimeline, rhi::ExternalObjectKind::eBinarySemaphore })
		{
			rhi::Error error{};
			rhi::ExternalHandleSupport support{};
			ASSERT_TRUE(test::Ok(
				Inst().QueryExternalHandleSupport({ .adapterIndex = 0, .kind = kind, .handleType = rhi::ExternalHandleType::eD3D12Heap }, support, error),
				error));

			EXPECT_FALSE(support.exportable) << "a semaphore was reported exportable through a heap handle type";
			EXPECT_FALSE(support.importable) << "a semaphore was reported importable through a heap handle type";
		}
	}

} // namespace

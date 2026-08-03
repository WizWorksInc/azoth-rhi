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

#include "conformance/overload_contract.hpp"

#include "azoth/rhi/commands/command.hpp"
#include "azoth/rhi/commands/sync.hpp"
#include "azoth/rhi/present/swapchain.hpp"
#include "azoth/rhi/resources/descriptors.hpp"
#include "azoth/rhi/resources/pipeline.hpp"
#include "azoth/rhi/resources/query.hpp"
#include "azoth/rhi/resources/resources.hpp"

#include "conformance/matchers.hpp"
#include "conformance/samples.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

namespace azo::rhi::test::oracle
{
	namespace
	{

		template <typename Plain, typename Errored, typename Resulted>
		[[nodiscard]] OverloadReport Probe(const CString operation, Plain plain, Errored errored, Resulted resulted)
		{
			OverloadReport report{};
			report.operation = operation;

			report.plainSucceeded = plain();

			Error error{};
			report.erroredSucceeded = errored(error);
			report.erroredCode		= error.code;

			const auto asResult	   = resulted();
			report.resultSucceeded = asResult.HasValue();
			report.resultCode	   = asResult.HasValue() ? ErrorCode::eOk : asResult.GetError().code;

			return report;
		}

		template <typename Handle>
		[[nodiscard]] bool CreatedAndReleased(Device device, Handle handle)
		{
			if (!handle.IsValid())
			{
				return false;
			}

			Error ignored{};
			static_cast<void>(device.Destroy(handle, {}, ignored));
			return true;
		}

	} // namespace

	std::vector<OverloadReport> ProbeEveryOverload(Device device)
	{
		std::vector<OverloadReport> reports;
		reports.reserve(32);

		const DeviceCaps & caps = device.GetCaps();

		reports.push_back(Probe(
			"Device::CreateTextureView",
			[&]
			{
				return device.CreateTextureView(TextureHandle{}, samples::FullTextureView()).IsValid();
			},
			[&](Error & error)
			{
				return device.CreateTextureView(TextureHandle{}, samples::FullTextureView(), error).IsValid();
			},
			[&]
			{
				return device.CreateTextureViewWithResult(TextureHandle{}, samples::FullTextureView());
			}));

		reports.push_back(Probe(
			"Device::Map",
			[&]
			{
				return device.Map(BufferHandle{}, MapDesc{}).data != nullptr;
			},
			[&](Error & error)
			{
				return device.Map(BufferHandle{}, MapDesc{}, error).data != nullptr;
			},
			[&]
			{
				return device.MapWithResult(BufferHandle{}, MapDesc{});
			}));

		reports.push_back(Probe(
			"Device::GetQueue",
			[&]
			{
				return device.GetQueue(QueueType::eGraphics, device.GetQueueCount(QueueType::eGraphics)).IsValid();
			},
			[&](Error & error)
			{
				return device.GetQueue(QueueType::eGraphics, device.GetQueueCount(QueueType::eGraphics), error).IsValid();
			},
			[&]
			{
				return device.GetQueueWithResult(QueueType::eGraphics, device.GetQueueCount(QueueType::eGraphics));
			}));

		{
			PlacedBufferDesc placed{};
			placed.buffer = samples::StorageBuffer();
			reports.push_back(Probe(
				"Device::CreatePlacedBuffer",
				[&]
				{
					return device.CreatePlacedBuffer(placed).IsValid();
				},
				[&](Error & error)
				{
					return device.CreatePlacedBuffer(placed, error).IsValid();
				},
				[&]
				{
					return device.CreatePlacedBufferWithResult(placed);
				}));

			PlacedTextureDesc placedTexture{};
			placedTexture.texture = samples::SampledTexture2D();
			reports.push_back(Probe(
				"Device::CreatePlacedTexture",
				[&]
				{
					return device.CreatePlacedTexture(placedTexture).IsValid();
				},
				[&](Error & error)
				{
					return device.CreatePlacedTexture(placedTexture, error).IsValid();
				},
				[&]
				{
					return device.CreatePlacedTextureWithResult(placedTexture);
				}));
		}

		reports.push_back(Probe(
			"Device::CreateBuffer",
			[&]
			{
				return CreatedAndReleased(device, device.CreateBuffer(samples::StorageBuffer()));
			},
			[&](Error & error)
			{
				return CreatedAndReleased(device, device.CreateBuffer(samples::StorageBuffer(), error));
			},
			[&]
			{
				Result<BufferHandle> result = device.CreateBufferWithResult(samples::StorageBuffer());
				if (result.HasValue())
				{
					static_cast<void>(CreatedAndReleased(device, result.Value()));
				}
				return result;
			}));

		reports.push_back(Probe(
			"Device::CreateTexture",
			[&]
			{
				return CreatedAndReleased(device, device.CreateTexture(samples::SampledTexture2D()));
			},
			[&](Error & error)
			{
				return CreatedAndReleased(device, device.CreateTexture(samples::SampledTexture2D(), error));
			},
			[&]
			{
				Result<TextureHandle> result = device.CreateTextureWithResult(samples::SampledTexture2D());
				if (result.HasValue())
				{
					static_cast<void>(CreatedAndReleased(device, result.Value()));
				}
				return result;
			}));

		reports.push_back(Probe(
			"Device::CreateSampler",
			[&]
			{
				return CreatedAndReleased(device, device.CreateSampler(samples::LinearSampler()));
			},
			[&](Error & error)
			{
				return CreatedAndReleased(device, device.CreateSampler(samples::LinearSampler(), error));
			},
			[&]
			{
				Result<SamplerHandle> result = device.CreateSamplerWithResult(samples::LinearSampler());
				if (result.HasValue())
				{
					static_cast<void>(CreatedAndReleased(device, result.Value()));
				}
				return result;
			}));

		reports.push_back(Probe(
			"Device::CreateHeap",
			[&]
			{
				return CreatedAndReleased(device, device.CreateHeap(samples::GpuHeap()));
			},
			[&](Error & error)
			{
				return CreatedAndReleased(device, device.CreateHeap(samples::GpuHeap(), error));
			},
			[&]
			{
				Result<HeapHandle> result = device.CreateHeapWithResult(samples::GpuHeap());
				if (result.HasValue())
				{
					static_cast<void>(CreatedAndReleased(device, result.Value()));
				}
				return result;
			}));

		{
			const samples::UniformLayout layout{};
			reports.push_back(Probe(
				"Device::CreateDescriptorSetLayout",
				[&]
				{
					return CreatedAndReleased(device, device.CreateDescriptorSetLayout(layout.Desc()));
				},
				[&](Error & error)
				{
					return CreatedAndReleased(device, device.CreateDescriptorSetLayout(layout.Desc(), error));
				},
				[&]
				{
					Result<DescriptorSetLayoutHandle> result = device.CreateDescriptorSetLayoutWithResult(layout.Desc());
					if (result.HasValue())
					{
						static_cast<void>(CreatedAndReleased(device, result.Value()));
					}
					return result;
				}));

			Error setupError{};
			const DescriptorSetLayoutHandle setLayout = device.CreateDescriptorSetLayout(layout.Desc(), setupError);
			const samples::SimplePipelineLayout pipelineLayout{ setLayout };
			reports.push_back(Probe(
				"Device::CreatePipelineLayout",
				[&]
				{
					return CreatedAndReleased(device, device.CreatePipelineLayout(pipelineLayout.Desc()));
				},
				[&](Error & error)
				{
					return CreatedAndReleased(device, device.CreatePipelineLayout(pipelineLayout.Desc(), error));
				},
				[&]
				{
					Result<PipelineLayoutHandle> result = device.CreatePipelineLayoutWithResult(pipelineLayout.Desc());
					if (result.HasValue())
					{
						static_cast<void>(CreatedAndReleased(device, result.Value()));
					}
					return result;
				}));

			static_cast<void>(CreatedAndReleased(device, setLayout));
		}

		reports.push_back(Probe(
			"Device::CreateTimeline",
			[&]
			{
				return CreatedAndReleased(device, device.CreateTimeline(samples::Timeline()));
			},
			[&](Error & error)
			{
				return CreatedAndReleased(device, device.CreateTimeline(samples::Timeline(), error));
			},
			[&]
			{
				Result<TimelineHandle> result = device.CreateTimelineWithResult(samples::Timeline());
				if (result.HasValue())
				{
					static_cast<void>(CreatedAndReleased(device, result.Value()));
				}
				return result;
			}));

		reports.push_back(Probe(
			"Device::CreateBinarySemaphore",
			[&]
			{
				return CreatedAndReleased(device, device.CreateBinarySemaphore(BinarySemaphoreDesc{}));
			},
			[&](Error & error)
			{
				return CreatedAndReleased(device, device.CreateBinarySemaphore(BinarySemaphoreDesc{}, error));
			},
			[&]
			{
				Result<BinarySemaphoreHandle> result = device.CreateBinarySemaphoreWithResult(BinarySemaphoreDesc{});
				if (result.HasValue())
				{
					static_cast<void>(CreatedAndReleased(device, result.Value()));
				}
				return result;
			}));

		reports.push_back(Probe(
			"Device::CreateQueryPool",
			[&]
			{
				return CreatedAndReleased(device, device.CreateQueryPool(samples::TimestampPool()));
			},
			[&](Error & error)
			{
				return CreatedAndReleased(device, device.CreateQueryPool(samples::TimestampPool(), error));
			},
			[&]
			{
				Result<QueryPoolHandle> result = device.CreateQueryPoolWithResult(samples::TimestampPool());
				if (result.HasValue())
				{
					static_cast<void>(CreatedAndReleased(device, result.Value()));
				}
				return result;
			}));

		reports.push_back(Probe(
			"Device::CreateCommandPool",
			[&]
			{
				return device.CreateCommandPool(samples::CommandPool()).IsValid();
			},
			[&](Error & error)
			{
				return device.CreateCommandPool(samples::CommandPool(), error).IsValid();
			},
			[&]
			{
				return device.CreateCommandPoolWithResult(samples::CommandPool());
			}));

		reports.push_back(Probe(
			"Device::CreateDescriptorArena",
			[&]
			{
				return device.CreateDescriptorArena(samples::DescriptorArena()).IsValid();
			},
			[&](Error & error)
			{
				return device.CreateDescriptorArena(samples::DescriptorArena(), error).IsValid();
			},
			[&]
			{
				return device.CreateDescriptorArenaWithResult(samples::DescriptorArena());
			}));

		reports.push_back(Probe(
			"Device::GetBufferMemoryInfo",
			[&]
			{
				MemoryInfo out{};
				return device.GetBufferMemoryInfo(samples::StorageBuffer(), out);
			},
			[&](Error & error)
			{
				MemoryInfo out{};
				return device.GetBufferMemoryInfo(samples::StorageBuffer(), out, error);
			},
			[&]
			{
				return device.GetBufferMemoryInfoWithResult(samples::StorageBuffer());
			}));

		reports.push_back(Probe(
			"Device::GetTextureMemoryInfo",
			[&]
			{
				MemoryInfo out{};
				return device.GetTextureMemoryInfo(samples::SampledTexture2D(), out);
			},
			[&](Error & error)
			{
				MemoryInfo out{};
				return device.GetTextureMemoryInfo(samples::SampledTexture2D(), out, error);
			},
			[&]
			{
				return device.GetTextureMemoryInfoWithResult(samples::SampledTexture2D());
			}));

		reports.push_back(Probe(
			"Device::QueryMemoryBudget",
			[&]
			{
				MemoryBudgetInfo out{};
				return device.QueryMemoryBudget(HeapType::eGpuLocal, out);
			},
			[&](Error & error)
			{
				MemoryBudgetInfo out{};
				return device.QueryMemoryBudget(HeapType::eGpuLocal, out, error);
			},
			[&]
			{
				return device.QueryMemoryBudgetWithResult(HeapType::eGpuLocal);
			}));

		reports.push_back(Probe(
			"Device::CalibrateTimestamp",
			[&]
			{
				TimestampCalibration out{};
				return device.CalibrateTimestamp(QueueType::eGraphics, out);
			},
			[&](Error & error)
			{
				TimestampCalibration out{};
				return device.CalibrateTimestamp(QueueType::eGraphics, out, error);
			},
			[&]
			{
				return device.CalibrateTimestampWithResult(QueueType::eGraphics);
			}));

		reports.push_back(Probe(
			"Device::GetPipelineCacheData",
			[&]
			{
				PipelineCacheData out{};
				return device.GetPipelineCacheData(PipelineCacheHandle{}, out);
			},
			[&](Error & error)
			{
				PipelineCacheData out{};
				return device.GetPipelineCacheData(PipelineCacheHandle{}, out, error);
			},
			[&]
			{
				return device.GetPipelineCacheDataWithResult(PipelineCacheHandle{});
			}));

		if (caps.supportsPipelineCache)
		{
			reports.push_back(Probe(
				"Device::CreatePipelineCache",
				[&]
				{
					return CreatedAndReleased(device, device.CreatePipelineCache(PipelineCacheDesc{}));
				},
				[&](Error & error)
				{
					return CreatedAndReleased(device, device.CreatePipelineCache(PipelineCacheDesc{}, error));
				},
				[&]
				{
					Result<PipelineCacheHandle> result = device.CreatePipelineCacheWithResult(PipelineCacheDesc{});
					if (result.HasValue())
					{
						static_cast<void>(CreatedAndReleased(device, result.Value()));
					}
					return result;
				}));
		}

		reports.push_back(Probe(
			"Device::CreateGraphicsPipeline",
			[&]
			{
				return CreatedAndReleased(device, device.CreateGraphicsPipeline(GraphicsPipelineDesc{}));
			},
			[&](Error & error)
			{
				return CreatedAndReleased(device, device.CreateGraphicsPipeline(GraphicsPipelineDesc{}, error));
			},
			[&]
			{
				Result<GraphicsPipelineHandle> result = device.CreateGraphicsPipelineWithResult(GraphicsPipelineDesc{});
				if (result.HasValue())
				{
					static_cast<void>(CreatedAndReleased(device, result.Value()));
				}
				return result;
			}));

		reports.push_back(Probe(
			"Device::CreateComputePipeline",
			[&]
			{
				return CreatedAndReleased(device, device.CreateComputePipeline(ComputePipelineDesc{}));
			},
			[&](Error & error)
			{
				return CreatedAndReleased(device, device.CreateComputePipeline(ComputePipelineDesc{}, error));
			},
			[&]
			{
				Result<ComputePipelineHandle> result = device.CreateComputePipelineWithResult(ComputePipelineDesc{});
				if (result.HasValue())
				{
					static_cast<void>(CreatedAndReleased(device, result.Value()));
				}
				return result;
			}));

		reports.push_back(Probe(
			"Device::CreateRayTracingPipeline",
			[&]
			{
				return CreatedAndReleased(device, device.CreateRayTracingPipeline(RayTracingPipelineDesc{}));
			},
			[&](Error & error)
			{
				return CreatedAndReleased(device, device.CreateRayTracingPipeline(RayTracingPipelineDesc{}, error));
			},
			[&]
			{
				Result<RayTracingPipelineHandle> result = device.CreateRayTracingPipelineWithResult(RayTracingPipelineDesc{});
				if (result.HasValue())
				{
					static_cast<void>(CreatedAndReleased(device, result.Value()));
				}
				return result;
			}));

		reports.push_back(Probe(
			"Device::CreateAccelerationStructure",
			[&]
			{
				return CreatedAndReleased(device, device.CreateAccelerationStructure(AccelerationStructureDesc{}));
			},
			[&](Error & error)
			{
				return CreatedAndReleased(device, device.CreateAccelerationStructure(AccelerationStructureDesc{}, error));
			},
			[&]
			{
				Result<AccelerationStructureHandle> result = device.CreateAccelerationStructureWithResult(AccelerationStructureDesc{});
				if (result.HasValue())
				{
					static_cast<void>(CreatedAndReleased(device, result.Value()));
				}
				return result;
			}));

		reports.push_back(Probe(
			"Device::CreateSwapchain",
			[&]
			{
				return device.CreateSwapchain(SwapchainDesc{}).IsValid();
			},
			[&](Error & error)
			{
				return device.CreateSwapchain(SwapchainDesc{}, error).IsValid();
			},
			[&]
			{
				return device.CreateSwapchainWithResult(SwapchainDesc{});
			}));

		{
			Error error{};
			CommandPool pool = device.CreateCommandPool(samples::CommandPool(), error);
			if (pool.IsValid())
			{
				reports.push_back(Probe(
					"CommandPool::Allocate",
					[&]
					{
						return pool.Allocate("azoth.rhi.conformance.overloadPlain").IsValid();
					},
					[&](Error & poolError)
					{
						return pool.Allocate("azoth.rhi.conformance.overloadErrored", poolError).IsValid();
					},
					[&]
					{
						return pool.AllocateWithResult("azoth.rhi.conformance.overloadResult");
					}));
			}

			DescriptorArena arena = device.CreateDescriptorArena(samples::DescriptorArena(), error);
			if (arena.IsValid())
			{
				DescriptorSetAllocDesc alloc{};
				reports.push_back(Probe(
					"DescriptorArena::Allocate",
					[&]
					{
						return arena.Allocate(alloc).IsValid();
					},
					[&](Error & arenaError)
					{
						return arena.Allocate(alloc, arenaError).IsValid();
					},
					[&]
					{
						return arena.AllocateWithResult(alloc);
					}));
			}

			Queue queue = device.GetQueue(QueueType::eGraphics, 0, error);
			if (queue.IsValid())
			{
				reports.push_back(Probe(
					"Queue::GetCompletedValue",
					[&]
					{
						std::uint64_t out = 0;
						return queue.GetCompletedValue(TimelineHandle{}, out);
					},
					[&](Error & queueError)
					{
						std::uint64_t out = 0;
						return queue.GetCompletedValue(TimelineHandle{}, out, queueError);
					},
					[&]
					{
						return queue.GetCompletedValueWithResult(TimelineHandle{});
					}));
			}
		}

		return reports;
	}

	void CheckOverloadsAgree(Device device)
	{
		const std::vector<OverloadReport> reports = ProbeEveryOverload(device);

		ASSERT_GE(reports.size(), kMinimumOperationsProbed)
			<< "the overload sweep reached " << reports.size() << " operations, fewer than the " << kMinimumOperationsProbed
			<< " every device can be asked. A probe that stopped running is a probe that stopped disagreeing.";

		for (const OverloadReport & report : reports)
		{
			SCOPED_TRACE(report.operation);

			EXPECT_EQ(report.erroredSucceeded, report.resultSucceeded)
				<< report.operation << " succeeded through one form and failed through the other, so which one a caller picked decides what they were told";

			EXPECT_EQ(report.erroredCode, report.resultCode)
				<< report.operation << " reported " << ErrorCodeName(report.erroredCode) << " through the out-Error form and "
				<< ErrorCodeName(report.resultCode) << " through the WithResult form";

			EXPECT_EQ(report.plainSucceeded, report.erroredSucceeded)
				<< report.operation << " disagreed between the form that carries no diagnostic and the one that does, which is the form a caller reaches for "
				<< "when they do not want to handle the failure and the one that tells them there was one";

			if (!report.erroredSucceeded)
			{
				EXPECT_NE(report.erroredCode, ErrorCode::eOk) << report.operation << " failed and left ErrorCode::eOk behind";
			}
			else
			{
				EXPECT_EQ(report.erroredCode, ErrorCode::eOk) << report.operation << " succeeded but left a failure code behind";
			}
		}
	}

} // namespace azo::rhi::test::oracle

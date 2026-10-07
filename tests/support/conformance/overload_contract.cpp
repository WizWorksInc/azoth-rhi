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
			report.resultSucceeded = asResult.has_value();
			report.resultCode	   = asResult.has_value() ? ErrorCode::eOk : asResult.get_error().code;

			return report;
		}

		template <typename Handle>
		[[nodiscard]] bool CreatedAndReleased(Device device, Handle handle)
		{
			if (!handle.is_valid())
			{
				return false;
			}

			Error ignored{};
			static_cast<void>(device.destroy(handle, {}, ignored));
			return true;
		}

	}

	std::vector<OverloadReport> ProbeEveryOverload(Device device)
	{
		std::vector<OverloadReport> reports;
		reports.reserve(32);

		const DeviceCaps & caps = device.get_caps();

		reports.push_back(Probe(
			"Device::CreateTextureView",
			[&]
			{
				return device.create_texture_view(TextureHandle{}, samples::FullTextureView()).is_valid();
			},
			[&](Error & error)
			{
				return device.create_texture_view(TextureHandle{}, samples::FullTextureView(), error).is_valid();
			},
			[&]
			{
				return device.create_texture_view_with_result(TextureHandle{}, samples::FullTextureView());
			}
		));

		reports.push_back(Probe(
			"Device::Map",
			[&]
			{
				return device.map(BufferHandle{}, MapDesc{}).data != nullptr;
			},
			[&](Error & error)
			{
				return device.map(BufferHandle{}, MapDesc{}, error).data != nullptr;
			},
			[&]
			{
				return device.map_with_result(BufferHandle{}, MapDesc{});
			}
		));

		reports.push_back(Probe(
			"Device::GetQueue",
			[&]
			{
				return device.get_queue(QueueType::eGraphics, device.get_queue_count(QueueType::eGraphics)).is_valid();
			},
			[&](Error & error)
			{
				return device.get_queue(QueueType::eGraphics, device.get_queue_count(QueueType::eGraphics), error).is_valid();
			},
			[&]
			{
				return device.get_queue_with_result(QueueType::eGraphics, device.get_queue_count(QueueType::eGraphics));
			}
		));

		{
			PlacedBufferDesc placed{};
			placed.buffer = samples::StorageBuffer();
			reports.push_back(Probe(
				"Device::CreatePlacedBuffer",
				[&]
				{
					return device.create_placed_buffer(placed).is_valid();
				},
				[&](Error & error)
				{
					return device.create_placed_buffer(placed, error).is_valid();
				},
				[&]
				{
					return device.create_placed_buffer_with_result(placed);
				}
			));

			PlacedTextureDesc placedTexture{};
			placedTexture.texture = samples::SampledTexture2D();
			reports.push_back(Probe(
				"Device::CreatePlacedTexture",
				[&]
				{
					return device.create_placed_texture(placedTexture).is_valid();
				},
				[&](Error & error)
				{
					return device.create_placed_texture(placedTexture, error).is_valid();
				},
				[&]
				{
					return device.create_placed_texture_with_result(placedTexture);
				}
			));
		}

		reports.push_back(Probe(
			"Device::CreateBuffer",
			[&]
			{
				return CreatedAndReleased(device, device.create_buffer(samples::StorageBuffer()));
			},
			[&](Error & error)
			{
				return CreatedAndReleased(device, device.create_buffer(samples::StorageBuffer(), error));
			},
			[&]
			{
				Result<BufferHandle> result = device.create_buffer_with_result(samples::StorageBuffer());
				if (result.has_value())
				{
					static_cast<void>(CreatedAndReleased(device, result.value()));
				}
				return result;
			}
		));

		reports.push_back(Probe(
			"Device::CreateTexture",
			[&]
			{
				return CreatedAndReleased(device, device.create_texture(samples::SampledTexture2D()));
			},
			[&](Error & error)
			{
				return CreatedAndReleased(device, device.create_texture(samples::SampledTexture2D(), error));
			},
			[&]
			{
				Result<TextureHandle> result = device.create_texture_with_result(samples::SampledTexture2D());
				if (result.has_value())
				{
					static_cast<void>(CreatedAndReleased(device, result.value()));
				}
				return result;
			}
		));

		reports.push_back(Probe(
			"Device::CreateSampler",
			[&]
			{
				return CreatedAndReleased(device, device.create_sampler(samples::LinearSampler()));
			},
			[&](Error & error)
			{
				return CreatedAndReleased(device, device.create_sampler(samples::LinearSampler(), error));
			},
			[&]
			{
				Result<SamplerHandle> result = device.create_sampler_with_result(samples::LinearSampler());
				if (result.has_value())
				{
					static_cast<void>(CreatedAndReleased(device, result.value()));
				}
				return result;
			}
		));

		reports.push_back(Probe(
			"Device::CreateHeap",
			[&]
			{
				return CreatedAndReleased(device, device.create_heap(samples::GpuHeap()));
			},
			[&](Error & error)
			{
				return CreatedAndReleased(device, device.create_heap(samples::GpuHeap(), error));
			},
			[&]
			{
				Result<HeapHandle> result = device.create_heap_with_result(samples::GpuHeap());
				if (result.has_value())
				{
					static_cast<void>(CreatedAndReleased(device, result.value()));
				}
				return result;
			}
		));

		{
			const samples::UniformLayout layout{};
			reports.push_back(Probe(
				"Device::CreateDescriptorSetLayout",
				[&]
				{
					return CreatedAndReleased(device, device.create_descriptor_set_layout(layout.Desc()));
				},
				[&](Error & error)
				{
					return CreatedAndReleased(device, device.create_descriptor_set_layout(layout.Desc(), error));
				},
				[&]
				{
					Result<DescriptorSetLayoutHandle> result = device.create_descriptor_set_layout_with_result(layout.Desc());
					if (result.has_value())
					{
						static_cast<void>(CreatedAndReleased(device, result.value()));
					}
					return result;
				}
			));

			Error setupError{};
			const DescriptorSetLayoutHandle setLayout = device.create_descriptor_set_layout(layout.Desc(), setupError);
			const samples::SimplePipelineLayout pipelineLayout{ setLayout };
			reports.push_back(Probe(
				"Device::CreatePipelineLayout",
				[&]
				{
					return CreatedAndReleased(device, device.create_pipeline_layout(pipelineLayout.Desc()));
				},
				[&](Error & error)
				{
					return CreatedAndReleased(device, device.create_pipeline_layout(pipelineLayout.Desc(), error));
				},
				[&]
				{
					Result<PipelineLayoutHandle> result = device.create_pipeline_layout_with_result(pipelineLayout.Desc());
					if (result.has_value())
					{
						static_cast<void>(CreatedAndReleased(device, result.value()));
					}
					return result;
				}
			));

			static_cast<void>(CreatedAndReleased(device, setLayout));
		}

		reports.push_back(Probe(
			"Device::CreateTimeline",
			[&]
			{
				return CreatedAndReleased(device, device.create_timeline(samples::Timeline()));
			},
			[&](Error & error)
			{
				return CreatedAndReleased(device, device.create_timeline(samples::Timeline(), error));
			},
			[&]
			{
				Result<TimelineHandle> result = device.create_timeline_with_result(samples::Timeline());
				if (result.has_value())
				{
					static_cast<void>(CreatedAndReleased(device, result.value()));
				}
				return result;
			}
		));

		reports.push_back(Probe(
			"Device::CreateBinarySemaphore",
			[&]
			{
				return CreatedAndReleased(device, device.create_binary_semaphore(BinarySemaphoreDesc{}));
			},
			[&](Error & error)
			{
				return CreatedAndReleased(device, device.create_binary_semaphore(BinarySemaphoreDesc{}, error));
			},
			[&]
			{
				Result<BinarySemaphoreHandle> result = device.create_binary_semaphore_with_result(BinarySemaphoreDesc{});
				if (result.has_value())
				{
					static_cast<void>(CreatedAndReleased(device, result.value()));
				}
				return result;
			}
		));

		reports.push_back(Probe(
			"Device::CreateQueryPool",
			[&]
			{
				return CreatedAndReleased(device, device.create_query_pool(samples::TimestampPool()));
			},
			[&](Error & error)
			{
				return CreatedAndReleased(device, device.create_query_pool(samples::TimestampPool(), error));
			},
			[&]
			{
				Result<QueryPoolHandle> result = device.create_query_pool_with_result(samples::TimestampPool());
				if (result.has_value())
				{
					static_cast<void>(CreatedAndReleased(device, result.value()));
				}
				return result;
			}
		));

		reports.push_back(Probe(
			"Device::CreateCommandPool",
			[&]
			{
				return device.create_command_pool(samples::CommandPool()).is_valid();
			},
			[&](Error & error)
			{
				return device.create_command_pool(samples::CommandPool(), error).is_valid();
			},
			[&]
			{
				return device.create_command_pool_with_result(samples::CommandPool());
			}
		));

		reports.push_back(Probe(
			"Device::CreateDescriptorArena",
			[&]
			{
				return device.create_descriptor_arena(samples::DescriptorArena()).is_valid();
			},
			[&](Error & error)
			{
				return device.create_descriptor_arena(samples::DescriptorArena(), error).is_valid();
			},
			[&]
			{
				return device.create_descriptor_arena_with_result(samples::DescriptorArena());
			}
		));

		reports.push_back(Probe(
			"Device::GetBufferMemoryInfo",
			[&]
			{
				MemoryInfo out{};
				return device.get_buffer_memory_info(samples::StorageBuffer(), out);
			},
			[&](Error & error)
			{
				MemoryInfo out{};
				return device.get_buffer_memory_info(samples::StorageBuffer(), out, error);
			},
			[&]
			{
				return device.get_buffer_memory_info_with_result(samples::StorageBuffer());
			}
		));

		reports.push_back(Probe(
			"Device::GetTextureMemoryInfo",
			[&]
			{
				MemoryInfo out{};
				return device.get_texture_memory_info(samples::SampledTexture2D(), out);
			},
			[&](Error & error)
			{
				MemoryInfo out{};
				return device.get_texture_memory_info(samples::SampledTexture2D(), out, error);
			},
			[&]
			{
				return device.get_texture_memory_info_with_result(samples::SampledTexture2D());
			}
		));

		reports.push_back(Probe(
			"Device::QueryMemoryBudget",
			[&]
			{
				MemoryBudgetInfo out{};
				return device.query_memory_budget(HeapType::eGpuLocal, out);
			},
			[&](Error & error)
			{
				MemoryBudgetInfo out{};
				return device.query_memory_budget(HeapType::eGpuLocal, out, error);
			},
			[&]
			{
				return device.query_memory_budget_with_result(HeapType::eGpuLocal);
			}
		));

		reports.push_back(Probe(
			"Device::CalibrateTimestamp",
			[&]
			{
				TimestampCalibration out{};
				return device.calibrate_timestamp(QueueType::eGraphics, out);
			},
			[&](Error & error)
			{
				TimestampCalibration out{};
				return device.calibrate_timestamp(QueueType::eGraphics, out, error);
			},
			[&]
			{
				return device.calibrate_timestamp_with_result(QueueType::eGraphics);
			}
		));

		reports.push_back(Probe(
			"Device::GetPipelineCacheData",
			[&]
			{
				PipelineCacheData out{};
				return device.get_pipeline_cache_data(PipelineCacheHandle{}, out);
			},
			[&](Error & error)
			{
				PipelineCacheData out{};
				return device.get_pipeline_cache_data(PipelineCacheHandle{}, out, error);
			},
			[&]
			{
				return device.get_pipeline_cache_data_with_result(PipelineCacheHandle{});
			}
		));

		if (caps.supportsPipelineCache)
		{
			reports.push_back(Probe(
				"Device::CreatePipelineCache",
				[&]
				{
					return CreatedAndReleased(device, device.create_pipeline_cache(PipelineCacheDesc{}));
				},
				[&](Error & error)
				{
					return CreatedAndReleased(device, device.create_pipeline_cache(PipelineCacheDesc{}, error));
				},
				[&]
				{
					Result<PipelineCacheHandle> result = device.create_pipeline_cache_with_result(PipelineCacheDesc{});
					if (result.has_value())
					{
						static_cast<void>(CreatedAndReleased(device, result.value()));
					}
					return result;
				}
			));
		}

		reports.push_back(Probe(
			"Device::CreateGraphicsPipeline",
			[&]
			{
				return CreatedAndReleased(device, device.create_graphics_pipeline(GraphicsPipelineDesc{}));
			},
			[&](Error & error)
			{
				return CreatedAndReleased(device, device.create_graphics_pipeline(GraphicsPipelineDesc{}, error));
			},
			[&]
			{
				Result<GraphicsPipelineHandle> result = device.create_graphics_pipeline_with_result(GraphicsPipelineDesc{});
				if (result.has_value())
				{
					static_cast<void>(CreatedAndReleased(device, result.value()));
				}
				return result;
			}
		));

		reports.push_back(Probe(
			"Device::CreateComputePipeline",
			[&]
			{
				return CreatedAndReleased(device, device.create_compute_pipeline(ComputePipelineDesc{}));
			},
			[&](Error & error)
			{
				return CreatedAndReleased(device, device.create_compute_pipeline(ComputePipelineDesc{}, error));
			},
			[&]
			{
				Result<ComputePipelineHandle> result = device.create_compute_pipeline_with_result(ComputePipelineDesc{});
				if (result.has_value())
				{
					static_cast<void>(CreatedAndReleased(device, result.value()));
				}
				return result;
			}
		));

		reports.push_back(Probe(
			"Device::CreateRayTracingPipeline",
			[&]
			{
				return CreatedAndReleased(device, device.create_ray_tracing_pipeline(RayTracingPipelineDesc{}));
			},
			[&](Error & error)
			{
				return CreatedAndReleased(device, device.create_ray_tracing_pipeline(RayTracingPipelineDesc{}, error));
			},
			[&]
			{
				Result<RayTracingPipelineHandle> result = device.create_ray_tracing_pipeline_with_result(RayTracingPipelineDesc{});
				if (result.has_value())
				{
					static_cast<void>(CreatedAndReleased(device, result.value()));
				}
				return result;
			}
		));

		reports.push_back(Probe(
			"Device::CreateAccelerationStructure",
			[&]
			{
				return CreatedAndReleased(device, device.create_acceleration_structure(AccelerationStructureDesc{}));
			},
			[&](Error & error)
			{
				return CreatedAndReleased(device, device.create_acceleration_structure(AccelerationStructureDesc{}, error));
			},
			[&]
			{
				Result<AccelerationStructureHandle> result = device.create_acceleration_structure_with_result(AccelerationStructureDesc{});
				if (result.has_value())
				{
					static_cast<void>(CreatedAndReleased(device, result.value()));
				}
				return result;
			}
		));

		reports.push_back(Probe(
			"Device::CreateSwapchain",
			[&]
			{
				return device.create_swapchain(SwapchainDesc{}).is_valid();
			},
			[&](Error & error)
			{
				return device.create_swapchain(SwapchainDesc{}, error).is_valid();
			},
			[&]
			{
				return device.create_swapchain_with_result(SwapchainDesc{});
			}
		));

		{
			Error error{};
			CommandPool pool = device.create_command_pool(samples::CommandPool(), error);
			if (pool.is_valid())
			{
				reports.push_back(Probe(
					"CommandPool::Allocate",
					[&]
					{
						return pool.allocate("azoth.rhi.conformance.overloadPlain").is_valid();
					},
					[&](Error & poolError)
					{
						return pool.allocate("azoth.rhi.conformance.overloadErrored", poolError).is_valid();
					},
					[&]
					{
						return pool.allocate_with_result("azoth.rhi.conformance.overloadResult");
					}
				));
			}

			DescriptorArena arena = device.create_descriptor_arena(samples::DescriptorArena(), error);
			if (arena.is_valid())
			{
				DescriptorSetAllocDesc alloc{};
				reports.push_back(Probe(
					"DescriptorArena::Allocate",
					[&]
					{
						return arena.allocate(alloc).is_valid();
					},
					[&](Error & arenaError)
					{
						return arena.allocate(alloc, arenaError).is_valid();
					},
					[&]
					{
						return arena.allocate_with_result(alloc);
					}
				));
			}

			Queue queue = device.get_queue(QueueType::eGraphics, 0, error);
			if (queue.is_valid())
			{
				reports.push_back(Probe(
					"Queue::GetCompletedValue",
					[&]
					{
						std::uint64_t out = 0;
						return queue.get_completed_value(TimelineHandle{}, out);
					},
					[&](Error & queueError)
					{
						std::uint64_t out = 0;
						return queue.get_completed_value(TimelineHandle{}, out, queueError);
					},
					[&]
					{
						return queue.get_completed_value_with_result(TimelineHandle{});
					}
				));
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

}

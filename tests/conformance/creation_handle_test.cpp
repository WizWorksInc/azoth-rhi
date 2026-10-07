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

#include <gtest/gtest.h>

#include <array>
#include <string>
#include <vector>

namespace rhi  = azo::rhi;
namespace test = azo::rhi::test;

namespace
{

	class CreationHandleTest : public test::BackendTest
	{
	};

	AZO_RHI_BACKEND_SUITE(CreationHandleTest);

	constexpr std::uint32_t kUnissuedIndex		= 9999;
	constexpr std::uint32_t kUnissuedGeneration = 7;

	template <class HandleT>
	[[nodiscard]] constexpr HandleT Unissued() noexcept
	{
		return HandleT{
			.index		= kUnissuedIndex,
			.generation = kUnissuedGeneration,
		};
	}

	TEST_P(CreationHandleTest, EveryCreationCallRejectsAHandleTheDeviceNeverIssued)
	{
		AZO_RHI_REQUIRE_HANDLE_VALIDATION();

		rhi::Device device = Dev();
		rhi::Error error{};

		std::vector<std::string> accepted;
		const auto check = [&accepted](const char * call, const bool wasAccepted)
		{
			if (wasAccepted)
			{
				accepted.emplace_back(call);
			}
		};

		check("CreateTextureView(texture)", device.create_texture_view(Unissued<rhi::TextureHandle>(), test::samples::FullTextureView(), error).is_valid());

		rhi::PlacedBufferDesc placedBuffer{};
		placedBuffer.buffer = test::samples::StorageBuffer();
		placedBuffer.heap	= Unissued<rhi::HeapHandle>();
		check("CreatePlacedBuffer(desc.heap)", device.create_placed_buffer(placedBuffer, error).is_valid());

		rhi::PlacedTextureDesc placedTexture{};
		placedTexture.texture = test::samples::SampledTexture2D();
		placedTexture.heap	  = Unissued<rhi::HeapHandle>();
		check("CreatePlacedTexture(desc.heap)", device.create_placed_texture(placedTexture, error).is_valid());

		const std::array unissuedSets{ Unissued<rhi::DescriptorSetLayoutHandle>() };
		rhi::PipelineLayoutDesc pipelineLayout{};
		pipelineLayout.sets = unissuedSets;
		check("CreatePipelineLayout(desc.sets)", device.create_pipeline_layout(pipelineLayout, error).is_valid());

		rhi::ComputePipelineDesc computePipeline{};
		computePipeline.layout = Unissued<rhi::PipelineLayoutHandle>();
		check("CreateComputePipeline(desc.layout)", device.create_compute_pipeline(computePipeline, error).is_valid());

		std::array<rhi::ShaderBinary, 1> shaders{};
		shaders[0].stage = rhi::ShaderStage::eVertex;

		rhi::GraphicsPipelineDesc graphicsPipeline{};
		graphicsPipeline.layout	 = Unissued<rhi::PipelineLayoutHandle>();
		graphicsPipeline.shaders = shaders;
		check("CreateGraphicsPipeline(desc.layout)", device.create_graphics_pipeline(graphicsPipeline, error).is_valid());

		rhi::AccelerationStructureDesc accelerationStructure{};
		accelerationStructure.storage = Unissued<rhi::BufferHandle>();
		accelerationStructure.size	  = test::samples::kBufferSize;
		check("CreateAccelerationStructure(desc.storage)", device.create_acceleration_structure(accelerationStructure, error).is_valid());

		rhi::DescriptorArena arena = device.create_descriptor_arena(test::samples::DescriptorArena(), error);
		if (arena.is_valid())
		{
			rhi::DescriptorSetAllocDesc allocation{};
			allocation.layout = Unissued<rhi::DescriptorSetLayoutHandle>();
			check("DescriptorArena::Allocate(desc.layout)", arena.allocate(allocation, error).is_valid());
		}

		std::string report;
		for (const std::string & call : accepted)
		{
			report += "\n  ";
			report += call;
		}

		EXPECT_TRUE(accepted.empty()) << accepted.size() << " creation calls accepted a handle this device never issued:" << report;
	}

}

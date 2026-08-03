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

		check("CreateTextureView(texture)", device.CreateTextureView(Unissued<rhi::TextureHandle>(), test::samples::FullTextureView(), error).IsValid());

		rhi::PlacedBufferDesc placedBuffer{};
		placedBuffer.buffer = test::samples::StorageBuffer();
		placedBuffer.heap	= Unissued<rhi::HeapHandle>();
		check("CreatePlacedBuffer(desc.heap)", device.CreatePlacedBuffer(placedBuffer, error).IsValid());

		rhi::PlacedTextureDesc placedTexture{};
		placedTexture.texture = test::samples::SampledTexture2D();
		placedTexture.heap	  = Unissued<rhi::HeapHandle>();
		check("CreatePlacedTexture(desc.heap)", device.CreatePlacedTexture(placedTexture, error).IsValid());

		const std::array unissuedSets{ Unissued<rhi::DescriptorSetLayoutHandle>() };
		rhi::PipelineLayoutDesc pipelineLayout{};
		pipelineLayout.sets = unissuedSets;
		check("CreatePipelineLayout(desc.sets)", device.CreatePipelineLayout(pipelineLayout, error).IsValid());

		rhi::ComputePipelineDesc computePipeline{};
		computePipeline.layout = Unissued<rhi::PipelineLayoutHandle>();
		check("CreateComputePipeline(desc.layout)", device.CreateComputePipeline(computePipeline, error).IsValid());

		std::array<rhi::ShaderBinary, 1> shaders{};
		shaders[0].stage = rhi::ShaderStage::eVertex;

		rhi::GraphicsPipelineDesc graphicsPipeline{};
		graphicsPipeline.layout	 = Unissued<rhi::PipelineLayoutHandle>();
		graphicsPipeline.shaders = shaders;
		check("CreateGraphicsPipeline(desc.layout)", device.CreateGraphicsPipeline(graphicsPipeline, error).IsValid());

		rhi::AccelerationStructureDesc accelerationStructure{};
		accelerationStructure.storage = Unissued<rhi::BufferHandle>();
		accelerationStructure.size	  = test::samples::kBufferSize;
		check("CreateAccelerationStructure(desc.storage)", device.CreateAccelerationStructure(accelerationStructure, error).IsValid());

		rhi::DescriptorArena arena = device.CreateDescriptorArena(test::samples::DescriptorArena(), error);
		if (arena.IsValid())
		{
			rhi::DescriptorSetAllocDesc allocation{};
			allocation.layout = Unissued<rhi::DescriptorSetLayoutHandle>();
			check("DescriptorArena::Allocate(desc.layout)", arena.Allocate(allocation, error).IsValid());
		}

		std::string report;
		for (const std::string & call : accepted)
		{
			report += "\n  ";
			report += call;
		}

		EXPECT_TRUE(accepted.empty()) << accepted.size() << " creation calls accepted a handle this device never issued:" << report;
	}

} // namespace

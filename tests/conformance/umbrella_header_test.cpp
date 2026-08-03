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

#include "azoth/rhi/rhi.hpp"

#include <gtest/gtest.h>

#include <type_traits>

namespace
{

	template <class T>
	constexpr bool Exported = requires { sizeof(T); };

	TEST(UmbrellaHeader, ExportsTheBackendSelectionVocabulary)
	{
		static_assert(Exported<azo::rhi::GraphicsApiId>);
		static_assert(Exported<azo::rhi::VulkanApi>);
		static_assert(Exported<azo::rhi::D3D12Api>);
		static_assert(Exported<azo::rhi::MetalApi>);
		static_assert(Exported<azo::rhi::NullApi>);
		static_assert(Exported<azo::rhi::GraphicsApiRegistry>);
		static_assert(Exported<azo::rhi::BackendInfo>);
		static_assert(Exported<azo::rhi::BackendCreateInfo>);

		SUCCEED();
	}

	TEST(UmbrellaHeader, ExportsTheDeviceAndItsDescriptions)
	{
		static_assert(Exported<azo::rhi::Device>);
		static_assert(Exported<azo::rhi::UniqueDevice>);
		static_assert(Exported<azo::rhi::Instance>);
		static_assert(Exported<azo::rhi::DeviceDesc>);
		static_assert(Exported<azo::rhi::DeviceCaps>);
		static_assert(Exported<azo::rhi::AdapterInfo>);
		static_assert(Exported<azo::rhi::InstanceDesc>);
		static_assert(Exported<azo::rhi::QueueRequest>);
		static_assert(Exported<azo::rhi::FormatSupport>);
		static_assert(Exported<azo::rhi::ValidationMessageCounts>);

		SUCCEED();
	}

	TEST(UmbrellaHeader, ExportsEveryHandleDomain)
	{
		static_assert(Exported<azo::rhi::BufferHandle>);
		static_assert(Exported<azo::rhi::TextureHandle>);
		static_assert(Exported<azo::rhi::TextureViewHandle>);
		static_assert(Exported<azo::rhi::SamplerHandle>);
		static_assert(Exported<azo::rhi::HeapHandle>);
		static_assert(Exported<azo::rhi::DescriptorSetLayoutHandle>);
		static_assert(Exported<azo::rhi::DescriptorSetHandle>);
		static_assert(Exported<azo::rhi::PipelineLayoutHandle>);
		static_assert(Exported<azo::rhi::GraphicsPipelineHandle>);
		static_assert(Exported<azo::rhi::ComputePipelineHandle>);
		static_assert(Exported<azo::rhi::RayTracingPipelineHandle>);
		static_assert(Exported<azo::rhi::PipelineCacheHandle>);
		static_assert(Exported<azo::rhi::AccelerationStructureHandle>);
		static_assert(Exported<azo::rhi::QueryPoolHandle>);
		static_assert(Exported<azo::rhi::TimelineHandle>);
		static_assert(Exported<azo::rhi::BinarySemaphoreHandle>);

		SUCCEED();
	}

	TEST(UmbrellaHeader, ExportsTheRecordingAndSubmissionSurface)
	{
		static_assert(Exported<azo::rhi::CommandPool>);
		static_assert(Exported<azo::rhi::CommandList>);
		static_assert(Exported<azo::rhi::Queue>);
		static_assert(Exported<azo::rhi::SubmitDesc>);
		static_assert(Exported<azo::rhi::BarrierBatch>);
		static_assert(Exported<azo::rhi::BeginRenderingDesc>);
		static_assert(Exported<azo::rhi::Viewport>);
		static_assert(Exported<azo::rhi::Rect2D>);
		static_assert(Exported<azo::rhi::BufferTextureCopy>);
		static_assert(Exported<azo::rhi::TextureBlit>);

		SUCCEED();
	}

	TEST(UmbrellaHeader, ExportsTheResourceAndDescriptorVocabulary)
	{
		static_assert(Exported<azo::rhi::BufferDesc>);
		static_assert(Exported<azo::rhi::TextureDesc>);
		static_assert(Exported<azo::rhi::TextureViewDesc>);
		static_assert(Exported<azo::rhi::SamplerDesc>);
		static_assert(Exported<azo::rhi::HeapDesc>);
		static_assert(Exported<azo::rhi::MappedMemory>);
		static_assert(Exported<azo::rhi::DescriptorArena>);
		static_assert(Exported<azo::rhi::DescriptorBinding>);
		static_assert(Exported<azo::rhi::GraphicsPipelineDesc>);
		static_assert(Exported<azo::rhi::ComputePipelineDesc>);
		static_assert(Exported<azo::rhi::ShaderBinary>);
		static_assert(Exported<azo::rhi::Swapchain>);
		static_assert(Exported<azo::rhi::SwapchainDesc>);
		static_assert(Exported<azo::rhi::QueryPoolDesc>);

		SUCCEED();
	}

	TEST(UmbrellaHeader, ExportsTheErrorAndVersionTypes)
	{
		static_assert(Exported<azo::rhi::Error>);
		static_assert(Exported<azo::rhi::Result<int>>);
		static_assert(Exported<azo::rhi::BuildInfo>);
		static_assert(Exported<azo::rhi::Flags<azo::rhi::BufferUsage>>);

		static_assert(std::is_invocable_v<decltype(azo::rhi::GetBuildInfo)>);
		static_assert(azo::rhi::MakeGraphicsApiId("azoth.rhi.null") == azo::rhi::NullApi::id);

		SUCCEED();
	}

	TEST(UmbrellaHeader, DoesNotDragInTheOptionalSeams)
	{
		SUCCEED();
	}

} // namespace

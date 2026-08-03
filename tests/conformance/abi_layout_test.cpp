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

#include "azoth/rhi/builders/device_builder.hpp"
#include "azoth/rhi/host/allocator.hpp"
#include "azoth/rhi/host/profiler.hpp"
#include "azoth/rhi/rhi.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace rhi = azo::rhi;

namespace
{

	template <class T>
	constexpr bool IsPlainDesc = std::is_trivially_copyable_v<T> && std::is_standard_layout_v<T> && std::is_default_constructible_v<T>;

	// What the two descs measure today on a 64-bit target, read by the tripwire below, not asserted as an ABI promise.
	constexpr std::size_t kInstanceDescSize = 104;
	constexpr std::size_t kDeviceDescSize	= 216;

	TEST(AbiLayout, EveryResourceDescIsAPlainDescription)
	{
		static_assert(IsPlainDesc<rhi::BufferDesc>);
		static_assert(IsPlainDesc<rhi::TextureDesc>);
		static_assert(IsPlainDesc<rhi::TextureViewDesc>);
		static_assert(IsPlainDesc<rhi::SamplerDesc>);
		static_assert(IsPlainDesc<rhi::HeapDesc>);
		static_assert(IsPlainDesc<rhi::PlacedBufferDesc>);
		static_assert(IsPlainDesc<rhi::PlacedTextureDesc>);
		static_assert(IsPlainDesc<rhi::MapDesc>);
		static_assert(IsPlainDesc<rhi::MemoryInfo>);
		static_assert(IsPlainDesc<rhi::MemoryBudgetInfo>);
		static_assert(IsPlainDesc<rhi::ResidencyPriorityDesc>);
		static_assert(IsPlainDesc<rhi::ExternalHandle>);
		static_assert(IsPlainDesc<rhi::ExternalHandleSupportDesc>);
		static_assert(IsPlainDesc<rhi::ExternalHandleSupport>);
		static_assert(IsPlainDesc<rhi::ExternalBufferImportDesc>);
		static_assert(IsPlainDesc<rhi::ExternalTextureImportDesc>);
		static_assert(IsPlainDesc<rhi::ExternalHeapImportDesc>);

		SUCCEED();
	}

	TEST(AbiLayout, EverySynchronizationTypeIsAPlainDescription)
	{
		static_assert(IsPlainDesc<rhi::TimelineDesc>);
		static_assert(IsPlainDesc<rhi::ExternalTimelineImportDesc>);
		static_assert(IsPlainDesc<rhi::ExternalBinarySemaphoreImportDesc>);
		static_assert(IsPlainDesc<rhi::TimelinePoint>);
		static_assert(IsPlainDesc<rhi::BinarySemaphoreDesc>);
		static_assert(IsPlainDesc<rhi::SwapchainSync>);
		static_assert(IsPlainDesc<rhi::ResourceState>);
		static_assert(IsPlainDesc<rhi::QueueFamilyTransfer>);
		static_assert(IsPlainDesc<rhi::BufferBarrier>);
		static_assert(IsPlainDesc<rhi::TextureBarrier>);
		static_assert(IsPlainDesc<rhi::MemoryBarrier>);
		static_assert(IsPlainDesc<rhi::TextureSubresource>);
		static_assert(IsPlainDesc<rhi::TextureSubresourceRange>);
		static_assert(IsPlainDesc<rhi::RetirePoint>);

		SUCCEED();
	}

	TEST(AbiLayout, EveryPipelineAndDescriptorDescIsAPlainDescription)
	{
		static_assert(IsPlainDesc<rhi::DescriptorBinding>);
		static_assert(IsPlainDesc<rhi::PushConstantRange>);
		static_assert(IsPlainDesc<rhi::DescriptorArenaDesc>);
		static_assert(IsPlainDesc<rhi::DescriptorSetAllocDesc>);
		static_assert(IsPlainDesc<rhi::DescriptorWriteBuffer>);
		static_assert(IsPlainDesc<rhi::DescriptorWriteTexture>);
		static_assert(IsPlainDesc<rhi::DescriptorWriteSampler>);
		static_assert(IsPlainDesc<rhi::DescriptorWriteAccelerationStructure>);
		static_assert(IsPlainDesc<rhi::DynamicDescriptorOffset>);
		static_assert(IsPlainDesc<rhi::ShaderBinary>);
		static_assert(IsPlainDesc<rhi::VertexBindingDesc>);
		static_assert(IsPlainDesc<rhi::VertexAttributeDesc>);
		static_assert(IsPlainDesc<rhi::RasterStateDesc>);
		static_assert(IsPlainDesc<rhi::StencilFaceDesc>);
		static_assert(IsPlainDesc<rhi::DepthStencilStateDesc>);
		static_assert(IsPlainDesc<rhi::ColorBlendAttachmentDesc>);
		static_assert(IsPlainDesc<rhi::BlendStateDesc>);
		static_assert(IsPlainDesc<rhi::GraphicsPipelineDesc>);
		static_assert(IsPlainDesc<rhi::PipelineCacheDesc>);

		SUCCEED();
	}

	TEST(AbiLayout, EveryCommandAndRenderingTypeIsAPlainDescription)
	{
		static_assert(IsPlainDesc<rhi::Viewport>);
		static_assert(IsPlainDesc<rhi::Rect2D>);
		static_assert(IsPlainDesc<rhi::CommandPoolDesc>);
		static_assert(IsPlainDesc<rhi::ClearColor>);
		static_assert(IsPlainDesc<rhi::ClearDepthStencil>);
		static_assert(IsPlainDesc<rhi::RenderingAttachment>);
		static_assert(IsPlainDesc<rhi::BeginRenderingDesc>);
		static_assert(IsPlainDesc<rhi::Offset3D>);
		static_assert(IsPlainDesc<rhi::Extent2D>);
		static_assert(IsPlainDesc<rhi::Extent3D>);
		static_assert(IsPlainDesc<rhi::BufferTextureCopy>);
		static_assert(IsPlainDesc<rhi::TextureCopy>);
		static_assert(IsPlainDesc<rhi::TextureResolve>);
		static_assert(IsPlainDesc<rhi::TextureBlit>);
		static_assert(IsPlainDesc<rhi::QueryPoolDesc>);
		static_assert(IsPlainDesc<rhi::QueryResultDesc>);
		static_assert(IsPlainDesc<rhi::TimestampCalibration>);
		static_assert(IsPlainDesc<rhi::SwapchainDesc>);
		static_assert(IsPlainDesc<rhi::AcquireResult>);
		static_assert(IsPlainDesc<rhi::PresentResult>);
		static_assert(IsPlainDesc<rhi::SurfaceHandle>);

		SUCCEED();
	}

	TEST(AbiLayout, EveryDeviceLevelDescriptionIsPlainToo)
	{
		static_assert(IsPlainDesc<rhi::DeviceDesc>);
		static_assert(IsPlainDesc<rhi::InstanceDesc>);
		static_assert(IsPlainDesc<rhi::QueueRequest>);
		static_assert(IsPlainDesc<rhi::ApiVersion>);
		static_assert(IsPlainDesc<rhi::NativeValidationDesc>);
		static_assert(IsPlainDesc<rhi::DeviceCaps>);
		static_assert(IsPlainDesc<rhi::AdapterInfo>);
		static_assert(IsPlainDesc<rhi::FormatSupport>);
		static_assert(IsPlainDesc<rhi::DestroyDesc>);
		static_assert(IsPlainDesc<rhi::BackendInfo>);
		static_assert(IsPlainDesc<rhi::ValidationMessageCounts>);
		static_assert(IsPlainDesc<rhi::Error>);
		static_assert(IsPlainDesc<rhi::BuildInfo>);

		SUCCEED();
	}

	TEST(AbiLayout, DescsThatFeedInstanceCreationHaveNotChangedShape)
	{
		static_assert(sizeof(rhi::InstanceDesc) == kInstanceDescSize,
			"InstanceDesc changed shape. Check InstanceDescForDevice carries any field a device has to pass on, then update kInstanceDescSize.");
		static_assert(sizeof(rhi::DeviceDesc) == kDeviceDescSize,
			"DeviceDesc changed shape. Check InstanceDescForDevice carries any field the instance behind a device needs, then update kDeviceDescSize.");

		SUCCEED();
	}

	TEST(AbiLayout, EveryHandleIsExactlyTwoWords)
	{
		constexpr std::size_t kHandleSize = 2 * sizeof(std::uint32_t);

		static_assert(sizeof(rhi::BufferHandle) == kHandleSize);
		static_assert(sizeof(rhi::TextureHandle) == kHandleSize);
		static_assert(sizeof(rhi::TextureViewHandle) == kHandleSize);
		static_assert(sizeof(rhi::SamplerHandle) == kHandleSize);
		static_assert(sizeof(rhi::HeapHandle) == kHandleSize);
		static_assert(sizeof(rhi::DescriptorSetLayoutHandle) == kHandleSize);
		static_assert(sizeof(rhi::DescriptorSetHandle) == kHandleSize);
		static_assert(sizeof(rhi::PipelineLayoutHandle) == kHandleSize);
		static_assert(sizeof(rhi::GraphicsPipelineHandle) == kHandleSize);
		static_assert(sizeof(rhi::ComputePipelineHandle) == kHandleSize);
		static_assert(sizeof(rhi::QueryPoolHandle) == kHandleSize);
		static_assert(sizeof(rhi::TimelineHandle) == kHandleSize);
		static_assert(sizeof(rhi::BinarySemaphoreHandle) == kHandleSize);

		SUCCEED();
	}

	TEST(AbiLayout, TheFacadesAreThinViewsRatherThanObjects)
	{
		static_assert(sizeof(rhi::Queue) == 2 * sizeof(void *));
		static_assert(sizeof(rhi::Instance) == 2 * sizeof(void *));
		static_assert(sizeof(rhi::Swapchain) == 2 * sizeof(void *));
		static_assert(sizeof(rhi::Device) == 2 * sizeof(void *));
		static_assert(sizeof(rhi::CommandList) == 2 * sizeof(void *));

		static_assert(sizeof(rhi::UniqueDevice) == 2 * sizeof(void *));
		static_assert(sizeof(rhi::UniqueInstance) == 2 * sizeof(void *));

		static_assert(sizeof(rhi::CommandPool) == 3 * sizeof(void *));
		static_assert(sizeof(rhi::DescriptorArena) == 3 * sizeof(void *));

		SUCCEED();
	}

	TEST(AbiLayout, TheNonOwningFacadesCopyFreelyAndTheOwningOneDoesNot)
	{
		static_assert(std::is_trivially_copyable_v<rhi::Device>);
		static_assert(std::is_trivially_copyable_v<rhi::Queue>);
		static_assert(std::is_trivially_copyable_v<rhi::CommandPool>);
		static_assert(std::is_trivially_copyable_v<rhi::CommandList>);
		static_assert(std::is_trivially_copyable_v<rhi::DescriptorArena>);
		static_assert(std::is_trivially_copyable_v<rhi::Swapchain>);
		static_assert(std::is_trivially_copyable_v<rhi::Instance>);

		static_assert(!std::is_copy_constructible_v<rhi::UniqueDevice>);
		static_assert(std::is_nothrow_move_constructible_v<rhi::UniqueDevice>);

		SUCCEED();
	}

	TEST(AbiLayout, TheFlagWrappersCostNothingOverTheirUnderlyingType)
	{
		static_assert(sizeof(rhi::Flags<rhi::BufferUsage>) == sizeof(std::uint32_t));
		static_assert(sizeof(rhi::Flags<rhi::TextureUsage>) == sizeof(std::uint32_t));
		static_assert(sizeof(rhi::Flags<rhi::ShaderStage>) == sizeof(std::uint32_t));
		static_assert(sizeof(rhi::Flags<rhi::TextureAspect>) == sizeof(std::uint8_t));
		static_assert(sizeof(rhi::Flags<rhi::PipelineStage>) == sizeof(std::uint64_t));
		static_assert(sizeof(rhi::Flags<rhi::Access>) == sizeof(std::uint64_t));

		SUCCEED();
	}

	TEST(AbiLayout, TheTypesThatOwnStorageAreTheOnesExpectedTo)
	{
		// The builders and the registry own heap storage on purpose, which is why they are not descs and why a desc built from one borrows, not owns.
		static_assert(!std::is_trivially_copyable_v<rhi::DeviceBuilder>);
		static_assert(!std::is_trivially_copyable_v<rhi::GraphicsApiRegistry>);

		static_assert(std::is_trivially_copyable_v<rhi::BufferDesc>);
		static_assert(std::is_trivially_copyable_v<rhi::BarrierBatch>);

		SUCCEED();
	}

	TEST(AbiLayout, TheSeamInterfacesArePolymorphicAndTheDataTypesAreNot)
	{
		static_assert(std::is_polymorphic_v<rhi::Profiler>);
		static_assert(std::is_polymorphic_v<rhi::HostAllocator>);
		static_assert(std::is_polymorphic_v<rhi::DeviceMemoryAllocator>);
		static_assert(std::has_virtual_destructor_v<rhi::Profiler>);
		static_assert(std::has_virtual_destructor_v<rhi::HostAllocator>);
		static_assert(std::has_virtual_destructor_v<rhi::DeviceMemoryAllocator>);

		static_assert(!std::is_polymorphic_v<rhi::BufferDesc>);
		static_assert(!std::is_polymorphic_v<rhi::Device>);
		static_assert(!std::is_polymorphic_v<rhi::CommandList>);

		SUCCEED();
	}

	TEST(AbiLayout, TheSpanCarryingDescsBorrowRatherThanOwn)
	{
		static_assert(std::is_trivially_copyable_v<rhi::BarrierBatch>);
		static_assert(std::is_trivially_copyable_v<rhi::SubmitDesc>);
		static_assert(std::is_trivially_copyable_v<rhi::DescriptorSetLayoutDesc>);
		static_assert(std::is_trivially_copyable_v<rhi::PipelineLayoutDesc>);
		static_assert(std::is_trivially_copyable_v<rhi::ShaderBindingMap>);
		static_assert(std::is_trivially_copyable_v<rhi::ShaderBindingEntry>);
		static_assert(std::is_trivially_copyable_v<rhi::SparseBindDesc>);

		SUCCEED();
	}

} // namespace

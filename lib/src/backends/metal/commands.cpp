// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#include "azoth/rhi/backend/dispatch.hpp"
#include "azoth/rhi/backend/support/bounded_count.hpp"
#include "azoth/rhi/backend/support/format_info.hpp"
#include "azoth/rhi/backend/support/host_containers.hpp"
#include "azoth/rhi/commands/copy_types.hpp"
#include "azoth/rhi/commands/render.hpp"
#include "azoth/rhi/commands/sync.hpp"
#include "azoth/rhi/core/c_string.hpp"
#include "azoth/rhi/core/constants.hpp"
#include "azoth/rhi/core/enums.hpp"
#include "azoth/rhi/core/profiling.hpp"
#include "azoth/rhi/core/result.hpp"
#include "azoth/rhi/resources/resources.hpp"
#include "azoth/rhi/resources/texture_view.hpp"

#include "backends/metal/internal.hpp"
#include "backends/metal_common/conversions.hpp"

#include <Foundation/NSAutoreleasePool.hpp>
#include <Foundation/NSSharedPtr.hpp>
#include <Foundation/NSString.hpp>
#include <Foundation/NSTypes.hpp>
#include <Metal/MTLArgument.hpp>
#include <Metal/MTLBlitCommandEncoder.hpp>
#include <Metal/MTLBuffer.hpp>
#include <Metal/MTLCommandBuffer.hpp>
#include <Metal/MTLCommandEncoder.hpp>
#include <Metal/MTLCommandQueue.hpp>
#include <Metal/MTLRenderCommandEncoder.hpp>
#include <Metal/MTLRenderPass.hpp>
#include <Metal/MTLResource.hpp>
#include <Metal/MTLTexture.hpp>
#include <Metal/MTLTypes.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

namespace azo::rhi::metal
{
	bool metal_cmd_begin(void * impl, Error * error) noexcept
	{
		auto * object	   = static_cast<MetalObject *>(impl);
		MetalCmdList * rec = object->list;

		MetalDevice * device			 = object->owner;
		MTL::CommandQueue * commandQueue = device->command_queue_for(object->queueType);
		if (commandQueue == nullptr)
		{
			return fail(error, ErrorCode::eInvalidState, "Metal command list has no command queue for its type");
		}

		release_cmd_buffer(device, rec, object->queueType);

		if (!device->openLists.try_open(object->queueType))
		{
			return fail(error, ErrorCode::eInvalidState, kOpenListBudgetExhausted);
		}
		rec->holdsListSlot = true;

		const NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());
		MTL::CommandBuffer * commandBuffer			  = commandQueue->commandBuffer();
		if (commandBuffer == nullptr)
		{
			rec->holdsListSlot = false;
			device->openLists.close(object->queueType);
			return fail(error, ErrorCode::eNativeApiError, "Metal command buffer allocation failed");
		}
		rec->commandBuffer = NS::RetainPtr(commandBuffer);
		if (!rec->debugName.empty())
		{
			commandBuffer->setLabel(NS::String::string(rec->debugName.c_str(), NS::UTF8StringEncoding));
		}
		rec->keepAlive.clear();

		rec->aliasWaitPending = false;
		rec->debugLabelScopes.clear();
		rec->pendingEndTimestamp.reset();

		rec->boundIndexBuffer = nullptr;
		rec->boundIndexOffset = 0;
		rec->boundIndexType	  = MTL::IndexTypeUInt32;
		rec->boundPrimitive	  = MTL::PrimitiveTypeTriangle;
		rec->boundThreadGroup = MTL::Size{ 1, 1, 1 };

		rec->lifecycle = ListLifecycle::eRecording;
		return succeed(error);
	}

	void pop_encoder_debug_groups(MetalCmdList * rec, MTL::CommandEncoder * encoder) noexcept
	{
		if (encoder == nullptr)
		{
			return;
		}

		for (std::size_t index = rec->debugLabelScopes.size(); index-- > 0;)
		{
			if (azo::rhi::detail::at(rec->debugLabelScopes, index) != rec->encoderEpoch)
			{
				break;
			}

			encoder->popDebugGroup();
			azo::rhi::detail::at(rec->debugLabelScopes, index) = kDebugScopeClosed;
		}
	}

	bool metal_cmd_end(void * impl, Error * error) noexcept
	{
		auto * object	   = static_cast<MetalObject *>(impl);
		MetalCmdList * rec = object->list;

		if (rec->renderEncoder.get() != nullptr)
		{
			pop_encoder_debug_groups(rec, rec->renderEncoder.get());
			if (rec->pendingEndTimestamp.get() != nullptr)
			{
				rec->renderEncoder->sampleCountersInBuffer(rec->pendingEndTimestamp.get(), rec->pendingEndQuery, false);
				rec->pendingEndTimestamp.reset();
			}

			rec->renderEncoder->endEncoding();
			rec->renderEncoder.reset();
		}
		if (rec->computeEncoder.get() != nullptr)
		{
			pop_encoder_debug_groups(rec, rec->computeEncoder.get());
			rec->computeEncoder->endEncoding();
			rec->computeEncoder.reset();
		}

		rec->lifecycle = ListLifecycle::eEnded;
		return succeed(error);
	}

	bool metal_cmd_barriers([[maybe_unused]] void * impl, [[maybe_unused]] const BarrierBatch & barriers, Error * error) noexcept
	{
		return succeed(error);
	}

	bool metal_cmd_alias_barriers(void * impl, std::span<const AliasBarrier> barriers, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal.aliasBarriers");

		auto * object		 = static_cast<MetalObject *>(impl);
		MetalDevice * device = object->owner;
		MetalCmdList * rec	 = object->list;
		if (rec == nullptr || rec->commandBuffer.get() == nullptr)
		{
			return fail(error, ErrorCode::eInvalidState, "command list has no command buffer");
		}
		if (barriers.empty())
		{
			return succeed(error);
		}
		if (rec->renderEncoder.get() != nullptr)
		{
			return fail(error, ErrorCode::eInvalidState, "aliasBarriers cannot be recorded inside a rendering scope, so record it between passes");
		}

		for (const AliasBarrier & barrier : barriers)
		{
			if ((barrier.beforeBuffer.is_valid() && device->buffers.resolve(barrier.beforeBuffer, true) == nullptr) ||
				(barrier.afterBuffer.is_valid() && device->buffers.resolve(barrier.afterBuffer, true) == nullptr) ||
				(barrier.beforeTexture.is_valid() && device->textures.resolve(barrier.beforeTexture, true) == nullptr) ||
				(barrier.afterTexture.is_valid() && device->textures.resolve(barrier.afterTexture, true) == nullptr))
			{
				return fail(error, ErrorCode::eInvalidHandle, "aliasBarriers with an invalid resource handle");
			}
		}

		if (rec->aliasFence.get() == nullptr)
		{
			rec->aliasFence = NS::TransferPtr(object->owner->device->newFence());
			if (rec->aliasFence.get() == nullptr)
			{
				return fail(error, ErrorCode::eNativeApiError, "Metal alias fence allocation failed");
			}
		}

		const NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());
		if (rec->computeEncoder.get() != nullptr)
		{
			pop_encoder_debug_groups(rec, rec->computeEncoder.get());
			rec->computeEncoder->updateFence(rec->aliasFence.get());
			rec->computeEncoder->endEncoding();
			rec->computeEncoder.reset();
		}
		else
		{
			MTL::BlitCommandEncoder * encoder = rec->commandBuffer->blitCommandEncoder();
			if (encoder == nullptr)
			{
				return fail(error, ErrorCode::eNativeApiError, "Metal blit command encoder creation failed");
			}
			encoder->updateFence(rec->aliasFence.get());
			encoder->endEncoding();
		}

		rec->aliasWaitPending = true;
		return succeed(error);
	}

	bool metal_cmd_begin_debug_label(void * impl, CString name, [[maybe_unused]] std::uint32_t color, Error * error) noexcept
	{
		auto * object	   = static_cast<MetalObject *>(impl);
		MetalCmdList * rec = object->list;
		if (rec == nullptr || rec->commandBuffer.get() == nullptr)
		{
			return fail(error, ErrorCode::eInvalidState, "command list has no command buffer");
		}

		if (!object->owner->debugLabels)
		{
			return succeed(error);
		}

		const NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());
		NS::String * label							  = NS::String::string(name == nullptr ? "" : name, NS::UTF8StringEncoding);

		MTL::CommandEncoder * encoder = rec->renderEncoder.get() != nullptr ? static_cast<MTL::CommandEncoder *>(rec->renderEncoder.get())
																			: static_cast<MTL::CommandEncoder *>(rec->computeEncoder.get());
		if (!detail::try_push_back(rec->debugLabelScopes, encoder != nullptr ? rec->encoderEpoch : kDebugScopeCommandBuffer))
		{
			return fail(error, ErrorCode::eOutOfHostMemory, "Metal debug label tracking allocation failed");
		}

		if (encoder != nullptr)
		{
			encoder->pushDebugGroup(label);
		}
		else
		{
			rec->commandBuffer->pushDebugGroup(label);
		}
		return succeed(error);
	}

	bool metal_cmd_end_debug_label(void * impl, Error * error) noexcept
	{
		auto * object	   = static_cast<MetalObject *>(impl);
		MetalCmdList * rec = object->list;
		if (rec == nullptr || rec->commandBuffer.get() == nullptr)
		{
			return fail(error, ErrorCode::eInvalidState, "command list has no command buffer");
		}

		if (!object->owner->debugLabels)
		{
			return succeed(error);
		}

		if (rec->debugLabelScopes.empty())
		{
			return fail(error, ErrorCode::eValidationFailed, "endDebugLabel without a matching beginDebugLabel");
		}

		const std::uint64_t opened = rec->debugLabelScopes.back();
		rec->debugLabelScopes.pop_back();

		if (opened == kDebugScopeClosed)
		{
			return succeed(error);
		}

		if (opened == kDebugScopeCommandBuffer)
		{
			rec->commandBuffer->popDebugGroup();
			return succeed(error);
		}

		MTL::CommandEncoder * live = rec->renderEncoder.get() != nullptr ? static_cast<MTL::CommandEncoder *>(rec->renderEncoder.get())
																		 : static_cast<MTL::CommandEncoder *>(rec->computeEncoder.get());
		if (live != nullptr && opened == rec->encoderEpoch)
		{
			live->popDebugGroup();
		}
		return succeed(error);
	}

	bool metal_copy_buffer(
		void * impl,
		BufferHandle dst,
		std::uint64_t dstOffset,
		BufferHandle src,
		std::uint64_t srcOffset,
		std::uint64_t size,
		Error * error
	) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal.copyBuffer");

		auto * object					   = static_cast<MetalObject *>(impl);
		MetalDevice * device			   = object->owner;
		MTL::CommandBuffer * commandBuffer = cmd_buffer_of(object);
		if (commandBuffer == nullptr)
		{
			return fail(error, ErrorCode::eInvalidState, "command list has no command buffer");
		}

		MTL::Buffer * source	  = resolve_buffer(device, src);
		MTL::Buffer * destination = resolve_buffer(device, dst);
		if (source == nullptr || destination == nullptr)
		{
			return fail(error, ErrorCode::eInvalidHandle, "copyBuffer names a buffer this device never created");
		}

		const NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());
		MTL::BlitCommandEncoder * encoder			  = begin_blit(object, error);
		if (encoder == nullptr)
		{
			return false;
		}
		encoder->copyFromBuffer(source, srcOffset, destination, dstOffset, size);
		encoder->endEncoding();
		return succeed(error);
	}

	bool metal_copy_buffer_to_texture(void * impl, TextureHandle dst, BufferHandle src, std::span<const BufferTextureCopy> regions, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal.copyBufferToTexture");

		auto * object					   = static_cast<MetalObject *>(impl);
		MetalDevice * device			   = object->owner;
		MTL::CommandBuffer * commandBuffer = cmd_buffer_of(object);
		if (commandBuffer == nullptr)
		{
			return fail(error, ErrorCode::eInvalidState, "command list has no command buffer");
		}

		MTL::Texture * texture = resolve_texture(device, dst);
		MTL::Buffer * buffer   = resolve_buffer(device, src);
		if (texture == nullptr || buffer == nullptr)
		{
			return fail(error, ErrorCode::eInvalidHandle, "copyBufferToTexture names a resource this device never created");
		}

		const Format format = resolve_texture_format(device, dst);
		if (!detail::has_linear_layout(format))
		{
			return fail(error, ErrorCode::eUnsupportedFeature, "copyBufferToTexture on a combined depth-stencil format, whose aspects copy separately");
		}

		const NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());
		MTL::BlitCommandEncoder * encoder			  = begin_blit(object, error);
		if (encoder == nullptr)
		{
			return false;
		}
		for (const BufferTextureCopy & region : regions)
		{
			const std::uint32_t rowTexels	 = region.bufferRowLength != 0 ? region.bufferRowLength : region.textureExtent.width;
			const std::uint32_t imageRows	 = region.bufferImageHeight != 0 ? region.bufferImageHeight : region.textureExtent.height;
			const auto bytesPerRow			 = static_cast<NS::UInteger>(detail::tight_row_pitch(format, rowTexels));
			const NS::UInteger bytesPerImage = bytesPerRow * detail::block_rows(format, imageRows);
			encoder->copyFromBuffer(
				buffer,
				region.bufferOffset,
				bytesPerRow,
				bytesPerImage,
				MTL::Size::Make(region.textureExtent.width, region.textureExtent.height, region.textureExtent.depth),
				texture,
				region.subresource.layer,
				region.subresource.mip,
				MTL::Origin::Make(
					static_cast<NS::UInteger>(region.textureOffset.x),
					static_cast<NS::UInteger>(region.textureOffset.y),
					static_cast<NS::UInteger>(region.textureOffset.z)
				)
			);
		}
		encoder->endEncoding();
		return succeed(error);
	}

	bool metal_copy_texture_to_buffer(void * impl, BufferHandle dst, TextureHandle src, std::span<const BufferTextureCopy> regions, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal.copyTextureToBuffer");

		auto * object					   = static_cast<MetalObject *>(impl);
		MetalDevice * device			   = object->owner;
		MTL::CommandBuffer * commandBuffer = cmd_buffer_of(object);
		if (commandBuffer == nullptr)
		{
			return fail(error, ErrorCode::eInvalidState, "command list has no command buffer");
		}

		MTL::Texture * texture = resolve_texture(device, src);
		MTL::Buffer * buffer   = resolve_buffer(device, dst);
		if (texture == nullptr || buffer == nullptr)
		{
			return fail(error, ErrorCode::eInvalidHandle, "copyTextureToBuffer names a resource this device never created");
		}

		const Format format = resolve_texture_format(device, src);
		if (!detail::has_linear_layout(format))
		{
			return fail(error, ErrorCode::eUnsupportedFeature, "copyTextureToBuffer on a combined depth-stencil format, whose aspects copy separately");
		}

		const NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());
		MTL::BlitCommandEncoder * encoder			  = begin_blit(object, error);
		if (encoder == nullptr)
		{
			return false;
		}
		for (const BufferTextureCopy & region : regions)
		{
			const std::uint32_t rowTexels	 = region.bufferRowLength != 0 ? region.bufferRowLength : region.textureExtent.width;
			const std::uint32_t imageRows	 = region.bufferImageHeight != 0 ? region.bufferImageHeight : region.textureExtent.height;
			const auto bytesPerRow			 = static_cast<NS::UInteger>(detail::tight_row_pitch(format, rowTexels));
			const NS::UInteger bytesPerImage = bytesPerRow * detail::block_rows(format, imageRows);
			encoder->copyFromTexture(
				texture,
				region.subresource.layer,
				region.subresource.mip,
				MTL::Origin::Make(
					static_cast<NS::UInteger>(region.textureOffset.x),
					static_cast<NS::UInteger>(region.textureOffset.y),
					static_cast<NS::UInteger>(region.textureOffset.z)
				),
				MTL::Size::Make(region.textureExtent.width, region.textureExtent.height, region.textureExtent.depth),
				buffer,
				region.bufferOffset,
				bytesPerRow,
				bytesPerImage
			);
		}
		encoder->endEncoding();
		return succeed(error);
	}

	bool metal_copy_texture(void * impl, TextureHandle dst, TextureHandle src, std::span<const TextureCopy> regions, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal.copyTexture");

		auto * object					   = static_cast<MetalObject *>(impl);
		MetalDevice * device			   = object->owner;
		MTL::CommandBuffer * commandBuffer = cmd_buffer_of(object);
		if (commandBuffer == nullptr)
		{
			return fail(error, ErrorCode::eInvalidState, "command list has no command buffer");
		}

		MTL::Texture * source	   = resolve_texture(device, src);
		MTL::Texture * destination = resolve_texture(device, dst);
		if (source == nullptr || destination == nullptr)
		{
			return fail(error, ErrorCode::eInvalidHandle, "copyTexture names a texture this device never created");
		}

		const NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());
		MTL::BlitCommandEncoder * encoder			  = begin_blit(object, error);
		if (encoder == nullptr)
		{
			return false;
		}
		for (const TextureCopy & region : regions)
		{
			encoder->copyFromTexture(
				source,
				region.srcSubresource.layer,
				region.srcSubresource.mip,
				MTL::Origin::Make(
					static_cast<NS::UInteger>(region.srcOffset.x),
					static_cast<NS::UInteger>(region.srcOffset.y),
					static_cast<NS::UInteger>(region.srcOffset.z)
				),
				MTL::Size::Make(region.extent.width, region.extent.height, region.extent.depth),
				destination,
				region.dstSubresource.layer,
				region.dstSubresource.mip,
				MTL::Origin::Make(
					static_cast<NS::UInteger>(region.dstOffset.x),
					static_cast<NS::UInteger>(region.dstOffset.y),
					static_cast<NS::UInteger>(region.dstOffset.z)
				)
			);
		}
		encoder->endEncoding();
		return succeed(error);
	}

	bool metal_blit(
		void * impl,
		[[maybe_unused]] TextureHandle dst,
		[[maybe_unused]] TextureHandle src,
		[[maybe_unused]] std::span<const TextureBlit> regions,
		[[maybe_unused]] Filter filter,
		Error * error
	) noexcept
	{
		[[maybe_unused]] auto * object = static_cast<MetalObject *>(impl);
		return fail(error, ErrorCode::eUnsupportedFeature, "Metal has no scaled blit; use generateMips or a compute downsample");
	}

	bool metal_generate_mips(void * impl, TextureHandle texture, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal.generateMips");

		auto * object					   = static_cast<MetalObject *>(impl);
		MetalDevice * device			   = object->owner;
		MTL::CommandBuffer * commandBuffer = cmd_buffer_of(object);
		if (commandBuffer == nullptr)
		{
			return fail(error, ErrorCode::eInvalidState, "command list has no command buffer");
		}

		MTL::Texture * tex = resolve_texture(device, texture);
		if (tex == nullptr)
		{
			return fail(error, ErrorCode::eInvalidHandle, "generateMips names a texture this device never created");
		}

		if (tex->mipmapLevelCount() <= 1)
		{
			return succeed(error);
		}

		auto * const tracked = device->textures.resolve(texture, kHandleAlreadyChecked);
		if (tracked != nullptr && (is_compressed_format(tracked->format) || is_integer_format(tracked->format) || is_depth_format(tracked->format)))
		{
			return fail(
				error,
				ErrorCode::eUnsupportedFeature,
				"generateMips needs a linear-filterable, renderable format (not block-compressed, integer, or depth)"
			);
		}

		const NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());
		MTL::BlitCommandEncoder * encoder			  = begin_blit(object, error);
		if (encoder == nullptr)
		{
			return false;
		}
		encoder->generateMipmaps(tex);
		encoder->endEncoding();
		return succeed(error);
	}

	bool metal_clear_buffer(void * impl, BufferHandle buffer, std::uint64_t offset, std::uint64_t size, std::uint32_t value, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal.clearBuffer");

		auto * object					   = static_cast<MetalObject *>(impl);
		MetalDevice * device			   = object->owner;
		MTL::CommandBuffer * commandBuffer = cmd_buffer_of(object);
		if (commandBuffer == nullptr)
		{
			return fail(error, ErrorCode::eInvalidState, "command list has no command buffer");
		}

		MTL::Buffer * destination = resolve_buffer(device, buffer);
		if (destination == nullptr)
		{
			return fail(error, ErrorCode::eInvalidHandle, "clearBuffer names a buffer this device never created");
		}

		NS::SharedPtr<MTL::Buffer> staging = NS::TransferPtr(device->device->newBuffer(size, MTL::ResourceStorageModeShared));
		if (staging.get() == nullptr)
		{
			return fail(error, ErrorCode::eOutOfDeviceMemory, "Metal clear staging buffer allocation failed");
		}
		auto * words				  = static_cast<std::uint32_t *>(staging->contents());
		const std::uint64_t wordCount = size / 4;
		for (std::uint64_t i = 0; i < wordCount; ++i)
		{
			words[i] = value;
		}

		const NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());
		MTL::BlitCommandEncoder * encoder			  = begin_blit(object, error);
		if (encoder == nullptr)
		{
			return false;
		}
		encoder->copyFromBuffer(staging.get(), 0, destination, offset, wordCount * 4);
		encoder->endEncoding();
		object->list->keepAlive.push_back(std::move(staging));
		return succeed(error);
	}

	bool metal_clear_texture(
		void * impl,
		TextureHandle texture,
		const ClearColor & color,
		std::span<const TextureSubresourceRange> ranges,
		Error * error
	) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal.clearTexture");

		auto * object					   = static_cast<MetalObject *>(impl);
		MetalDevice * device			   = object->owner;
		MTL::CommandBuffer * commandBuffer = cmd_buffer_of(object);
		if (commandBuffer == nullptr)
		{
			return fail(error, ErrorCode::eInvalidState, "command list has no command buffer");
		}

		const MetalTextureSlot * slot = device->textures.resolve(texture, kHandleAlreadyChecked);
		if (slot == nullptr || slot->texture.get() == nullptr)
		{
			return fail(error, ErrorCode::eInvalidHandle, "clearTexture names a texture this device never created");
		}

		MTL::Texture * tex = slot->texture.get();

		if (!slot->usage.contains(TextureUsage::eColorAttachment))
		{
			return fail(error, ErrorCode::eInvalidArgument, "clearTexture needs a texture usable as a color attachment, which is what Metal clears through");
		}

		end_active_encoders(object);
		const NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

		for (const TextureSubresourceRange & range : ranges)
		{
			if (range.aspects.contains(TextureAspect::eDepth) || range.aspects.contains(TextureAspect::eStencil))
			{
				return fail(error, ErrorCode::eUnsupportedFeature, "Metal clearTexture clears color aspects only");
			}

			const auto textureMips	 = static_cast<std::uint32_t>(tex->mipmapLevelCount());
			const auto textureLayers = static_cast<std::uint32_t>(tex->arrayLength());
			if (range.baseMip >= textureMips || range.baseLayer >= textureLayers)
			{
				return fail(error, ErrorCode::eInvalidArgument, "clearTexture range starts past the end of the texture");
			}

			const std::uint32_t mipCount   = range.mipCount == kAllMips ? textureMips - range.baseMip : range.mipCount;
			const std::uint32_t layerCount = range.layerCount == kAllLayers ? textureLayers - range.baseLayer : range.layerCount;

			for (std::uint32_t mip = range.baseMip; mip < range.baseMip + mipCount; ++mip)
			{
				for (std::uint32_t layer = range.baseLayer; layer < range.baseLayer + layerCount; ++layer)
				{
					MTL::RenderPassDescriptor * pass						 = MTL::RenderPassDescriptor::alloc()->init();
					const NS::SharedPtr<MTL::RenderPassDescriptor> passGuard = NS::TransferPtr(pass);

					MTL::RenderPassColorAttachmentDescriptor * attachment = pass->colorAttachments()->object(0);
					attachment->setTexture(tex);
					attachment->setLevel(mip);
					attachment->setSlice(layer);
					attachment->setLoadAction(MTL::LoadActionClear);
					attachment->setStoreAction(MTL::StoreActionStore);
					attachment->setClearColor(MTL::ClearColor::Make(color.r, color.g, color.b, color.a));

					MTL::RenderCommandEncoder * encoder = commandBuffer->renderCommandEncoder(pass);
					if (encoder == nullptr)
					{
						return fail(error, ErrorCode::eNativeApiError, "Metal clear render command encoder creation failed");
					}
					consume_alias_wait(object->list, encoder);
					encoder->endEncoding();
				}
			}
		}

		return succeed(error);
	}

	bool metal_resolve_texture(void * impl, TextureHandle dst, TextureHandle src, std::span<const TextureResolve> regions, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal.resolveTexture");

		auto * object					   = static_cast<MetalObject *>(impl);
		MetalDevice * device			   = object->owner;
		MTL::CommandBuffer * commandBuffer = cmd_buffer_of(object);
		if (commandBuffer == nullptr)
		{
			return fail(error, ErrorCode::eInvalidState, "command list has no command buffer");
		}

		MTL::Texture * source	   = resolve_texture(device, src);
		MTL::Texture * destination = resolve_texture(device, dst);
		if (source == nullptr || destination == nullptr)
		{
			return fail(error, ErrorCode::eInvalidHandle, "resolveTexture names a texture this device never created");
		}

		end_active_encoders(object);
		const NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

		for (const TextureResolve & region : regions)
		{
			const bool wholeSlice = region.srcOffset.x == 0 && region.srcOffset.y == 0 && region.srcOffset.z == 0 && region.dstOffset.x == 0 &&
									region.dstOffset.y == 0 && region.dstOffset.z == 0 &&
									region.extent.width == static_cast<std::uint32_t>(source->width() >> region.srcSubresource.mip) &&
									region.extent.height == static_cast<std::uint32_t>(source->height() >> region.srcSubresource.mip);
			if (!wholeSlice)
			{
				return fail(error, ErrorCode::eUnsupportedFeature, "Metal resolveTexture resolves a whole subresource, not a sub-rectangle");
			}

			MTL::RenderPassDescriptor * pass						 = MTL::RenderPassDescriptor::alloc()->init();
			const NS::SharedPtr<MTL::RenderPassDescriptor> passGuard = NS::TransferPtr(pass);

			MTL::RenderPassColorAttachmentDescriptor * attachment = pass->colorAttachments()->object(0);
			attachment->setTexture(source);
			attachment->setLevel(region.srcSubresource.mip);
			attachment->setSlice(region.srcSubresource.layer);
			attachment->setLoadAction(MTL::LoadActionLoad);
			attachment->setStoreAction(MTL::StoreActionMultisampleResolve);
			attachment->setResolveTexture(destination);
			attachment->setResolveLevel(region.dstSubresource.mip);
			attachment->setResolveSlice(region.dstSubresource.layer);

			MTL::RenderCommandEncoder * encoder = commandBuffer->renderCommandEncoder(pass);
			if (encoder == nullptr)
			{
				return fail(error, ErrorCode::eNativeApiError, "Metal resolve render command encoder creation failed");
			}
			consume_alias_wait(object->list, encoder);
			encoder->endEncoding();
		}

		return succeed(error);
	}

}

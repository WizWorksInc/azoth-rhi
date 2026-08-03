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

#include "backends/metal/internal.hpp"

namespace azo::rhi::metal
{
	bool MetalCmdBegin(void * impl, Error * error) noexcept
	{
		auto * object	   = static_cast<MetalObject *>(impl);
		MetalCmdList * rec = object->list;

		// Metal command buffers are single use so each Begin starts a fresh one. The caller has already waited for this slot's previous submission to finish so last
		// frame's staging buffers can drop. The buffer comes from the list's pool type queue so it runs on that type's command queue.
		MTL::CommandQueue * commandQueue = object->owner->CommandQueueFor(object->queueType);
		if (commandQueue == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidState, "Metal command list has no command queue for its type");
		}

		const NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());
		MTL::CommandBuffer * commandBuffer			  = commandQueue->commandBuffer();
		if (commandBuffer == nullptr)
		{
			return Fail(error, ErrorCode::eNativeApiError, "Metal command buffer allocation failed");
		}
		rec->commandBuffer = NS::RetainPtr(commandBuffer);
		if (!rec->debugName.empty())
		{
			commandBuffer->setLabel(NS::String::string(rec->debugName.c_str(), NS::UTF8StringEncoding));
		}
		rec->keepAlive.clear();

		// The recorded scopes start clean. A wait left pending, a label left open or a timestamp left owing by the previous recording belongs to a command buffer
		// that is gone.
		rec->aliasWaitPending = false;
		rec->debugLabelScopes.clear();
		rec->pendingEndTimestamp.reset();

		rec->lifecycle = 1;
		return Succeed(error);
	}

	bool MetalCmdEnd(void * impl, Error * error) noexcept
	{
		auto * object	   = static_cast<MetalObject *>(impl);
		MetalCmdList * rec = object->list;

		// Close any encoder left open by the recorded scope before the command buffer is committed.
		if (rec->renderEncoder.get() != nullptr)
		{
			rec->renderEncoder->endEncoding();
			rec->renderEncoder.reset();
		}
		if (rec->computeEncoder.get() != nullptr)
		{
			rec->computeEncoder->endEncoding();
			rec->computeEncoder.reset();
		}

		rec->lifecycle = 2;
		return Succeed(error);
	}

	// Metal tracks hazards between the commands of one command buffer itself so there is no native barrier for the batch to lower to. What the batch claims about
	// each resource's state is checked above this, where every backend gets the same answer.
	bool MetalCmdBarriers([[maybe_unused]] void * impl, [[maybe_unused]] const BarrierBatch & barriers, Error * error) noexcept
	{
		return Succeed(error);
	}

	/*
	 * Aliasing is the one hazard Metal will not track here. Heaps are MTLHeapTypePlacement so the RHI can place resources at the offsets the caller picked and a
	 * placement heap gives up hazard tracking for that control. Two resources over the same bytes are ordered only by fences the backend records. So the barrier
	 * closes the encoder reading the before-resource, updates a fence and leaves a wait for the next encoder to open.
	 */
	bool MetalCmdAliasBarriers(void * impl, std::span<const AliasBarrier> barriers, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal.aliasBarriers");

		auto * object	   = static_cast<MetalObject *>(impl);
		MetalCmdList * rec = object->list;
		if (rec == nullptr || rec->commandBuffer.get() == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidState, "command list has no command buffer");
		}
		if (barriers.empty())
		{
			return Succeed(error);
		}

		if (rec->aliasFence.get() == nullptr)
		{
			rec->aliasFence = NS::TransferPtr(object->owner->device->newFence());
			if (rec->aliasFence.get() == nullptr)
			{
				return Fail(error, ErrorCode::eNativeApiError, "Metal alias fence allocation failed");
			}
		}

		const NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());
		if (rec->renderEncoder.get() != nullptr)
		{
			rec->renderEncoder->updateFence(rec->aliasFence.get(), MTL::RenderStageFragment);
			rec->renderEncoder->endEncoding();
			rec->renderEncoder.reset();
		}
		else if (rec->computeEncoder.get() != nullptr)
		{
			rec->computeEncoder->updateFence(rec->aliasFence.get());
			rec->computeEncoder->endEncoding();
			rec->computeEncoder.reset();
		}
		else
		{
			// Nothing is open so the update needs an encoder of its own. An empty blit encoder is the cheapest one that can carry a fence.
			MTL::BlitCommandEncoder * encoder = rec->commandBuffer->blitCommandEncoder();
			if (encoder == nullptr)
			{
				return Fail(error, ErrorCode::eNativeApiError, "Metal blit command encoder creation failed");
			}
			encoder->updateFence(rec->aliasFence.get());
			encoder->endEncoding();
		}

		rec->aliasWaitPending = true;
		return Succeed(error);
	}

	/*
	 * A label opened inside a rendering or dispatch scope goes on the open encoder, since Metal refuses a command-buffer group while an encoder is live. One
	 * opened between scopes goes on the command buffer. Which one it was is remembered so the pop reaches the same object.
	 *
	 * Labels turned off in the DeviceDesc leave both ends doing nothing, tracking stack included. Pushing a scope nothing opened would make the pop balance
	 * against an entry that was never a group.
	 */
	bool MetalCmdBeginDebugLabel(void * impl, CString name, [[maybe_unused]] std::uint32_t color, Error * error) noexcept
	{
		auto * object	   = static_cast<MetalObject *>(impl);
		MetalCmdList * rec = object->list;
		if (rec == nullptr || rec->commandBuffer.get() == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidState, "command list has no command buffer");
		}

		if (!object->owner->debugLabels)
		{
			return Succeed(error);
		}

		const NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());
		NS::String * label							  = NS::String::string(name == nullptr ? "" : name, NS::UTF8StringEncoding);

		MTL::CommandEncoder * encoder = rec->renderEncoder.get() != nullptr ? static_cast<MTL::CommandEncoder *>(rec->renderEncoder.get())
																			: static_cast<MTL::CommandEncoder *>(rec->computeEncoder.get());
		if (!detail::TryPushBack(rec->debugLabelScopes, encoder))
		{
			return Fail(error, ErrorCode::eOutOfHostMemory, "Metal debug label tracking allocation failed");
		}

		if (encoder != nullptr)
		{
			encoder->pushDebugGroup(label);
		}
		else
		{
			rec->commandBuffer->pushDebugGroup(label);
		}
		return Succeed(error);
	}

	bool MetalCmdEndDebugLabel(void * impl, Error * error) noexcept
	{
		auto * object	   = static_cast<MetalObject *>(impl);
		MetalCmdList * rec = object->list;
		if (rec == nullptr || rec->commandBuffer.get() == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidState, "command list has no command buffer");
		}

		if (!object->owner->debugLabels)
		{
			return Succeed(error);
		}

		if (rec->debugLabelScopes.empty())
		{
			return Fail(error, ErrorCode::eValidationFailed, "endDebugLabel without a matching beginDebugLabel");
		}

		MTL::CommandEncoder * opened = rec->debugLabelScopes.back();
		rec->debugLabelScopes.pop_back();

		if (opened == nullptr)
		{
			rec->commandBuffer->popDebugGroup();
			return Succeed(error);
		}

		// The encoder the label was pushed on may have ended since, which already closed the group with it. Only pop when that same encoder is still live, since
		// whatever is open now is a different encoder that never saw this label.
		MTL::CommandEncoder * live = rec->renderEncoder.get() != nullptr ? static_cast<MTL::CommandEncoder *>(rec->renderEncoder.get())
																		 : static_cast<MTL::CommandEncoder *>(rec->computeEncoder.get());
		if (opened == live)
		{
			live->popDebugGroup();
		}
		return Succeed(error);
	}

	bool MetalCopyBuffer(
		void * impl, BufferHandle dst, std::uint64_t dstOffset, BufferHandle src, std::uint64_t srcOffset, std::uint64_t size, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal.copyBuffer");

		auto * object					   = static_cast<MetalObject *>(impl);
		MetalDevice * device			   = object->owner;
		MTL::CommandBuffer * commandBuffer = CmdBufferOf(object);
		if (commandBuffer == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidState, "command list has no command buffer");
		}

		MTL::Buffer * source	  = ResolveBuffer(device, src);
		MTL::Buffer * destination = ResolveBuffer(device, dst);
		if (source == nullptr || destination == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "copyBuffer names a buffer this device never created");
		}

		const NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());
		MTL::BlitCommandEncoder * encoder			  = BeginBlit(object);
		encoder->copyFromBuffer(source, srcOffset, destination, dstOffset, size);
		encoder->endEncoding();
		return Succeed(error);
	}

	bool MetalCopyBufferToTexture(void * impl, TextureHandle dst, BufferHandle src, std::span<const BufferTextureCopy> regions, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal.copyBufferToTexture");

		auto * object					   = static_cast<MetalObject *>(impl);
		MetalDevice * device			   = object->owner;
		MTL::CommandBuffer * commandBuffer = CmdBufferOf(object);
		if (commandBuffer == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidState, "command list has no command buffer");
		}

		MTL::Texture * texture = ResolveTexture(device, dst);
		MTL::Buffer * buffer   = ResolveBuffer(device, src);
		if (texture == nullptr || buffer == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "copyBufferToTexture names a resource this device never created");
		}

		const Format format = ResolveTextureFormat(device, dst);
		if (!detail::HasLinearLayout(format))
		{
			return Fail(error, ErrorCode::eUnsupportedFeature, "copyBufferToTexture on a combined depth-stencil format, whose aspects copy separately");
		}

		const NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());
		MTL::BlitCommandEncoder * encoder			  = BeginBlit(object);
		for (const BufferTextureCopy & region : regions)
		{
			// Block arithmetic, not texel arithmetic. A row length is given in texels so it converts through the block grid and the image height counts block rows, not
			// texel rows, which for a compressed format differ by the block height.
			const std::uint32_t rowTexels	 = region.bufferRowLength != 0 ? region.bufferRowLength : region.textureExtent.width;
			const std::uint32_t imageRows	 = region.bufferImageHeight != 0 ? region.bufferImageHeight : region.textureExtent.height;
			const NS::UInteger bytesPerRow	 = static_cast<NS::UInteger>(detail::TightRowPitch(format, rowTexels));
			const NS::UInteger bytesPerImage = bytesPerRow * detail::BlockRows(format, imageRows);
			encoder->copyFromBuffer(buffer,
				region.bufferOffset,
				bytesPerRow,
				bytesPerImage,
				MTL::Size::Make(region.textureExtent.width, region.textureExtent.height, region.textureExtent.depth),
				texture,
				region.subresource.layer,
				region.subresource.mip,
				MTL::Origin::Make(static_cast<NS::UInteger>(region.textureOffset.x),
					static_cast<NS::UInteger>(region.textureOffset.y),
					static_cast<NS::UInteger>(region.textureOffset.z)));
		}
		encoder->endEncoding();
		return Succeed(error);
	}

	bool MetalCopyTextureToBuffer(void * impl, BufferHandle dst, TextureHandle src, std::span<const BufferTextureCopy> regions, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal.copyTextureToBuffer");

		auto * object					   = static_cast<MetalObject *>(impl);
		MetalDevice * device			   = object->owner;
		MTL::CommandBuffer * commandBuffer = CmdBufferOf(object);
		if (commandBuffer == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidState, "command list has no command buffer");
		}

		MTL::Texture * texture = ResolveTexture(device, src);
		MTL::Buffer * buffer   = ResolveBuffer(device, dst);
		if (texture == nullptr || buffer == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "copyTextureToBuffer names a resource this device never created");
		}

		const Format format = ResolveTextureFormat(device, src);
		if (!detail::HasLinearLayout(format))
		{
			return Fail(error, ErrorCode::eUnsupportedFeature, "copyTextureToBuffer on a combined depth-stencil format, whose aspects copy separately");
		}

		const NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());
		MTL::BlitCommandEncoder * encoder			  = BeginBlit(object);
		for (const BufferTextureCopy & region : regions)
		{
			// Block arithmetic, not texel arithmetic. See the matching comment in the buffer-to-texture direction.
			const std::uint32_t rowTexels	 = region.bufferRowLength != 0 ? region.bufferRowLength : region.textureExtent.width;
			const std::uint32_t imageRows	 = region.bufferImageHeight != 0 ? region.bufferImageHeight : region.textureExtent.height;
			const NS::UInteger bytesPerRow	 = static_cast<NS::UInteger>(detail::TightRowPitch(format, rowTexels));
			const NS::UInteger bytesPerImage = bytesPerRow * detail::BlockRows(format, imageRows);
			encoder->copyFromTexture(texture,
				region.subresource.layer,
				region.subresource.mip,
				MTL::Origin::Make(static_cast<NS::UInteger>(region.textureOffset.x),
					static_cast<NS::UInteger>(region.textureOffset.y),
					static_cast<NS::UInteger>(region.textureOffset.z)),
				MTL::Size::Make(region.textureExtent.width, region.textureExtent.height, region.textureExtent.depth),
				buffer,
				region.bufferOffset,
				bytesPerRow,
				bytesPerImage);
		}
		encoder->endEncoding();
		return Succeed(error);
	}

	bool MetalCopyTexture(void * impl, TextureHandle dst, TextureHandle src, std::span<const TextureCopy> regions, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal.copyTexture");

		auto * object					   = static_cast<MetalObject *>(impl);
		MetalDevice * device			   = object->owner;
		MTL::CommandBuffer * commandBuffer = CmdBufferOf(object);
		if (commandBuffer == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidState, "command list has no command buffer");
		}

		MTL::Texture * source	   = ResolveTexture(device, src);
		MTL::Texture * destination = ResolveTexture(device, dst);
		if (source == nullptr || destination == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "copyTexture names a texture this device never created");
		}

		const NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());
		MTL::BlitCommandEncoder * encoder			  = BeginBlit(object);
		for (const TextureCopy & region : regions)
		{
			encoder->copyFromTexture(source,
				region.srcSubresource.layer,
				region.srcSubresource.mip,
				MTL::Origin::Make(static_cast<NS::UInteger>(region.srcOffset.x),
					static_cast<NS::UInteger>(region.srcOffset.y),
					static_cast<NS::UInteger>(region.srcOffset.z)),
				MTL::Size::Make(region.extent.width, region.extent.height, region.extent.depth),
				destination,
				region.dstSubresource.layer,
				region.dstSubresource.mip,
				MTL::Origin::Make(static_cast<NS::UInteger>(region.dstOffset.x),
					static_cast<NS::UInteger>(region.dstOffset.y),
					static_cast<NS::UInteger>(region.dstOffset.z)));
		}
		encoder->endEncoding();
		return Succeed(error);
	}

	// Metal has no scaled image copy in the blit encoder so an explicit Blit is unsupported. Mip generation goes through the native generateMipmaps below and
	// arbitrary downsamples through a compute pass.
	bool MetalBlit(void * impl, [[maybe_unused]] TextureHandle dst, [[maybe_unused]] TextureHandle src, [[maybe_unused]] std::span<const TextureBlit> regions,
		[[maybe_unused]] Filter filter, Error * error) noexcept
	{
		[[maybe_unused]] auto * object = static_cast<MetalObject *>(impl);
		return Fail(error, ErrorCode::eUnsupportedFeature, "Metal has no scaled blit; use generateMips or a compute downsample");
	}

	bool MetalGenerateMips(void * impl, TextureHandle texture, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal.generateMips");

		auto * object					   = static_cast<MetalObject *>(impl);
		MetalDevice * device			   = object->owner;
		MTL::CommandBuffer * commandBuffer = CmdBufferOf(object);
		if (commandBuffer == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidState, "command list has no command buffer");
		}

		MTL::Texture * tex = ResolveTexture(device, texture);
		if (tex == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "generateMips names a texture this device never created");
		}

		if (tex->mipmapLevelCount() <= 1)
		{
			return Succeed(error);
		}

		// generateMipmaps cannot filter block-compressed formats. Metal's own limit and not a rule the RHI imposes so it is asked whatever the mode and refused here
		// without recording a blit Metal refuses at commit.
		auto * const tracked = device->textures.Resolve(texture, kHandleAlreadyChecked);
		if (tracked != nullptr && IsCompressedFormat(tracked->format))
		{
			return Fail(error, ErrorCode::eUnsupportedFeature, "generateMips cannot filter a block-compressed format on Metal");
		}

		const NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());
		MTL::BlitCommandEncoder * encoder			  = BeginBlit(object);
		encoder->generateMipmaps(tex);
		encoder->endEncoding();
		return Succeed(error);
	}

	bool MetalClearBuffer(void * impl, BufferHandle buffer, std::uint64_t offset, std::uint64_t size, std::uint32_t value, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal.clearBuffer");

		auto * object					   = static_cast<MetalObject *>(impl);
		MetalDevice * device			   = object->owner;
		MTL::CommandBuffer * commandBuffer = CmdBufferOf(object);
		if (commandBuffer == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidState, "command list has no command buffer");
		}

		MTL::Buffer * destination = ResolveBuffer(device, buffer);
		if (destination == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "clearBuffer names a buffer this device never created");
		}

		// Metal blit fill is byte granular so a 32-bit pattern is staged in a shared buffer and copied in.
		NS::SharedPtr<MTL::Buffer> staging = NS::TransferPtr(device->device->newBuffer(size, MTL::ResourceStorageModeShared));
		if (staging.get() == nullptr)
		{
			return Fail(error, ErrorCode::eOutOfDeviceMemory, "Metal clear staging buffer allocation failed");
		}
		auto * words				  = static_cast<std::uint32_t *>(staging->contents());
		const std::uint64_t wordCount = size / 4;
		for (std::uint64_t i = 0; i < wordCount; ++i)
		{
			words[i] = value;
		}

		const NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());
		MTL::BlitCommandEncoder * encoder			  = BeginBlit(object);
		encoder->copyFromBuffer(staging.get(), 0, destination, offset, wordCount * 4);
		encoder->endEncoding();
		object->list->keepAlive.push_back(std::move(staging));
		return Succeed(error);
	}

	// Metal has no standalone texture clear in the blit encoder. The load action does it instead. Each subresource gets a render pass attaching that one slice,
	// clearing on load and storing, ended with no draw. One encoder per mip and layer is the price.
	bool MetalClearTexture(
		void * impl, TextureHandle texture, const ClearColor & color, std::span<const TextureSubresourceRange> ranges, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal.clearTexture");

		auto * object					   = static_cast<MetalObject *>(impl);
		MetalDevice * device			   = object->owner;
		MTL::CommandBuffer * commandBuffer = CmdBufferOf(object);
		if (commandBuffer == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidState, "command list has no command buffer");
		}

		const MetalTextureSlot * slot = device->textures.Resolve(texture, kHandleAlreadyChecked);
		if (slot == nullptr || slot->texture.get() == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "clearTexture names a texture this device never created");
		}

		MTL::Texture * tex = slot->texture.get();

		/*
		 * Metal clears a color texture by opening a render pass over it, which a texture created without MTLTextureUsageRenderTarget cannot be an attachment
		 * of. Refused here with a reason instead of left to the validation layer, which traps in a debug build and reads back nothing at all in a release one.
		 *
		 * The same refusal Direct3D 12 makes, for the same reason: it clears through a render target view.
		 */
		if (!slot->usage.Contains(TextureUsage::eColorAttachment))
		{
			return Fail(error, ErrorCode::eInvalidArgument, "clearTexture needs a texture usable as a color attachment, which is what Metal clears through");
		}

		EndActiveEncoders(object);
		const NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

		for (const TextureSubresourceRange & range : ranges)
		{
			// The clear value arrives as a color and a depth or stencil slice would need the depth attachment and a different clear value so a range naming one is
			// refused, not cleared to something the caller did not ask for.
			if (range.aspects.Contains(TextureAspect::eDepth) || range.aspects.Contains(TextureAspect::eStencil))
			{
				return Fail(error, ErrorCode::eUnsupportedFeature, "Metal clearTexture clears color aspects only");
			}

			const auto textureMips	 = static_cast<std::uint32_t>(tex->mipmapLevelCount());
			const auto textureLayers = static_cast<std::uint32_t>(tex->arrayLength());
			if (range.baseMip >= textureMips || range.baseLayer >= textureLayers)
			{
				return Fail(error, ErrorCode::eInvalidArgument, "clearTexture range starts past the end of the texture");
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
						return Fail(error, ErrorCode::eNativeApiError, "Metal clear render command encoder creation failed");
					}
					ConsumeAliasWait(object->list, encoder);
					encoder->endEncoding();
				}
			}
		}

		return Succeed(error);
	}

	// Metal resolves through the store action and not a copy: the multisampled source is attached, the single-sample destination is named as its resolve target
	// and ending the encoder with no draw performs the resolve.
	bool MetalResolveTexture(void * impl, TextureHandle dst, TextureHandle src, std::span<const TextureResolve> regions, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal.resolveTexture");

		auto * object					   = static_cast<MetalObject *>(impl);
		MetalDevice * device			   = object->owner;
		MTL::CommandBuffer * commandBuffer = CmdBufferOf(object);
		if (commandBuffer == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidState, "command list has no command buffer");
		}

		MTL::Texture * source	   = ResolveTexture(device, src);
		MTL::Texture * destination = ResolveTexture(device, dst);
		if (source == nullptr || destination == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "resolveTexture names a texture this device never created");
		}

		EndActiveEncoders(object);
		const NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

		for (const TextureResolve & region : regions)
		{
			// A store-action resolve covers the whole attachment. A region naming a sub-rectangle cannot be honored and resolving everything instead would write outside
			// what the caller asked for so it is refused.
			const bool wholeSlice = region.srcOffset.x == 0 && region.srcOffset.y == 0 && region.srcOffset.z == 0 && region.dstOffset.x == 0 &&
									region.dstOffset.y == 0 && region.dstOffset.z == 0 &&
									region.extent.width == static_cast<std::uint32_t>(source->width() >> region.srcSubresource.mip) &&
									region.extent.height == static_cast<std::uint32_t>(source->height() >> region.srcSubresource.mip);
			if (!wholeSlice)
			{
				return Fail(error, ErrorCode::eUnsupportedFeature, "Metal resolveTexture resolves a whole subresource, not a sub-rectangle");
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
				return Fail(error, ErrorCode::eNativeApiError, "Metal resolve render command encoder creation failed");
			}
			ConsumeAliasWait(object->list, encoder);
			encoder->endEncoding();
		}

		return Succeed(error);
	}

} // namespace azo::rhi::metal

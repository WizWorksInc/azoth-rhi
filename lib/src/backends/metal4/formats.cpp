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

#include "backends/metal4/internal.hpp"

namespace azo::rhi::metal4
{
	bool GetTextureInfo(void * impl, const TextureHandle texture, TextureInfo * out, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal4.getTextureInfo");
		auto * device = static_cast<Metal4Device *>(impl);
		if (out == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidArgument, "getTextureInfo output pointer is null");
		}

		const Metal4TextureSlot * const slot = device->textures.Resolve(texture, false);
		if (slot == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "getTextureInfo names a texture this device did not create");
		}

		// A back buffer never came from a TextureDesc, so there is nothing here to report that would not be invented. The swapchain owns that description and
		// answers for it.
		if (slot->lifetime == SlotLifetime::eSwapchainBorrowed)
		{
			return Fail(error, ErrorCode::eUnsupportedFeature, "a swapchain back buffer has no texture description; ask the swapchain instead");
		}

		// Metal reports what the texture actually cost, which for a placed one is its share of the heap.
		const std::uint64_t allocated = slot->texture.get() != nullptr ? slot->texture->allocatedSize() : 0;

		*out = TextureInfo{ .desc = slot->desc, .allocationSize = allocated };
		return true;
	}

	bool GetBufferInfo(void * impl, const BufferHandle buffer, BufferInfo * out, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal4.getBufferInfo");
		auto * device = static_cast<Metal4Device *>(impl);
		if (out == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidArgument, "getBufferInfo output pointer is null");
		}

		const Metal4BufferSlot * const slot = device->buffers.Resolve(buffer, false);
		if (slot == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "getBufferInfo names a buffer this device did not create");
		}

		std::uint64_t allocated = 0;
		MemoryAccess access		= MemoryAccess::eGpuOnly;
		if (slot->buffer.get() != nullptr)
		{
			allocated = slot->buffer->allocatedSize();

			// Shared and managed are both host reachable and Metal keeps shared coherent, so the split is by storage mode and not by what was asked.
			access = slot->buffer->storageMode() == MTL::StorageModePrivate ? MemoryAccess::eGpuOnly : MemoryAccess::eCpuVisibleCoherent;
		}

		*out = BufferInfo{ .desc = slot->desc, .allocationSize = allocated, .memoryAccess = access };
		return true;
	}

	FormatSupport DeviceFormatSupport(void * impl, Format format) noexcept
	{
		auto * device = static_cast<Metal4Device *>(impl);

		FormatSupport support{ .format = format };
		if (MetalPixelFormat(format) == MTL::PixelFormatInvalid)
		{
			return support;
		}
		if (format == Format::eD24UNormS8UInt && !device->device->isDepth24Stencil8PixelFormatSupported())
		{
			return support;
		}

		const bool depth	  = IsDepthFormat(format);
		const bool compressed = IsCompressedFormat(format);
		const bool integer	  = IsIntegerFormat(format);

		support.sampled				   = true;
		support.copySrc				   = true;
		support.copyDst				   = true;
		support.linearFiltering		   = !depth && !integer;
		support.storage				   = !depth && !compressed;
		support.colorAttachment		   = !depth && !compressed;
		support.blendable			   = !depth && !compressed && !integer;
		support.depthStencilAttachment = depth;

		return support;
	}

} // namespace azo::rhi::metal4

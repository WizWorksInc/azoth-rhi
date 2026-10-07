// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#include "azoth/rhi/backend/support/slot_map.hpp"
#include "azoth/rhi/core/enums.hpp"
#include "azoth/rhi/core/profiling.hpp"
#include "azoth/rhi/core/resource_handles.hpp"
#include "azoth/rhi/core/result.hpp"
#include "azoth/rhi/device/device.hpp"
#include "azoth/rhi/resources/resources.hpp"
#include "backends/metal4/internal.hpp"
#include "backends/metal_common/conversions.hpp"
#include <Metal/MTLPixelFormat.hpp>
#include <Metal/MTLResource.hpp>
#include <cstdint>

namespace azo::rhi::metal4
{
	bool metal4_get_texture_info(void * impl, const TextureHandle texture, TextureInfo * out, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal4.getTextureInfo");
		auto * device = static_cast<Metal4Device *>(impl);
		if (out == nullptr)
		{
			return fail(error, ErrorCode::eInvalidArgument, "getTextureInfo output pointer is null");
		}

		const Metal4TextureSlot * const slot = device->textures.resolve(texture, false);
		if (slot == nullptr)
		{
			return fail(error, ErrorCode::eInvalidHandle, "getTextureInfo names a texture this device did not create");
		}

		if (slot->lifetime == SlotLifetime::eSwapchainBorrowed)
		{
			return fail(error, ErrorCode::eUnsupportedFeature, "a swapchain back buffer has no texture description; ask the swapchain instead");
		}

		const std::uint64_t allocated = slot->texture.get() != nullptr ? slot->texture->allocatedSize() : 0;

		*out = TextureInfo{ .desc = slot->desc, .allocationSize = allocated };
		return true;
	}

	bool metal4_get_buffer_info(void * impl, const BufferHandle buffer, BufferInfo * out, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal4.getBufferInfo");
		auto * device = static_cast<Metal4Device *>(impl);
		if (out == nullptr)
		{
			return fail(error, ErrorCode::eInvalidArgument, "getBufferInfo output pointer is null");
		}

		const Metal4BufferSlot * const slot = device->buffers.resolve(buffer, false);
		if (slot == nullptr)
		{
			return fail(error, ErrorCode::eInvalidHandle, "getBufferInfo names a buffer this device did not create");
		}

		std::uint64_t allocated = 0;
		MemoryAccess access		= MemoryAccess::eGpuOnly;
		if (slot->buffer.get() != nullptr)
		{
			allocated = slot->buffer->allocatedSize();

			access = slot->buffer->storageMode() == MTL::StorageModePrivate ? MemoryAccess::eGpuOnly : MemoryAccess::eCpuVisibleCoherent;
		}

		*out = BufferInfo{ .desc = slot->desc, .allocationSize = allocated, .memoryAccess = access };
		return true;
	}

	FormatSupport metal4_device_format_support(void * impl, Format format) noexcept
	{
		auto * device = static_cast<Metal4Device *>(impl);

		FormatSupport support{ .format = format };
		if (metal_pixel_format(format) == MTL::PixelFormatInvalid)
		{
			return support;
		}
		if (format == Format::eD24UNormS8UInt && !device->device->isDepth24Stencil8PixelFormatSupported())
		{
			return support;
		}

		const bool depth	  = is_depth_format(format);
		const bool compressed = is_compressed_format(format);
		const bool integer	  = is_integer_format(format);

		support.sampled				   = true;
		support.copySrc				   = true;
		support.copyDst				   = true;
		support.linearFiltering		   = !depth && !integer;
		support.storage				   = !depth && !compressed;
		support.colorAttachment		   = !depth && !compressed;
		support.blendable			   = is_blendable_format(format);
		support.depthStencilAttachment = depth;

		return support;
	}

}

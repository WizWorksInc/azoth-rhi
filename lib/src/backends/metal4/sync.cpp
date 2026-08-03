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
	TimelineHandle CreateTimeline(void * impl, const TimelineDesc & desc, Error * error) noexcept
	{
		if (!RefuseUnexportable(desc.exportableHandleTypes,
				ExternalHandleType::eMtlSharedEvent,
				"Metal exports a timeline only through MTLSharedEventHandle, and this asked for another handle type",
				error))
		{
			return TimelineHandle{};
		}

		AZO_RHI_PROFILE_ZONE("rhi.metal4.createTimeline");

		auto * device = static_cast<Metal4Device *>(impl);

		MTL::SharedEvent * raw = device->device->newSharedEvent();
		if (raw == nullptr)
		{
			return FailValue<TimelineHandle>(error, ErrorCode::eNativeApiError, "Metal shared event creation failed");
		}

		NS::SharedPtr<MTL::SharedEvent> event = NS::TransferPtr(raw);
		event->setSignaledValue(desc.initialValue);

		const TimelineHandle handle = device->timelines.Store(Metal4Timeline{ .event = std::move(event), .exportableHandleTypes = desc.exportableHandleTypes });
		if (!handle.IsValid())
		{
			return FailValue<TimelineHandle>(error, ErrorCode::eOutOfHostMemory, "Metal timeline tracking failed");
		}

		return ReturnValue(handle, error);
	}

	[[nodiscard]] MTL::Buffer * ResolveBuffer(Metal4Device * device, BufferHandle handle) noexcept
	{
		const auto * tracked = device->buffers.Resolve(handle, kHandleAlreadyChecked);
		return tracked != nullptr ? tracked->buffer.get() : nullptr;
	}

	[[nodiscard]] MTL::Texture * ResolveTexture(Metal4Device * device, TextureHandle handle) noexcept
	{
		const auto * tracked = device->textures.Resolve(handle, kHandleAlreadyChecked);
		return tracked != nullptr ? tracked->texture.get() : nullptr;
	}

	[[nodiscard]] Format ResolveTextureFormat(Metal4Device * device, TextureHandle handle) noexcept
	{
		auto * const tracked = device->textures.Resolve(handle, kHandleAlreadyChecked);
		return tracked != nullptr ? tracked->format : Format::eRGBA8UNorm;
	}

	BinarySemaphoreHandle CreateBinarySemaphore(void * impl, const BinarySemaphoreDesc & desc, Error * error) noexcept
	{
		if (!RefuseUnexportable(desc.exportableHandleTypes,
				ExternalHandleType::eMtlSharedEvent,
				"Metal exports a binary semaphore only through MTLSharedEventHandle, and this asked for another handle type",
				error))
		{
			return BinarySemaphoreHandle{};
		}

		AZO_RHI_PROFILE_ZONE("rhi.metal4.createBinarySemaphore");

		auto * device		   = static_cast<Metal4Device *>(impl);
		MTL::SharedEvent * raw = device->device->newSharedEvent();
		if (raw == nullptr)
		{
			return FailValue<BinarySemaphoreHandle>(error, ErrorCode::eNativeApiError, "Metal shared event creation failed");
		}
		NS::SharedPtr<MTL::SharedEvent> event = NS::TransferPtr(raw);
		event->setSignaledValue(0);

		const BinarySemaphoreHandle handle = device->binarySemaphores.Store(Metal4BinarySemaphore{
			.event				   = std::move(event),
			.value				   = 0,
			.exportableHandleTypes = desc.exportableHandleTypes,
		});
		if (!handle.IsValid())
		{
			return FailValue<BinarySemaphoreHandle>(error, ErrorCode::eOutOfHostMemory, "Metal binary semaphore tracking failed");
		}

		return ReturnValue(handle, error);
	}

} // namespace azo::rhi::metal4

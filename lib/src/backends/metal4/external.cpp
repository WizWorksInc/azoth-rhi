// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#include "azoth/rhi/core/external.hpp"

#include "azoth/rhi/backend/blocks/device.hpp"
#include "azoth/rhi/backend/dispatch.hpp"
#include "azoth/rhi/backend/support/resource_record.hpp"
#include "azoth/rhi/commands/sync.hpp"
#include "azoth/rhi/core/flags.hpp"
#include "azoth/rhi/core/profiling.hpp"
#include "azoth/rhi/core/resource_handles.hpp"
#include "azoth/rhi/core/result.hpp"
#include "azoth/rhi/resources/resources.hpp"

#include "backends/metal4/internal.hpp"
#include "backends/metal_common/conversions.hpp"

#include <Foundation/NSObject.hpp>
#include <Foundation/NSSharedPtr.hpp>
#include <Metal/MTLEvent.hpp>
#include <Metal/MTLTexture.hpp>

#include <utility>

namespace azo::rhi::metal4
{
	namespace
	{
		constexpr const char * kUndeclared = "export of a handle type this object was not created exportable to";
		constexpr const char * kNoBuffers  = "Metal has no shared buffer, so nothing here can be exported or imported as one";
		constexpr const char * kNoHeaps	   = "Metal has no shared heap, so nothing here can be exported or imported as one";

		[[nodiscard]] bool return_object(NS::Object * object, const ExternalHandleType type, ExternalHandle * out, Error * error) noexcept
		{
			if (object == nullptr)
			{
				return fail(error, ErrorCode::eNativeApiError, "Metal produced no handle for an object created shared");
			}

			*out = ExternalHandle{ .type = type, .handle = object };
			return succeed(error);
		}

		[[nodiscard]] bool check_export(
			const Flags<ExternalHandleType> declared,
			const ExternalHandleType wanted,
			const ExternalHandleType only,
			Error * error
		) noexcept
		{
			if (wanted != only)
			{
				return fail(error, ErrorCode::eUnsupportedFeature, "Metal names this kind of object under one handle type only, and it is not that one");
			}

			return declared.contains(wanted) ? true : fail(error, ErrorCode::eInvalidArgument, kUndeclared);
		}
	}

	bool metal4_export_buffer(
		[[maybe_unused]] void * impl,
		[[maybe_unused]] const BufferHandle buffer,
		[[maybe_unused]] const ExternalHandleType type,
		ExternalHandle * out,
		Error * error
	) noexcept
	{
		if (out != nullptr)
		{
			*out = {};
		}

		return fail(error, ErrorCode::eUnsupportedFeature, kNoBuffers);
	}

	bool metal4_export_heap(
		[[maybe_unused]] void * impl,
		[[maybe_unused]] const HeapHandle heap,
		[[maybe_unused]] const ExternalHandleType type,
		ExternalHandle * out,
		Error * error
	) noexcept
	{
		if (out != nullptr)
		{
			*out = {};
		}

		return fail(error, ErrorCode::eUnsupportedFeature, kNoHeaps);
	}

	bool metal4_export_texture(void * impl, const TextureHandle texture, const ExternalHandleType type, ExternalHandle * out, Error * error) noexcept
	{
		if (out == nullptr)
		{
			return fail(error, ErrorCode::eInvalidArgument, "external export needs somewhere to write the handle");
		}

		*out		  = {};
		auto * device = static_cast<Metal4Device *>(impl);

		const Metal4TextureSlot * slot = device->textures.resolve(texture, kHandleAlreadyChecked);
		if (slot == nullptr)
		{
			return fail(error, ErrorCode::eInvalidHandle, "export of an invalid texture handle");
		}

		if (!slot->shared)
		{
			return fail(error, ErrorCode::eInvalidArgument, kUndeclared);
		}

		if (type != ExternalHandleType::eMtlSharedTexture)
		{
			return fail(error, ErrorCode::eUnsupportedFeature, "Metal names a texture under MTLSharedTextureHandle and nothing else");
		}

		return return_object(slot->texture->newSharedTextureHandle(), type, out, error);
	}

	bool metal4_export_timeline(void * impl, const TimelineHandle timeline, const ExternalHandleType type, ExternalHandle * out, Error * error) noexcept
	{
		if (out == nullptr)
		{
			return fail(error, ErrorCode::eInvalidArgument, "external export needs somewhere to write the handle");
		}

		*out		  = {};
		auto * device = static_cast<Metal4Device *>(impl);

		const Metal4Timeline * slot = device->timelines.resolve(timeline, kHandleAlreadyChecked);
		if (slot == nullptr)
		{
			return fail(error, ErrorCode::eInvalidHandle, "export of an invalid timeline handle");
		}

		if (!check_export(slot->exportableHandleTypes, type, ExternalHandleType::eMtlSharedEvent, error))
		{
			return false;
		}

		return return_object(slot->event->newSharedEventHandle(), type, out, error);
	}

	bool metal4_export_binary_semaphore(
		void * impl,
		const BinarySemaphoreHandle semaphore,
		const ExternalHandleType type,
		ExternalHandle * out,
		Error * error
	) noexcept
	{
		if (out == nullptr)
		{
			return fail(error, ErrorCode::eInvalidArgument, "external export needs somewhere to write the handle");
		}

		*out		  = {};
		auto * device = static_cast<Metal4Device *>(impl);

		const Metal4BinarySemaphore * slot = device->binarySemaphores.resolve(semaphore, kHandleAlreadyChecked);
		if (slot == nullptr)
		{
			return fail(error, ErrorCode::eInvalidHandle, "export of an invalid binary semaphore handle");
		}

		if (!check_export(slot->exportableHandleTypes, type, ExternalHandleType::eMtlSharedEvent, error))
		{
			return false;
		}

		return return_object(slot->event->newSharedEventHandle(), type, out, error);
	}

	BufferHandle metal4_import_buffer([[maybe_unused]] void * impl, [[maybe_unused]] const ExternalBufferImportDesc & desc, Error * error) noexcept
	{
		return fail_value<BufferHandle>(error, ErrorCode::eUnsupportedFeature, kNoBuffers);
	}

	HeapHandle metal4_import_heap([[maybe_unused]] void * impl, [[maybe_unused]] const ExternalHeapImportDesc & desc, Error * error) noexcept
	{
		return fail_value<HeapHandle>(error, ErrorCode::eUnsupportedFeature, kNoHeaps);
	}

	TextureHandle metal4_import_texture(void * impl, const ExternalTextureImportDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal4.importTexture");
		auto * device = static_cast<Metal4Device *>(impl);

		if (desc.handle.type != ExternalHandleType::eMtlSharedTexture)
		{
			return fail_value<TextureHandle>(error, ErrorCode::eUnsupportedFeature, "Metal opens a texture from an MTLSharedTextureHandle and nothing else");
		}

		if (desc.handle.handle == nullptr)
		{
			return fail_value<TextureHandle>(error, ErrorCode::eInvalidArgument, "import of a handle carrying no Metal object");
		}

		if (build_texture_descriptor(desc.desc, error).get() == nullptr)
		{
			return TextureHandle{};
		}

		MTL::Texture * raw = device->device->newSharedTexture(static_cast<const MTL::SharedTextureHandle *>(desc.handle.handle));
		if (raw == nullptr)
		{
			return fail_value<TextureHandle>(
				error,
				ErrorCode::eNativeApiError,
				"the handle names no texture this device can open, which is what a handle from another device reports"
			);
		}

		set_metal_label(raw, desc.desc.debugName);
		NS::SharedPtr<MTL::Texture> texture = NS::TransferPtr(raw);

		const TextureHandle handle = device->textures.store(
			Metal4TextureSlot{
				.texture	   = std::move(texture),
				.format		   = desc.desc.format,
				.usage		   = desc.desc.usage,
				.mutableFormat = desc.desc.allowFormatViews,
				.shared		   = false,
				.desc		   = detail::recorded(desc.desc),
			}
		);
		if (!handle.is_valid())
		{
			return fail_value<TextureHandle>(error, ErrorCode::eOutOfHostMemory, "Metal imported texture handle tracking failed");
		}

		return return_value(handle, error);
	}

	TimelineHandle metal4_import_timeline(void * impl, const ExternalTimelineImportDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal4.importTimeline");
		auto * device = static_cast<Metal4Device *>(impl);

		if (desc.handle.type != ExternalHandleType::eMtlSharedEvent)
		{
			return fail_value<TimelineHandle>(error, ErrorCode::eUnsupportedFeature, "Metal opens a timeline from an MTLSharedEventHandle and nothing else");
		}

		if (desc.handle.handle == nullptr)
		{
			return fail_value<TimelineHandle>(error, ErrorCode::eInvalidArgument, "import of a handle carrying no Metal object");
		}

		MTL::SharedEvent * raw = device->device->newSharedEvent(static_cast<const MTL::SharedEventHandle *>(desc.handle.handle));
		if (raw == nullptr)
		{
			return fail_value<TimelineHandle>(
				error,
				ErrorCode::eNativeApiError,
				"the handle names no event this device can open, which is what a handle from another device reports"
			);
		}

		const TimelineHandle handle = device->timelines.store(Metal4Timeline{ .event = NS::TransferPtr(raw) });
		if (!handle.is_valid())
		{
			return fail_value<TimelineHandle>(error, ErrorCode::eOutOfHostMemory, "Metal imported timeline handle tracking failed");
		}

		return return_value(handle, error);
	}

	BinarySemaphoreHandle metal4_import_binary_semaphore(void * impl, const ExternalBinarySemaphoreImportDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal4.importBinarySemaphore");
		auto * device = static_cast<Metal4Device *>(impl);

		if (desc.handle.type != ExternalHandleType::eMtlSharedEvent)
		{
			return fail_value<BinarySemaphoreHandle>(
				error,
				ErrorCode::eUnsupportedFeature,
				"Metal opens a binary semaphore from an MTLSharedEventHandle and nothing else"
			);
		}

		if (desc.handle.handle == nullptr)
		{
			return fail_value<BinarySemaphoreHandle>(error, ErrorCode::eInvalidArgument, "import of a handle carrying no Metal object");
		}

		MTL::SharedEvent * raw = device->device->newSharedEvent(static_cast<const MTL::SharedEventHandle *>(desc.handle.handle));
		if (raw == nullptr)
		{
			return fail_value<BinarySemaphoreHandle>(
				error,
				ErrorCode::eNativeApiError,
				"the handle names no event this device can open, which is what a handle from another device reports"
			);
		}

		const BinarySemaphoreHandle handle = device->binarySemaphores.store(Metal4BinarySemaphore{ .event = NS::TransferPtr(raw) });
		if (!handle.is_valid())
		{
			return fail_value<BinarySemaphoreHandle>(error, ErrorCode::eOutOfHostMemory, "Metal imported binary semaphore handle tracking failed");
		}

		return return_value(handle, error);
	}

	bool metal4_close_exported_handle([[maybe_unused]] void * impl, const ExternalHandle & handle, Error * error) noexcept
	{
		switch (handle.type)
		{
		case ExternalHandleType::eMtlSharedEvent:
		case ExternalHandleType::eMtlSharedTexture:
			if (handle.handle != nullptr)
			{
				static_cast<NS::Object *>(handle.handle)->release();
			}

			return succeed(error);

		case ExternalHandleType::eOpaqueFd:
		case ExternalHandleType::eOpaqueWin32:
		case ExternalHandleType::eOpaqueWin32Kmt:
		case ExternalHandleType::eD3D12Resource:
		case ExternalHandleType::eD3D12Heap:
		case ExternalHandleType::eD3D12Fence:
		case ExternalHandleType::eDmaBuf:		  break;
		}

		return fail(error, ErrorCode::eInvalidArgument, "this backend does not produce handles of that type, so it has nothing to release");
	}

	const ExternalSharingApi & external_sharing_block() noexcept
	{
		static const ExternalSharingApi block{
			.exportBuffer		   = &metal4_export_buffer,
			.exportHeap			   = &metal4_export_heap,
			.exportTexture		   = &metal4_export_texture,
			.exportTimeline		   = &metal4_export_timeline,
			.exportBinarySemaphore = &metal4_export_binary_semaphore,
			.importBuffer		   = &metal4_import_buffer,
			.importHeap			   = &metal4_import_heap,
			.importTexture		   = &metal4_import_texture,
			.importTimeline		   = &metal4_import_timeline,
			.importBinarySemaphore = &metal4_import_binary_semaphore,
			.closeExportedHandle   = &metal4_close_exported_handle,
		};

		return block;
	}

}

// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#include "backends/metal/internal.hpp"

namespace azo::rhi::metal
{
	namespace
	{
		constexpr const char * kUndeclared = "export of a handle type this object was not created exportable to";
		constexpr const char * kNoBuffers  = "Metal has no shared buffer, so nothing here can be exported or imported as one";
		constexpr const char * kNoHeaps	   = "Metal has no shared heap, so nothing here can be exported or imported as one";

		[[nodiscard]] bool ReturnObject(NS::Object * object, const ExternalHandleType type, ExternalHandle * out, Error * error) noexcept
		{
			if (object == nullptr)
			{
				return Fail(error, ErrorCode::eNativeApiError, "Metal produced no handle for an object created shared");
			}

			*out = ExternalHandle{ .type = type, .handle = object };
			return Succeed(error);
		}

		[[nodiscard]] bool CheckExport(
			const Flags<ExternalHandleType> declared, const ExternalHandleType wanted, const ExternalHandleType only, Error * error) noexcept
		{
			if (wanted != only)
			{
				return Fail(error, ErrorCode::eUnsupportedFeature, "Metal names this kind of object under one handle type only, and it is not that one");
			}

			return declared.Contains(wanted) ? true : Fail(error, ErrorCode::eInvalidArgument, kUndeclared);
		}
	}

	bool MetalExportBuffer([[maybe_unused]] void * impl, [[maybe_unused]] const BufferHandle buffer, [[maybe_unused]] const ExternalHandleType type,
		ExternalHandle * out, Error * error) noexcept
	{
		if (out != nullptr)
		{
			*out = {};
		}

		return Fail(error, ErrorCode::eUnsupportedFeature, kNoBuffers);
	}

	bool MetalExportHeap([[maybe_unused]] void * impl, [[maybe_unused]] const HeapHandle heap, [[maybe_unused]] const ExternalHandleType type,
		ExternalHandle * out, Error * error) noexcept
	{
		if (out != nullptr)
		{
			*out = {};
		}

		return Fail(error, ErrorCode::eUnsupportedFeature, kNoHeaps);
	}

	bool MetalExportTexture(void * impl, const TextureHandle texture, const ExternalHandleType type, ExternalHandle * out, Error * error) noexcept
	{
		if (out == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidArgument, "external export needs somewhere to write the handle");
		}

		*out		  = {};
		auto * device = static_cast<MetalDevice *>(impl);

		const MetalTextureSlot * slot = device->textures.Resolve(texture, kHandleAlreadyChecked);
		if (slot == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "export of an invalid texture handle");
		}

		if (!slot->shared)
		{
			return Fail(error, ErrorCode::eInvalidArgument, kUndeclared);
		}

		if (type != ExternalHandleType::eMtlSharedTexture)
		{
			return Fail(error, ErrorCode::eUnsupportedFeature, "Metal names a texture under MTLSharedTextureHandle and nothing else");
		}

		return ReturnObject(slot->texture->newSharedTextureHandle(), type, out, error);
	}

	bool MetalExportTimeline(void * impl, const TimelineHandle timeline, const ExternalHandleType type, ExternalHandle * out, Error * error) noexcept
	{
		if (out == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidArgument, "external export needs somewhere to write the handle");
		}

		*out		  = {};
		auto * device = static_cast<MetalDevice *>(impl);

		const MetalTimeline * slot = device->timelines.Resolve(timeline, kHandleAlreadyChecked);
		if (slot == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "export of an invalid timeline handle");
		}

		if (!CheckExport(slot->exportableHandleTypes, type, ExternalHandleType::eMtlSharedEvent, error))
		{
			return false;
		}

		return ReturnObject(slot->event->newSharedEventHandle(), type, out, error);
	}

	bool MetalExportBinarySemaphore(
		void * impl, const BinarySemaphoreHandle semaphore, const ExternalHandleType type, ExternalHandle * out, Error * error) noexcept
	{
		if (out == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidArgument, "external export needs somewhere to write the handle");
		}

		*out		  = {};
		auto * device = static_cast<MetalDevice *>(impl);

		const MetalBinarySemaphore * slot = device->binarySemaphores.Resolve(semaphore, kHandleAlreadyChecked);
		if (slot == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "export of an invalid binary semaphore handle");
		}

		if (!CheckExport(slot->exportableHandleTypes, type, ExternalHandleType::eMtlSharedEvent, error))
		{
			return false;
		}

		return ReturnObject(slot->event->newSharedEventHandle(), type, out, error);
	}

	BufferHandle MetalImportBuffer([[maybe_unused]] void * impl, [[maybe_unused]] const ExternalBufferImportDesc & desc, Error * error) noexcept
	{
		return FailValue<BufferHandle>(error, ErrorCode::eUnsupportedFeature, kNoBuffers);
	}

	HeapHandle MetalImportHeap([[maybe_unused]] void * impl, [[maybe_unused]] const ExternalHeapImportDesc & desc, Error * error) noexcept
	{
		return FailValue<HeapHandle>(error, ErrorCode::eUnsupportedFeature, kNoHeaps);
	}

	TextureHandle MetalImportTexture(void * impl, const ExternalTextureImportDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal.importTexture");
		auto * device = static_cast<MetalDevice *>(impl);

		if (desc.handle.type != ExternalHandleType::eMtlSharedTexture)
		{
			return FailValue<TextureHandle>(error, ErrorCode::eUnsupportedFeature, "Metal opens a texture from an MTLSharedTextureHandle and nothing else");
		}

		if (desc.handle.handle == nullptr)
		{
			return FailValue<TextureHandle>(error, ErrorCode::eInvalidArgument, "import of a handle carrying no Metal object");
		}

		if (BuildTextureDescriptor(desc.desc, error).get() == nullptr)
		{
			return TextureHandle{};
		}

		MTL::Texture * raw = device->device->newSharedTexture(static_cast<const MTL::SharedTextureHandle *>(desc.handle.handle));
		if (raw == nullptr)
		{
			return FailValue<TextureHandle>(
				error, ErrorCode::eNativeApiError, "the handle names no texture this device can open, which is what a handle from another device reports");
		}

		SetMetalLabel(raw, desc.desc.debugName);
		NS::SharedPtr<MTL::Texture> texture = NS::TransferPtr(raw);

		const TextureHandle handle = device->textures.Store(MetalTextureSlot{ .texture = std::move(texture),
			.format																	   = desc.desc.format,
			.usage																	   = desc.desc.usage,
			.mutableFormat															   = desc.desc.allowFormatViews,
			.shared																	   = false,
			.desc																	   = detail::Recorded(desc.desc) });
		if (!handle.IsValid())
		{
			return FailValue<TextureHandle>(error, ErrorCode::eOutOfHostMemory, "Metal imported texture handle tracking failed");
		}

		return ReturnValue(handle, error);
	}

	TimelineHandle MetalImportTimeline(void * impl, const ExternalTimelineImportDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal.importTimeline");
		auto * device = static_cast<MetalDevice *>(impl);

		if (desc.handle.type != ExternalHandleType::eMtlSharedEvent)
		{
			return FailValue<TimelineHandle>(error, ErrorCode::eUnsupportedFeature, "Metal opens a timeline from an MTLSharedEventHandle and nothing else");
		}

		if (desc.handle.handle == nullptr)
		{
			return FailValue<TimelineHandle>(error, ErrorCode::eInvalidArgument, "import of a handle carrying no Metal object");
		}

		MTL::SharedEvent * raw = device->device->newSharedEvent(static_cast<const MTL::SharedEventHandle *>(desc.handle.handle));
		if (raw == nullptr)
		{
			return FailValue<TimelineHandle>(
				error, ErrorCode::eNativeApiError, "the handle names no event this device can open, which is what a handle from another device reports");
		}

		const TimelineHandle handle = device->timelines.Store(MetalTimeline{ .event = NS::TransferPtr(raw) });
		if (!handle.IsValid())
		{
			return FailValue<TimelineHandle>(error, ErrorCode::eOutOfHostMemory, "Metal imported timeline handle tracking failed");
		}

		return ReturnValue(handle, error);
	}

	BinarySemaphoreHandle MetalImportBinarySemaphore(void * impl, const ExternalBinarySemaphoreImportDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal.importBinarySemaphore");
		auto * device = static_cast<MetalDevice *>(impl);

		if (desc.handle.type != ExternalHandleType::eMtlSharedEvent)
		{
			return FailValue<BinarySemaphoreHandle>(
				error, ErrorCode::eUnsupportedFeature, "Metal opens a binary semaphore from an MTLSharedEventHandle and nothing else");
		}

		if (desc.handle.handle == nullptr)
		{
			return FailValue<BinarySemaphoreHandle>(error, ErrorCode::eInvalidArgument, "import of a handle carrying no Metal object");
		}

		MTL::SharedEvent * raw = device->device->newSharedEvent(static_cast<const MTL::SharedEventHandle *>(desc.handle.handle));
		if (raw == nullptr)
		{
			return FailValue<BinarySemaphoreHandle>(
				error, ErrorCode::eNativeApiError, "the handle names no event this device can open, which is what a handle from another device reports");
		}

		const BinarySemaphoreHandle handle = device->binarySemaphores.Store(MetalBinarySemaphore{ .event = NS::TransferPtr(raw) });
		if (!handle.IsValid())
		{
			return FailValue<BinarySemaphoreHandle>(error, ErrorCode::eOutOfHostMemory, "Metal imported binary semaphore handle tracking failed");
		}

		return ReturnValue(handle, error);
	}

	bool MetalCloseExportedHandle([[maybe_unused]] void * impl, const ExternalHandle & handle, Error * error) noexcept
	{
		switch (handle.type)
		{
		case ExternalHandleType::eMtlSharedEvent:
		case ExternalHandleType::eMtlSharedTexture:
			if (handle.handle != nullptr)
			{
				static_cast<NS::Object *>(handle.handle)->release();
			}

			return Succeed(error);

		case ExternalHandleType::eOpaqueFd:
		case ExternalHandleType::eOpaqueWin32:
		case ExternalHandleType::eOpaqueWin32Kmt:
		case ExternalHandleType::eD3D12Resource:
		case ExternalHandleType::eD3D12Heap:
		case ExternalHandleType::eD3D12Fence:
		case ExternalHandleType::eDmaBuf:		  break;
		}

		return Fail(error, ErrorCode::eInvalidArgument, "this backend does not produce handles of that type, so it has nothing to release");
	}

	const ExternalSharingApi & ExternalSharingBlock() noexcept
	{
		static const ExternalSharingApi block{
			.exportBuffer		   = &MetalExportBuffer,
			.exportHeap			   = &MetalExportHeap,
			.exportTexture		   = &MetalExportTexture,
			.exportTimeline		   = &MetalExportTimeline,
			.exportBinarySemaphore = &MetalExportBinarySemaphore,
			.importBuffer		   = &MetalImportBuffer,
			.importHeap			   = &MetalImportHeap,
			.importTexture		   = &MetalImportTexture,
			.importTimeline		   = &MetalImportTimeline,
			.importBinarySemaphore = &MetalImportBinarySemaphore,
			.closeExportedHandle   = &MetalCloseExportedHandle,
		};

		return block;
	}

}

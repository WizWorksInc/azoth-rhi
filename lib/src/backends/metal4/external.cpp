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

/*
 * Metal's half of external sharing, which is textures and events and nothing else. Neither handle here is a file descriptor or an NT handle.
 * MTLSharedTextureHandle and MTLSharedEventHandle are NSSecureCoding objects moved between processes over XPC and bridge to neither of the other two APIs.
 *
 * MTLBuffer and MTLHeap have no shared form so those four entries refuse by name, which is what the per adapter query already reports for those kinds.
 */

#include "backends/metal4/internal.hpp"

namespace azo::rhi::metal4
{
	namespace
	{
		constexpr const char * kUndeclared = "export of a handle type this object was not created exportable to";
		constexpr const char * kNoBuffers  = "Metal has no shared buffer, so nothing here can be exported or imported as one";
		constexpr const char * kNoHeaps	   = "Metal has no shared heap, so nothing here can be exported or imported as one";

		/*
		 * A Metal handle travels as the object itself and not as an operating system handle, which nothing else in this enum does. The newSharedTextureHandle and
		 * newSharedEventHandle calls return a reference the caller owns and nothing is retained on top of it.
		 *
		 * An import reads the handle without consuming it. The reference outlives any number of imports and one release rule covers this alongside the descriptor and
		 * NT handle types.
		 */
		[[nodiscard]] bool ReturnObject(NS::Object * object, const ExternalHandleType type, ExternalHandle * out, Error * error) noexcept
		{
			if (object == nullptr)
			{
				return Fail(error, ErrorCode::eNativeApiError, "Metal produced no handle for an object created shared");
			}

			*out = ExternalHandle{ .type = type, .handle = object };
			return Succeed(error);
		}

		// The type has to be the one Metal names this kind of object under, and it has to be one the object was created exportable to. Both, since a declaration
		// naming the right type on an object created without it is still a refusal.
		[[nodiscard]] bool CheckExport(
			const Flags<ExternalHandleType> declared, const ExternalHandleType wanted, const ExternalHandleType only, Error * error) noexcept
		{
			if (wanted != only)
			{
				return Fail(error, ErrorCode::eUnsupportedFeature, "Metal names this kind of object under one handle type only, and it is not that one");
			}

			return declared.Contains(wanted) ? true : Fail(error, ErrorCode::eInvalidArgument, kUndeclared);
		}
	} // namespace

	bool ExportBuffer([[maybe_unused]] void * impl, [[maybe_unused]] const BufferHandle buffer, [[maybe_unused]] const ExternalHandleType type,
		ExternalHandle * out, Error * error) noexcept
	{
		if (out != nullptr)
		{
			*out = {};
		}

		return Fail(error, ErrorCode::eUnsupportedFeature, kNoBuffers);
	}

	bool ExportHeap([[maybe_unused]] void * impl, [[maybe_unused]] const HeapHandle heap, [[maybe_unused]] const ExternalHandleType type, ExternalHandle * out,
		Error * error) noexcept
	{
		if (out != nullptr)
		{
			*out = {};
		}

		return Fail(error, ErrorCode::eUnsupportedFeature, kNoHeaps);
	}

	bool ExportTexture(void * impl, const TextureHandle texture, const ExternalHandleType type, ExternalHandle * out, Error * error) noexcept
	{
		if (out == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidArgument, "external export needs somewhere to write the handle");
		}

		*out		  = {};
		auto * device = static_cast<Metal4Device *>(impl);

		const Metal4TextureSlot * slot = device->textures.Resolve(texture, kHandleAlreadyChecked);
		if (slot == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "export of an invalid texture handle");
		}

		// The texture records whether newSharedTexture made it and not a declared set, an ordinary MTLTexture having no handle to give whatever the description asked
		// for. So the check is the same one in effect and reads off what was actually built.
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

	bool ExportTimeline(void * impl, const TimelineHandle timeline, const ExternalHandleType type, ExternalHandle * out, Error * error) noexcept
	{
		if (out == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidArgument, "external export needs somewhere to write the handle");
		}

		*out		  = {};
		auto * device = static_cast<Metal4Device *>(impl);

		const Metal4Timeline * slot = device->timelines.Resolve(timeline, kHandleAlreadyChecked);
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

	bool ExportBinarySemaphore(void * impl, const BinarySemaphoreHandle semaphore, const ExternalHandleType type, ExternalHandle * out, Error * error) noexcept
	{
		if (out == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidArgument, "external export needs somewhere to write the handle");
		}

		*out		  = {};
		auto * device = static_cast<Metal4Device *>(impl);

		const Metal4BinarySemaphore * slot = device->binarySemaphores.Resolve(semaphore, kHandleAlreadyChecked);
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

	BufferHandle ImportBuffer([[maybe_unused]] void * impl, [[maybe_unused]] const ExternalBufferImportDesc & desc, Error * error) noexcept
	{
		return FailValue<BufferHandle>(error, ErrorCode::eUnsupportedFeature, kNoBuffers);
	}

	HeapHandle ImportHeap([[maybe_unused]] void * impl, [[maybe_unused]] const ExternalHeapImportDesc & desc, Error * error) noexcept
	{
		return FailValue<HeapHandle>(error, ErrorCode::eUnsupportedFeature, kNoHeaps);
	}

	/*
	 * A texture over a handle another process produced.
	 *
	 * newSharedTexture hands back the texture the exporter made without building one over its memory, so desc is read for the format and usage the slot records
	 * and for the debug name. The descriptor is still built from it, so a description that does not name a representable format is refused before the handle is
	 * touched.
	 */
	TextureHandle ImportTexture(void * impl, const ExternalTextureImportDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal4.importTexture");
		auto * device = static_cast<Metal4Device *>(impl);

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

		// Not shared onward, whatever the description said: this texture was created elsewhere, so handing out a handle to it would share a payload this device does
		// not own.
		const TextureHandle handle = device->textures.Store(Metal4TextureSlot{ .texture = std::move(texture),
			.format																		= desc.desc.format,
			.usage																		= desc.desc.usage,
			.mutableFormat																= desc.desc.allowFormatViews,
			.shared																		= false,
			.desc																		= detail::Recorded(desc.desc) });
		if (!handle.IsValid())
		{
			return FailValue<TextureHandle>(error, ErrorCode::eOutOfHostMemory, "Metal imported texture handle tracking failed");
		}

		return ReturnValue(handle, error);
	}

	TimelineHandle ImportTimeline(void * impl, const ExternalTimelineImportDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal4.importTimeline");
		auto * device = static_cast<Metal4Device *>(impl);

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

		// The initial value is ignored, as it is on the other two backends: the payload arrives at whatever the exporter left it at, and setting it here would rewind
		// a counter the other side is still advancing.
		const TimelineHandle handle = device->timelines.Store(Metal4Timeline{ .event = NS::TransferPtr(raw) });
		if (!handle.IsValid())
		{
			return FailValue<TimelineHandle>(error, ErrorCode::eOutOfHostMemory, "Metal imported timeline handle tracking failed");
		}

		return ReturnValue(handle, error);
	}

	BinarySemaphoreHandle ImportBinarySemaphore(void * impl, const ExternalBinarySemaphoreImportDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal4.importBinarySemaphore");
		auto * device = static_cast<Metal4Device *>(impl);

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

		// The counter starts at zero here as it does on the exporting side, which is what keeps an exporter that only signals and an importer that only waits in
		// step.
		const BinarySemaphoreHandle handle = device->binarySemaphores.Store(Metal4BinarySemaphore{ .event = NS::TransferPtr(raw) });
		if (!handle.IsValid())
		{
			return FailValue<BinarySemaphoreHandle>(error, ErrorCode::eOutOfHostMemory, "Metal imported binary semaphore handle tracking failed");
		}

		return ReturnValue(handle, error);
	}

	bool CloseExportedHandle([[maybe_unused]] void * impl, const ExternalHandle & handle, Error * error) noexcept
	{
		switch (handle.type)
		{
		/*
		 * A reference release and not a handle close, which is the whole of what owning a Metal handle means.
		 *
		 * Balances the one reference export returned. An import reads the handle without consuming it, so this is correct before or after one, the same way the other
		 * two backends' entries are.
		 */
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
			.exportBuffer		   = &ExportBuffer,
			.exportHeap			   = &ExportHeap,
			.exportTexture		   = &ExportTexture,
			.exportTimeline		   = &ExportTimeline,
			.exportBinarySemaphore = &ExportBinarySemaphore,
			.importBuffer		   = &ImportBuffer,
			.importHeap			   = &ImportHeap,
			.importTexture		   = &ImportTexture,
			.importTimeline		   = &ImportTimeline,
			.importBinarySemaphore = &ImportBinarySemaphore,
			.closeExportedHandle   = &CloseExportedHandle,
		};

		return block;
	}

} // namespace azo::rhi::metal4

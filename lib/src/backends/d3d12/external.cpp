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

#include "backends/d3d12/internal.hpp"

namespace azo::rhi::d3d12
{
	namespace
	{
		/*
		 * Which handle types name the NT handle CreateSharedHandle produces for each kind of object.
		 *
		 * One call produces one handle whatever it was given, so what varies is the name it travels under: the plain Win32 one, and the producer-tagged one Vulkan
		 * and CUDA use when importing that same handle. The tagged names are not interchangeable between kinds, a resource handle and a heap handle being different
		 * objects, not two spellings of one, which is the distinction both consumers draw.
		 */
		constexpr Flags<ExternalHandleType> kResourceTypes = Flags<ExternalHandleType>(ExternalHandleType::eOpaqueWin32) | ExternalHandleType::eD3D12Resource;
		constexpr Flags<ExternalHandleType> kHeapTypes	   = Flags<ExternalHandleType>(ExternalHandleType::eOpaqueWin32) | ExternalHandleType::eD3D12Heap;
		constexpr Flags<ExternalHandleType> kFenceTypes	   = Flags<ExternalHandleType>(ExternalHandleType::eOpaqueWin32) | ExternalHandleType::eD3D12Fence;

		constexpr const char * kUndeclared = "export of a handle type this object was not created exportable to";

		/*
		 * The NT handle for an object created with the shared flag.
		 *
		 * Security attributes and a name are both left absent, which Direct3D 12 reads as a default descriptor and a non-inheritable unnamed handle. A named handle
		 * would share the kernel object namespace and collide, and no consumer here needs one.
		 */
		[[nodiscard]] bool ExportObject(D3D12Device * device, ID3D12DeviceChild * object, const Flags<ExternalHandleType> declared,
			const Flags<ExternalHandleType> accepted, const ExternalHandleType type, ExternalHandle * out, Error * error) noexcept
		{
			if (out == nullptr)
			{
				return Fail(error, ErrorCode::eInvalidArgument, "external export needs somewhere to write the handle");
			}

			*out = {};
			if (!declared.Contains(type))
			{
				return Fail(error, ErrorCode::eInvalidArgument, kUndeclared);
			}

			if (!accepted.Contains(type))
			{
				return Fail(error, ErrorCode::eUnsupportedFeature, "Direct3D 12 does not name this kind of object under that handle type");
			}

			HANDLE handle = nullptr;
			if (FAILED(device->device->CreateSharedHandle(object, nullptr, GENERIC_ALL, nullptr, &handle)))
			{
				return Fail(error, ErrorCode::eNativeApiError, "ID3D12Device::CreateSharedHandle failed");
			}

			*out = ExternalHandle{ .type = type, .handle = handle };
			return Succeed(error);
		}

		// The other half. A handle from another adapter, another API, or a corrupted one fails here without opening into a broken object, which is the validation the
		// external path has and adoption does not.
		template <class Object>
		[[nodiscard]] bool OpenShared(
			D3D12Device * device, const ExternalHandle & handle, const Flags<ExternalHandleType> accepted, ComPtr<Object> & out, Error * error) noexcept
		{
			if (!accepted.Contains(handle.type))
			{
				return Fail(error, ErrorCode::eUnsupportedFeature, "Direct3D 12 cannot open a handle of that type as this kind of object");
			}

			if (handle.handle == nullptr)
			{
				return Fail(error, ErrorCode::eInvalidArgument, "import of a handle carrying no Win32 handle");
			}

			if (FAILED(device->device->OpenSharedHandle(handle.handle, IID_PPV_ARGS(out.GetAddressOf()))))
			{
				return Fail(error,
					ErrorCode::eNativeApiError,
					"the handle names no payload this device can open, which is what a handle from another adapter or a corrupted one reports");
			}

			return Succeed(error);
		}
	} // namespace

	bool D3D12ExportBuffer(void * impl, const BufferHandle buffer, const ExternalHandleType type, ExternalHandle * out, Error * error) noexcept
	{
		auto * device			= static_cast<D3D12Device *>(impl);
		const BufferSlot * slot = ResolveBuffer(device, buffer);
		if (slot == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "export of an invalid buffer handle");
		}

		return ExportObject(device, slot->resource.Get(), slot->exportableHandleTypes, kResourceTypes, type, out, error);
	}

	bool D3D12ExportTexture(void * impl, const TextureHandle texture, const ExternalHandleType type, ExternalHandle * out, Error * error) noexcept
	{
		auto * device			 = static_cast<D3D12Device *>(impl);
		const TextureSlot * slot = ResolveTexture(device, texture);
		if (slot == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "export of an invalid texture handle");
		}

		return ExportObject(device, slot->resource.Get(), slot->exportableHandleTypes, kResourceTypes, type, out, error);
	}

	bool D3D12ExportHeap(void * impl, const HeapHandle heap, const ExternalHandleType type, ExternalHandle * out, Error * error) noexcept
	{
		auto * device		  = static_cast<D3D12Device *>(impl);
		const HeapSlot * slot = ResolveHeap(device, heap);
		if (slot == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "export of an invalid heap handle");
		}

		return ExportObject(device, slot->heap.Get(), slot->exportableHandleTypes, kHeapTypes, type, out, error);
	}

	bool D3D12ExportTimeline(void * impl, const TimelineHandle timeline, const ExternalHandleType type, ExternalHandle * out, Error * error) noexcept
	{
		auto * device			  = static_cast<D3D12Device *>(impl);
		const TimelineSlot * slot = ResolveTimeline(device, timeline);
		if (slot == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "export of an invalid timeline handle");
		}

		return ExportObject(device, slot->fence.Get(), slot->exportableHandleTypes, kFenceTypes, type, out, error);
	}

	bool D3D12ExportBinarySemaphore(
		void * impl, const BinarySemaphoreHandle semaphore, const ExternalHandleType type, ExternalHandle * out, Error * error) noexcept
	{
		auto * device					 = static_cast<D3D12Device *>(impl);
		const BinarySemaphoreSlot * slot = ResolveBinarySemaphore(device, semaphore);
		if (slot == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "export of an invalid binary semaphore handle");
		}

		return ExportObject(device, slot->fence.Get(), slot->exportableHandleTypes, kFenceTypes, type, out, error);
	}

	/*
	 * A buffer over memory another component produced.
	 *
	 * OpenSharedHandle hands back the resource the exporter created without building a new one over its memory, so desc is read for the size the slot records and
	 * for the debug name and nothing else. Direct3D 12 keeps the description with the resource, which is the one place this surface is easier than the Vulkan
	 * side, where every field has to be restated because none of it travels with the handle.
	 */
	BufferHandle D3D12ImportBuffer(void * impl, const ExternalBufferImportDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.d3d12.importBuffer");
		auto * device = static_cast<D3D12Device *>(impl);

		ComPtr<ID3D12Resource> resource;
		if (!OpenShared(device, desc.handle, kResourceTypes, resource, error))
		{
			return BufferHandle{};
		}

		NameD3D12Object(resource.Get(), desc.desc.debugName, device->debugNames);

		// No allocation and no exportable set: this device owns the reference the open returned and not the memory under it, and re-exporting a payload it did not
		// create would hand out a handle to memory that is not its to share.
		return ReturnValue(device->bufferSlots.Store(BufferSlot{
							   .resource = std::move(resource), .size = desc.desc.size, .hostVisible = false, .desc = detail::Recorded(desc.desc) }),
			error);
	}

	TextureHandle D3D12ImportTexture(void * impl, const ExternalTextureImportDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.d3d12.importTexture");
		auto * device			 = static_cast<D3D12Device *>(impl);
		const DXGI_FORMAT format = MapFormat(desc.desc.format);
		if (format == DXGI_FORMAT_UNKNOWN)
		{
			return FailValue<TextureHandle>(error, ErrorCode::eUnsupportedFormat, "imported texture format is undefined or unsupported");
		}

		ComPtr<ID3D12Resource> resource;
		if (!OpenShared(device, desc.handle, kResourceTypes, resource, error))
		{
			return TextureHandle{};
		}

		NameD3D12Object(resource.Get(), desc.desc.debugName, device->debugNames);

		return ReturnValue(device->textureSlots.Store(TextureSlot{ .resource = std::move(resource),
							   .format										 = format,
							   .rhiFormat									 = desc.desc.format,
							   .type										 = desc.desc.type,
							   .mipLevels									 = desc.desc.mipLevels,
							   .arrayLayers									 = desc.desc.arrayLayers,
							   .usage										 = desc.desc.usage,
							   .mutableFormat								 = desc.desc.allowFormatViews }),
			error);
	}

	HeapHandle D3D12ImportHeap(void * impl, const ExternalHeapImportDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.d3d12.importHeap");
		auto * device = static_cast<D3D12Device *>(impl);

		ComPtr<ID3D12Heap> heap;
		if (!OpenShared(device, desc.handle, kHeapTypes, heap, error))
		{
			return HeapHandle{};
		}

		// The heap's own description and not the caller's, since the exporter fixed both and the placement checks have to judge against what the memory actually is.
		const D3D12_HEAP_DESC opened = heap->GetDesc();
		return ReturnValue(device->heapSlots.Store(HeapSlot{
							   .heap = std::move(heap),
							   .type = opened.Properties.Type,
							   .size = opened.SizeInBytes,
						   }),
			error);
	}

	TimelineHandle D3D12ImportTimeline(void * impl, const ExternalTimelineImportDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.d3d12.importTimeline");
		auto * device = static_cast<D3D12Device *>(impl);

		ComPtr<ID3D12Fence> fence;
		if (!OpenShared(device, desc.handle, kFenceTypes, fence, error))
		{
			return TimelineHandle{};
		}

		// The initial value in desc is ignored on purpose: the payload arrives at whatever the exporter left it at, and setting it here would rewind a counter the
		// other side is still advancing.
		return ReturnValue(device->timelineSlots.Store(TimelineSlot{ .fence = std::move(fence) }), error);
	}

	BinarySemaphoreHandle D3D12ImportBinarySemaphore(void * impl, const ExternalBinarySemaphoreImportDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.d3d12.importBinarySemaphore");
		auto * device = static_cast<D3D12Device *>(impl);

		ComPtr<ID3D12Fence> fence;
		if (!OpenShared(device, desc.handle, kFenceTypes, fence, error))
		{
			return BinarySemaphoreHandle{};
		}

		// Both counters start at zero here as they do on the exporting side, which is what keeps an exporter that only signals and an importer that only waits in
		// step. See BinarySemaphoreSlot.
		return ReturnValue(device->binarySemaphoreSlots.Store(BinarySemaphoreSlot{ .fence = std::move(fence) }), error);
	}

	bool D3D12CloseExportedHandle([[maybe_unused]] void * impl, const ExternalHandle & handle, Error * error) noexcept
	{
		switch (handle.type)
		{
		// Every handle this backend produces is an NT handle, which an import does not consume, so releasing it here is what ends its life.
		case ExternalHandleType::eOpaqueWin32:
		case ExternalHandleType::eD3D12Resource:
		case ExternalHandleType::eD3D12Heap:
		case ExternalHandleType::eD3D12Fence:
			if (handle.handle != nullptr)
			{
				static_cast<void>(CloseHandle(handle.handle));
			}

			return Succeed(error);

		// Owned by nobody, so closing it is the defect without leaving it alone.
		case ExternalHandleType::eOpaqueWin32Kmt: return Succeed(error);

		case ExternalHandleType::eOpaqueFd:
		case ExternalHandleType::eDmaBuf:
		case ExternalHandleType::eMtlSharedEvent:
		case ExternalHandleType::eMtlSharedTexture: break;
		}

		return Fail(error, ErrorCode::eInvalidArgument, "this backend does not produce handles of that type, so it has nothing to release");
	}

	const ExternalSharingApi & ExternalSharingBlock() noexcept
	{
		static const ExternalSharingApi block{
			.exportBuffer		   = &D3D12ExportBuffer,
			.exportHeap			   = &D3D12ExportHeap,
			.exportTexture		   = &D3D12ExportTexture,
			.exportTimeline		   = &D3D12ExportTimeline,
			.exportBinarySemaphore = &D3D12ExportBinarySemaphore,
			.importBuffer		   = &D3D12ImportBuffer,
			.importHeap			   = &D3D12ImportHeap,
			.importTexture		   = &D3D12ImportTexture,
			.importTimeline		   = &D3D12ImportTimeline,
			.importBinarySemaphore = &D3D12ImportBinarySemaphore,
			.closeExportedHandle   = &D3D12CloseExportedHandle,
		};

		return block;
	}

} // namespace azo::rhi::d3d12

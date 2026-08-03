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

#include "backends/vulkan/internal.hpp"

#ifndef _WIN32
	#include <unistd.h>
#endif

namespace azo::rhi::vulkan
{
	namespace
	{
		/*
		 * How a handle of this type travels, which is what selects the entry point and not the platform doing it.
		 *
		 * An opaque descriptor and a dma-buf both leave through vkGetMemoryFdKHR and every Win32 spelling leaves through vkGetMemoryWin32HandleKHR, so the
		 * transport follows the type even where a device happens to have both families enabled. The Metal types travel by neither, being objects moved over XPC
		 * without operating system handles.
		 */
		enum class Transport : std::uint8_t
		{
			eNone,
			eFd,
			eWin32,
		};

		[[nodiscard]] Transport TransportOf(const ExternalHandleType type) noexcept
		{
			switch (type)
			{
			case ExternalHandleType::eOpaqueFd:
			case ExternalHandleType::eDmaBuf:			return Transport::eFd;
			case ExternalHandleType::eOpaqueWin32:
			case ExternalHandleType::eOpaqueWin32Kmt:
			case ExternalHandleType::eD3D12Resource:
			case ExternalHandleType::eD3D12Heap:
			case ExternalHandleType::eD3D12Fence:		return Transport::eWin32;
			case ExternalHandleType::eMtlSharedEvent:
			case ExternalHandleType::eMtlSharedTexture: break;
			}

			return Transport::eNone;
		}

		constexpr const char * kUndeclared = "export of a handle type this object was not created exportable to";
		constexpr const char * kNoTransport =
			"this device has no external transport for that handle type, which the adapter query answers before an object is created with it";

		/*
		 * The caller's descriptor, duplicated, because Vulkan consumes what it is given.
		 *
		 * VkImportMemoryFdInfoKHR and VkImportSemaphoreFdInfoKHR both take ownership of the descriptor on success, so handing the caller's own in would leave
		 * it holding one it must not close, while a caller that never reached an import would hold one it must. Importing a duplicate leaves ownership where
		 * the caller can reason about it: what it holds is its own until closeExportedHandle releases it, whatever happened here.
		 */
		[[nodiscard]] int DuplicateForImport(const int fd) noexcept
		{
#ifdef _WIN32
			return fd;
#else
			return fd < 0 ? -1 : ::dup(fd);
#endif
		}

		void ReleaseUnconsumed(const int fd) noexcept
		{
#ifndef _WIN32
			if (fd >= 0)
			{
				static_cast<void>(::close(fd));
			}
#else
			static_cast<void>(fd);
#endif
		}

		// Produces a handle for memory already created exportable to this type. declared is what the object was built with, and a type outside it is refused
		// here because vkGetMemoryFdKHR would reject it as a usage error and not an allocation failure.
		[[nodiscard]] bool ExportMemory(VulkanDevice * device, const vk::DeviceMemory memory, const Flags<ExternalHandleType> declared,
			const ExternalHandleType type, ExternalHandle * out, Error * error) noexcept
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

			const std::optional<vk::ExternalMemoryHandleTypeFlagBits> bit = MapMemoryHandleType(type);
			if (!bit)
			{
				return Fail(error, ErrorCode::eUnsupportedFeature, "that handle type carries no Vulkan memory");
			}

			switch (TransportOf(type))
			{
			case Transport::eFd:
			{
				if (!device->externalMemoryFd)
				{
					return Fail(error, ErrorCode::eUnsupportedFeature, kNoTransport);
				}

				const auto got = device->device.getMemoryFdKHR(vk::MemoryGetFdInfoKHR(memory, *bit), device->dispatch);
				if (got.result != vk::Result::eSuccess)
				{
					return Fail(error, ErrorCode::eNativeApiError, "vkGetMemoryFdKHR failed");
				}

				*out = ExternalHandle{ .type = type, .fd = got.value };
				return Succeed(error);
			}

			case Transport::eWin32:
			{
#ifdef VK_USE_PLATFORM_WIN32_KHR
				if (!device->externalMemoryWin32)
				{
					return Fail(error, ErrorCode::eUnsupportedFeature, kNoTransport);
				}

				const auto got = device->device.getMemoryWin32HandleKHR(vk::MemoryGetWin32HandleInfoKHR(memory, *bit), device->dispatch);
				if (got.result != vk::Result::eSuccess)
				{
					return Fail(error, ErrorCode::eNativeApiError, "vkGetMemoryWin32HandleKHR failed");
				}

				*out = ExternalHandle{ .type = type, .handle = got.value };
				return Succeed(error);
#else
				break;
#endif
			}

			case Transport::eNone: break;
			}

			return Fail(error, ErrorCode::eUnsupportedFeature, kNoTransport);
		}

		[[nodiscard]] bool ExportSemaphore(VulkanDevice * device, const vk::Semaphore semaphore, const Flags<ExternalHandleType> declared,
			const ExternalHandleType type, ExternalHandle * out, Error * error) noexcept
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

			const std::optional<vk::ExternalSemaphoreHandleTypeFlagBits> bit = MapSemaphoreHandleType(type);
			if (!bit)
			{
				return Fail(error, ErrorCode::eUnsupportedFeature, "that handle type carries no Vulkan semaphore");
			}

			switch (TransportOf(type))
			{
			case Transport::eFd:
			{
				if (!device->externalSemaphoreFd)
				{
					return Fail(error, ErrorCode::eUnsupportedFeature, kNoTransport);
				}

				const auto got = device->device.getSemaphoreFdKHR(vk::SemaphoreGetFdInfoKHR(semaphore, *bit), device->dispatch);
				if (got.result != vk::Result::eSuccess)
				{
					return Fail(error, ErrorCode::eNativeApiError, "vkGetSemaphoreFdKHR failed");
				}

				*out = ExternalHandle{ .type = type, .fd = got.value };
				return Succeed(error);
			}

			case Transport::eWin32:
			{
#ifdef VK_USE_PLATFORM_WIN32_KHR
				if (!device->externalSemaphoreWin32)
				{
					return Fail(error, ErrorCode::eUnsupportedFeature, kNoTransport);
				}

				const auto got = device->device.getSemaphoreWin32HandleKHR(vk::SemaphoreGetWin32HandleInfoKHR(semaphore, *bit), device->dispatch);
				if (got.result != vk::Result::eSuccess)
				{
					return Fail(error, ErrorCode::eNativeApiError, "vkGetSemaphoreWin32HandleKHR failed");
				}

				*out = ExternalHandle{ .type = type, .handle = got.value };
				return Succeed(error);
#else
				break;
#endif
			}

			case Transport::eNone: break;
			}

			return Fail(error, ErrorCode::eUnsupportedFeature, kNoTransport);
		}

		/*
		 * What the driver said, in the RHI's terms.
		 *
		 * An import can fail four ways that want telling apart: a handle that names nothing this device can open, one it can open but not with the memory type
		 * the resource needs, and the two ordinary exhaustion answers. Reported as separate strings because Error carries a pointer to a literal, not
		 * storage to format into.
		 */
		[[nodiscard]] const char * ImportFailure(const VkResult result) noexcept
		{
			switch (result)
			{
			case VK_ERROR_INVALID_EXTERNAL_HANDLE:
				return "the handle names no payload this device can open, which is what a handle from another device or a corrupted one reports";
			case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "the device had no memory left to back the imported payload";
			case VK_ERROR_OUT_OF_HOST_MEMORY:	return "the host had no memory left for the imported allocation";
			default:							return "the imported memory was refused";
			}
		}

		/*
		 * The pNext chain an imported allocation is made with, held together for the length of one vkAllocateMemory.
		 *
		 * The import structure has to outlive the allocate call and cannot be returned by value without the chain pointer going stale, so the caller owns
		 * this and the fill below points head at it.
		 */
		struct ImportChain final
		{
			vk::ImportMemoryFdInfoKHR fd;
#ifdef VK_USE_PLATFORM_WIN32_KHR
			vk::ImportMemoryWin32HandleInfoKHR win32;
#endif

			// The descriptor handed to Vulkan, which is the caller's duplicated. Closed here if the allocation never happened, and owned by Vulkan once
			// it did.
			int ownedFd = -1;

			void * head = nullptr;
		};

		[[nodiscard]] bool FillImportChain(VulkanDevice * device, const ExternalHandle & handle, ImportChain & chain, Error * error) noexcept
		{
			const std::optional<vk::ExternalMemoryHandleTypeFlagBits> bit = MapMemoryHandleType(handle.type);
			if (!bit)
			{
				return Fail(error, ErrorCode::eUnsupportedFeature, "that handle type carries no Vulkan memory");
			}

			switch (TransportOf(handle.type))
			{
			case Transport::eFd:
			{
				if (!device->externalMemoryFd)
				{
					return Fail(error, ErrorCode::eUnsupportedFeature, kNoTransport);
				}

				chain.ownedFd = DuplicateForImport(handle.fd);
				if (chain.ownedFd < 0)
				{
					return Fail(error, ErrorCode::eInvalidArgument, "import of a handle carrying no file descriptor, or one the process could not duplicate");
				}

				chain.fd.handleType = *bit;
				chain.fd.fd			= chain.ownedFd;
				chain.head			= &chain.fd;
				return Succeed(error);
			}

			case Transport::eWin32:
			{
#ifdef VK_USE_PLATFORM_WIN32_KHR
				if (!device->externalMemoryWin32)
				{
					return Fail(error, ErrorCode::eUnsupportedFeature, kNoTransport);
				}

				if (handle.handle == nullptr)
				{
					return Fail(error, ErrorCode::eInvalidArgument, "import of a handle carrying no Win32 handle");
				}

				chain.win32.handleType = *bit;
				chain.win32.handle	   = handle.handle;
				chain.head			   = &chain.win32;
				return Succeed(error);
#else
				break;
#endif
			}

			case Transport::eNone: break;
			}

			return Fail(error, ErrorCode::eUnsupportedFeature, kNoTransport);
		}

	} // namespace

	bool VulkanExportBuffer(void * impl, const BufferHandle buffer, const ExternalHandleType type, ExternalHandle * out, Error * error) noexcept
	{
		auto * device			= static_cast<VulkanDevice *>(impl);
		const BufferSlot * slot = ResolveBuffer(device, buffer);
		if (slot == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "export of an invalid buffer handle");
		}

		if (slot->allocation == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidArgument, "export of a buffer that owns no allocation, which a sparse or placed buffer does not");
		}

		VmaAllocationInfo info{};
		vmaGetAllocationInfo(device->allocator, slot->allocation, &info);
		return ExportMemory(device, vk::DeviceMemory(info.deviceMemory), slot->exportableHandleTypes, type, out, error);
	}

	bool VulkanExportTexture(void * impl, const TextureHandle texture, const ExternalHandleType type, ExternalHandle * out, Error * error) noexcept
	{
		auto * device			 = static_cast<VulkanDevice *>(impl);
		const TextureSlot * slot = device->textureSlots.Resolve(texture, kHandleAlreadyChecked);
		if (slot == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "export of an invalid texture handle");
		}

		if (slot->allocation == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidArgument, "export of a texture that owns no allocation, which a sparse or borrowed texture does not");
		}

		VmaAllocationInfo info{};
		vmaGetAllocationInfo(device->allocator, slot->allocation, &info);
		return ExportMemory(device, vk::DeviceMemory(info.deviceMemory), slot->exportableHandleTypes, type, out, error);
	}

	bool VulkanExportHeap(void * impl, const HeapHandle heap, const ExternalHandleType type, ExternalHandle * out, Error * error) noexcept
	{
		auto * device		  = static_cast<VulkanDevice *>(impl);
		const HeapSlot * slot = ResolveHeap(device, heap);
		if (slot == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "export of an invalid heap handle");
		}

		return ExportMemory(device, slot->memory, slot->exportableHandleTypes, type, out, error);
	}

	bool VulkanExportTimeline(void * impl, const TimelineHandle timeline, const ExternalHandleType type, ExternalHandle * out, Error * error) noexcept
	{
		auto * device			  = static_cast<VulkanDevice *>(impl);
		const TimelineSlot * slot = device->timelineSlots.Resolve(timeline, kHandleAlreadyChecked);
		if (slot == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "export of an invalid timeline handle");
		}

		return ExportSemaphore(device, slot->semaphore, slot->exportableHandleTypes, type, out, error);
	}

	bool VulkanExportBinarySemaphore(
		void * impl, const BinarySemaphoreHandle semaphore, const ExternalHandleType type, ExternalHandle * out, Error * error) noexcept
	{
		auto * device					 = static_cast<VulkanDevice *>(impl);
		const BinarySemaphoreSlot * slot = device->binarySemaphoreSlots.Resolve(
			BinarySemaphoreHandle{ .index = semaphore.index & ~kDeviceBinarySemaphoreBit, .generation = semaphore.generation }, kHandleAlreadyChecked);
		if (slot == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "export of an invalid binary semaphore handle");
		}

		return ExportSemaphore(device, slot->semaphore, slot->exportableHandleTypes, type, out, error);
	}

	/*
	 * A resource built over memory another component produced. Made through the same VMA entry point the export path uses, with the same allocation create info
	 * and only the chain differing.
	 *
	 * Vulkan requires an import of an opaque handle to state the allocation size and memory type index the payload was created with. It offers no way to
	 * recover either. Calling VMA the way the exporter called it is what reproduces them.
	 */
	BufferHandle VulkanImportBuffer(void * impl, const ExternalBufferImportDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.vulkan.importBuffer");
		auto * device = static_cast<VulkanDevice *>(impl);
		if (desc.desc.size == 0)
		{
			return FailValue<BufferHandle>(error, ErrorCode::eInvalidArgument, "imported buffer size must be greater than zero");
		}

		if (desc.desc.allowSparseBinding)
		{
			return FailValue<BufferHandle>(error, ErrorCode::eInvalidArgument, "a sparse buffer binds its own pages and cannot be built over imported memory");
		}

		// The handle type goes on the buffer before it is created, the same way an exportable one carries its declared set.
		const std::optional<vk::ExternalMemoryHandleTypeFlagBits> bit = MapMemoryHandleType(desc.handle.type);
		if (!bit)
		{
			return FailValue<BufferHandle>(error, ErrorCode::eUnsupportedFeature, "that handle type carries no Vulkan memory");
		}

		const vk::ExternalMemoryBufferCreateInfo externalInfo(*bit);
		vk::BufferCreateInfo bufferCreateInfo({}, desc.desc.size, MapBufferUsage(desc.desc.usage), vk::SharingMode::eExclusive);
		bufferCreateInfo.pNext = &externalInfo;

		ImportChain chain{};
		if (!FillImportChain(device, desc.handle, chain, error))
		{
			return BufferHandle{};
		}

		auto fdGuard = detail::MakeScopeGuard(
			[&]
			{
				ReleaseUnconsumed(chain.ownedFd);
			});

		const VkBufferCreateInfo bufferInfo = bufferCreateInfo;
		VmaAllocationCreateFlags allocFlags = 0;
		VmaAllocationCreateInfo allocInfo{};
		allocInfo.usage = MapMemoryUsage(desc.desc.memory, desc.desc.persistentMap, allocFlags);
		allocInfo.flags = allocFlags;

		VkBuffer raw			 = VK_NULL_HANDLE;
		VmaAllocation allocation = nullptr;
		if (const VkResult result = vmaCreateDedicatedBuffer(device->allocator, &bufferInfo, &allocInfo, chain.head, &raw, &allocation, nullptr);
			result != VK_SUCCESS)
		{
			return FailValue<BufferHandle>(error, ErrorCode::eNativeApiError, ImportFailure(result));
		}

		// Vulkan owns the descriptor from here.
		fdGuard.Dismiss();

		NameVulkanObject(device, vk::ObjectType::eBuffer, std::bit_cast<std::uint64_t>(raw), desc.desc.debugName);

		VkMemoryPropertyFlags memFlags = 0;
		vmaGetAllocationMemoryProperties(device->allocator, allocation, &memFlags);
		const bool mappable = (allocFlags & (VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT)) != 0;

		// The slot records no exportable set, whatever the description carried. The declaration is honored by creation and this payload was created elsewhere,
		// so re-exporting it would hand out a handle to memory this device does not own.
		const BufferHandle handle = device->bufferSlots.Store(BufferSlot{ .buffer = raw,
			.allocation															  = allocation,
			.size																  = desc.desc.size,
			.coherent															  = (memFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0,
			.hostVisible														  = mappable,
			.persistentMapped													  = (allocFlags & VMA_ALLOCATION_CREATE_MAPPED_BIT) != 0,
			.desc																  = detail::Recorded(desc.desc) });
		if (!handle.IsValid())
		{
			vmaDestroyBuffer(device->allocator, raw, allocation);
			return FailValue<BufferHandle>(error, ErrorCode::eOutOfHostMemory, "Vulkan imported buffer handle tracking failed");
		}

		return ReturnValue(handle, error);
	}

	// The texture form of the buffer import above, and symmetric with texture creation for the same reason.
	TextureHandle VulkanImportTexture(void * impl, const ExternalTextureImportDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.vulkan.importTexture");
		auto * device = static_cast<VulkanDevice *>(impl);
		if (desc.desc.allowSparseBinding)
		{
			return FailValue<TextureHandle>(
				error, ErrorCode::eInvalidArgument, "a sparse texture binds its own tiles and cannot be built over imported memory");
		}

		vk::ImageCreateInfo imageCreateInfo{};
		if (!VulkanImageCreateInfo(desc.desc, imageCreateInfo, error))
		{
			return TextureHandle{};
		}

		const std::optional<vk::ExternalMemoryHandleTypeFlagBits> bit = MapMemoryHandleType(desc.handle.type);
		if (!bit)
		{
			return FailValue<TextureHandle>(error, ErrorCode::eUnsupportedFeature, "that handle type carries no Vulkan memory");
		}

		const vk::ExternalMemoryImageCreateInfo externalInfo(*bit);
		imageCreateInfo.pNext = &externalInfo;

		ImportChain chain{};
		if (!FillImportChain(device, desc.handle, chain, error))
		{
			return TextureHandle{};
		}

		auto fdGuard = detail::MakeScopeGuard(
			[&]
			{
				ReleaseUnconsumed(chain.ownedFd);
			});

		const VkImageCreateInfo imageInfo	= imageCreateInfo;
		VmaAllocationCreateFlags allocFlags = 0;
		VmaAllocationCreateInfo allocInfo{};
		allocInfo.usage = MapMemoryUsage(desc.desc.memory, false, allocFlags);
		allocInfo.flags = allocFlags;

		VkImage image			 = VK_NULL_HANDLE;
		VmaAllocation allocation = nullptr;
		if (const VkResult result = vmaCreateDedicatedImage(device->allocator, &imageInfo, &allocInfo, chain.head, &image, &allocation, nullptr);
			result != VK_SUCCESS)
		{
			return FailValue<TextureHandle>(error, ErrorCode::eNativeApiError, ImportFailure(result));
		}

		fdGuard.Dismiss();

		// The declaration cleared for the reason the imported buffer's slot records none. VulkanFinishTexture copies it onto the slot, and this image was
		// created without an export structure, so a slot claiming otherwise would let a later export reach a handle type the allocation never carried.
		TextureDesc imported		   = desc.desc;
		imported.exportableHandleTypes = {};
		return VulkanFinishTexture(device, imported, image, allocation, error);
	}

	HeapHandle VulkanImportHeap(void * impl, const ExternalHeapImportDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.vulkan.importHeap");
		auto * device = static_cast<VulkanDevice *>(impl);
		if (desc.desc.size == 0)
		{
			return FailValue<HeapHandle>(error, ErrorCode::eInvalidArgument, "imported heap size must be greater than zero");
		}

		std::uint32_t typeIndex = 0;
		bool hostVisible		= false;
		bool coherent			= false;
		if (!FindMemoryTypeForHeap(device->phys, device->dispatch, desc.desc.type, typeIndex, hostVisible, coherent))
		{
			return FailValue<HeapHandle>(error, ErrorCode::eUnsupportedFeature, "no memory type matches the imported heap");
		}

		ImportChain chain{};
		if (!FillImportChain(device, desc.handle, chain, error))
		{
			return HeapHandle{};
		}

		auto fdGuard = detail::MakeScopeGuard(
			[&]
			{
				ReleaseUnconsumed(chain.ownedFd);
			});

		vk::MemoryAllocateInfo allocateInfo(desc.desc.size, typeIndex);
		allocateInfo.pNext = chain.head;

		const auto allocated = device->device.allocateMemory(allocateInfo, nullptr, device->dispatch);
		if (allocated.result != vk::Result::eSuccess)
		{
			return FailValue<HeapHandle>(error, ErrorCode::eNativeApiError, ImportFailure(static_cast<VkResult>(allocated.result)));
		}

		fdGuard.Dismiss();

		// No export declaration on the slot, for the reason the imported buffer and texture record none.
		const HeapHandle handle = device->heapSlots.Store(
			HeapSlot{ .memory = allocated.value, .size = desc.desc.size, .memoryTypeIndex = typeIndex, .hostVisible = hostVisible, .coherent = coherent });
		if (!handle.IsValid())
		{
			device->device.freeMemory(allocated.value, nullptr, device->dispatch);
			return FailValue<HeapHandle>(error, ErrorCode::eOutOfHostMemory, "Vulkan imported heap handle tracking failed");
		}

		return ReturnValue(handle, error);
	}

	namespace
	{
		/*
		 * Takes a semaphore over an imported payload, permanently.
		 *
		 * Permanent, not temporary because a temporary import reverts after one wait and is not allowed on a timeline at all, so carrying the mode would
		 * carry one the primitive this feature exists for cannot use.
		 */
		[[nodiscard]] bool ImportSemaphorePayload(VulkanDevice * device, const vk::Semaphore semaphore, const ExternalHandle & handle, Error * error) noexcept
		{
			const std::optional<vk::ExternalSemaphoreHandleTypeFlagBits> bit = MapSemaphoreHandleType(handle.type);
			if (!bit)
			{
				return Fail(error, ErrorCode::eUnsupportedFeature, "that handle type carries no Vulkan semaphore");
			}

			switch (TransportOf(handle.type))
			{
			case Transport::eFd:
			{
				if (!device->externalSemaphoreFd)
				{
					return Fail(error, ErrorCode::eUnsupportedFeature, kNoTransport);
				}

				const int owned = DuplicateForImport(handle.fd);
				if (owned < 0)
				{
					return Fail(error, ErrorCode::eInvalidArgument, "import of a handle carrying no file descriptor, or one the process could not duplicate");
				}

				vk::ImportSemaphoreFdInfoKHR info;
				info.semaphore	= semaphore;
				info.handleType = *bit;
				info.fd			= owned;
				if (device->device.importSemaphoreFdKHR(info, device->dispatch) != vk::Result::eSuccess)
				{
					ReleaseUnconsumed(owned);
					return Fail(
						error, ErrorCode::eNativeApiError, "the imported semaphore payload was refused, which is how a handle from another device fails");
				}

				return Succeed(error);
			}

			case Transport::eWin32:
			{
#ifdef VK_USE_PLATFORM_WIN32_KHR
				if (!device->externalSemaphoreWin32)
				{
					return Fail(error, ErrorCode::eUnsupportedFeature, kNoTransport);
				}

				if (handle.handle == nullptr)
				{
					return Fail(error, ErrorCode::eInvalidArgument, "import of a handle carrying no Win32 handle");
				}

				vk::ImportSemaphoreWin32HandleInfoKHR info;
				info.semaphore	= semaphore;
				info.handleType = *bit;
				info.handle		= handle.handle;
				if (device->device.importSemaphoreWin32HandleKHR(info, device->dispatch) != vk::Result::eSuccess)
				{
					return Fail(
						error, ErrorCode::eNativeApiError, "the imported semaphore payload was refused, which is how a handle from another device fails");
				}

				return Succeed(error);
#else
				break;
#endif
			}

			case Transport::eNone: break;
			}

			return Fail(error, ErrorCode::eUnsupportedFeature, kNoTransport);
		}
	} // namespace

	TimelineHandle VulkanImportTimeline(void * impl, const ExternalTimelineImportDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.vulkan.importTimeline");
		auto * device = static_cast<VulkanDevice *>(impl);

		// No initial value and no export declaration: the payload arrives with the counter the exporter left it at, and this semaphore is not the one
		// that owns it.
		const vk::SemaphoreTypeCreateInfo typeInfo(vk::SemaphoreType::eTimeline, 0);
		const auto created = device->device.createSemaphore(vk::SemaphoreCreateInfo({}, &typeInfo), nullptr, device->dispatch);
		if (created.result != vk::Result::eSuccess)
		{
			return FailValue<TimelineHandle>(error, ErrorCode::eNativeApiError, "Vulkan imported timeline creation failed");
		}

		const vk::Semaphore semaphore = created.value;
		auto semaphoreGuard			  = detail::MakeScopeGuard(
			[&]
			{
				device->device.destroySemaphore(semaphore, nullptr, device->dispatch);
			});

		if (!ImportSemaphorePayload(device, semaphore, desc.handle, error))
		{
			return TimelineHandle{};
		}

		NameVulkanObject(device, vk::ObjectType::eSemaphore, std::bit_cast<std::uint64_t>(static_cast<VkSemaphore>(semaphore)), desc.desc.debugName);

		const TimelineHandle handle = device->timelineSlots.Store(TimelineSlot{ .semaphore = semaphore });
		if (!handle.IsValid())
		{
			return FailValue<TimelineHandle>(error, ErrorCode::eOutOfHostMemory, "Vulkan imported timeline handle tracking failed");
		}

		semaphoreGuard.Dismiss();
		return ReturnValue(handle, error);
	}

	BinarySemaphoreHandle VulkanImportBinarySemaphore(void * impl, const ExternalBinarySemaphoreImportDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.vulkan.importBinarySemaphore");
		auto * device	   = static_cast<VulkanDevice *>(impl);
		const auto created = device->device.createSemaphore(vk::SemaphoreCreateInfo{}, nullptr, device->dispatch);
		if (created.result != vk::Result::eSuccess)
		{
			return FailValue<BinarySemaphoreHandle>(error, ErrorCode::eNativeApiError, "Vulkan imported binary semaphore creation failed");
		}

		const vk::Semaphore semaphore = created.value;
		auto semaphoreGuard			  = detail::MakeScopeGuard(
			[&]
			{
				device->device.destroySemaphore(semaphore, nullptr, device->dispatch);
			});

		if (!ImportSemaphorePayload(device, semaphore, desc.handle, error))
		{
			return BinarySemaphoreHandle{};
		}

		NameVulkanObject(device, vk::ObjectType::eSemaphore, std::bit_cast<std::uint64_t>(static_cast<VkSemaphore>(semaphore)), desc.desc.debugName);

		BinarySemaphoreHandle handle = device->binarySemaphoreSlots.Store(BinarySemaphoreSlot{ .semaphore = semaphore });
		if (!handle.IsValid())
		{
			return FailValue<BinarySemaphoreHandle>(error, ErrorCode::eOutOfHostMemory, "Vulkan imported binary semaphore handle tracking failed");
		}

		semaphoreGuard.Dismiss();
		handle.index |= kDeviceBinarySemaphoreBit; // the same tag an RHI-created binary semaphore carries, so resolve reads the device registry
		return ReturnValue(handle, error);
	}

	bool VulkanCloseExportedHandle([[maybe_unused]] void * impl, const ExternalHandle & handle, Error * error) noexcept
	{
		switch (handle.type)
		{
		/*
		 * A descriptor the caller still owns, import having taken a duplicate, not this one.
		 */
		case ExternalHandleType::eOpaqueFd:
		case ExternalHandleType::eDmaBuf:	ReleaseUnconsumed(handle.fd); return Succeed(error);

		/*
		 * NT handles, which an import does not consume either.
		 */
		// NOLINTNEXTLINE(bugprone-branch-clone): only off Windows, where the CloseHandle below compiles away and this collapses onto the Kmt case.
		case ExternalHandleType::eOpaqueWin32:
		case ExternalHandleType::eD3D12Resource:
		case ExternalHandleType::eD3D12Heap:
		case ExternalHandleType::eD3D12Fence:
#ifdef VK_USE_PLATFORM_WIN32_KHR
			if (handle.handle != nullptr)
			{
				static_cast<void>(CloseHandle(handle.handle));
			}
#endif
			return Succeed(error);

		// Owned by nobody, so closing it is the defect without leaving it alone.
		case ExternalHandleType::eOpaqueWin32Kmt: return Succeed(error);

		// Never produced here, and released by Metal's own reference counting where they are.
		case ExternalHandleType::eMtlSharedEvent:
		case ExternalHandleType::eMtlSharedTexture: break;
		}

		return Fail(error, ErrorCode::eInvalidArgument, "this backend does not produce handles of that type, so it has nothing to release");
	}

	const ExternalSharingApi & ExternalSharingBlock() noexcept
	{
		static const ExternalSharingApi block{
			.exportBuffer		   = &VulkanExportBuffer,
			.exportHeap			   = &VulkanExportHeap,
			.exportTexture		   = &VulkanExportTexture,
			.exportTimeline		   = &VulkanExportTimeline,
			.exportBinarySemaphore = &VulkanExportBinarySemaphore,
			.importBuffer		   = &VulkanImportBuffer,
			.importHeap			   = &VulkanImportHeap,
			.importTexture		   = &VulkanImportTexture,
			.importTimeline		   = &VulkanImportTimeline,
			.importBinarySemaphore = &VulkanImportBinarySemaphore,
			.closeExportedHandle   = &VulkanCloseExportedHandle,
		};

		return block;
	}

} // namespace azo::rhi::vulkan

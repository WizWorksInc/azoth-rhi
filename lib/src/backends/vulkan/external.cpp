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
#include "azoth/rhi/backend/support/scope_guard.hpp"
#include "azoth/rhi/commands/sync.hpp"
#include "azoth/rhi/core/flags.hpp"
#include "azoth/rhi/core/profiling.hpp"
#include "azoth/rhi/core/resource_handles.hpp"
#include "azoth/rhi/core/result.hpp"
#include "azoth/rhi/resources/resources.hpp"

#include "backends/vulkan/internal.hpp"
#include "backends/vulkan/swapchain_bundle.hpp"
#include "vulkan/vulkan.hpp"

#include <vulkan/vulkan_core.h>

#include <bit>
#include <cstdint>
#include <optional>

#ifndef _WIN32
	#include <unistd.h>
#endif

namespace azo::rhi::vulkan
{
	namespace
	{
		enum class Transport : std::uint8_t
		{
			eNone,
			eFd,
			eWin32,
		};

		[[nodiscard]] Transport transport_of(const ExternalHandleType type) noexcept
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

		[[nodiscard]] int duplicate_for_import(const int fd) noexcept
		{
#ifdef _WIN32
			return fd;
#else
			return fd < 0 ? -1 : ::dup(fd);
#endif
		}

		void release_unconsumed(const int fd) noexcept
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

		[[nodiscard]] bool export_memory(
			VulkanDevice * device,
			const vk::DeviceMemory memory,
			const Flags<ExternalHandleType> declared,
			const ExternalHandleType type,
			ExternalHandle * out,
			Error * error
		) noexcept
		{
			if (out == nullptr)
			{
				return fail(error, ErrorCode::eInvalidArgument, "external export needs somewhere to write the handle");
			}

			*out = {};
			if (!declared.contains(type))
			{
				return fail(error, ErrorCode::eInvalidArgument, kUndeclared);
			}

			const std::optional<vk::ExternalMemoryHandleTypeFlagBits> bit = map_memory_handle_type(type);
			if (!bit)
			{
				return fail(error, ErrorCode::eUnsupportedFeature, "that handle type carries no Vulkan memory");
			}

			switch (transport_of(type))
			{
			case Transport::eFd:
			{
				if (!device->externalMemoryFd)
				{
					return fail(error, ErrorCode::eUnsupportedFeature, kNoTransport);
				}

				const auto got = device->device.getMemoryFdKHR(vk::MemoryGetFdInfoKHR(memory, *bit), device->dispatch);
				if (got.result != vk::Result::eSuccess)
				{
					return fail_native(error, "vkGetMemoryFdKHR failed", got.result);
				}

				*out = ExternalHandle{ .type = type, .fd = got.value };
				return succeed(error);
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
					return FailNative(error, "vkGetMemoryWin32HandleKHR failed", got.result);
				}

				*out = ExternalHandle{ .type = type, .handle = got.value };
				return Succeed(error);
#else
				break;
#endif
			}

			case Transport::eNone: break;
			}

			return fail(error, ErrorCode::eUnsupportedFeature, kNoTransport);
		}

		[[nodiscard]] bool export_semaphore(
			VulkanDevice * device,
			const vk::Semaphore semaphore,
			const Flags<ExternalHandleType> declared,
			const ExternalHandleType type,
			ExternalHandle * out,
			Error * error
		) noexcept
		{
			if (out == nullptr)
			{
				return fail(error, ErrorCode::eInvalidArgument, "external export needs somewhere to write the handle");
			}

			*out = {};
			if (!declared.contains(type))
			{
				return fail(error, ErrorCode::eInvalidArgument, kUndeclared);
			}

			const std::optional<vk::ExternalSemaphoreHandleTypeFlagBits> bit = map_semaphore_handle_type(type);
			if (!bit)
			{
				return fail(error, ErrorCode::eUnsupportedFeature, "that handle type carries no Vulkan semaphore");
			}

			switch (transport_of(type))
			{
			case Transport::eFd:
			{
				if (!device->externalSemaphoreFd)
				{
					return fail(error, ErrorCode::eUnsupportedFeature, kNoTransport);
				}

				const auto got = device->device.getSemaphoreFdKHR(vk::SemaphoreGetFdInfoKHR(semaphore, *bit), device->dispatch);
				if (got.result != vk::Result::eSuccess)
				{
					return fail_native(error, "vkGetSemaphoreFdKHR failed", got.result);
				}

				*out = ExternalHandle{ .type = type, .fd = got.value };
				return succeed(error);
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
					return FailNative(error, "vkGetSemaphoreWin32HandleKHR failed", got.result);
				}

				*out = ExternalHandle{ .type = type, .handle = got.value };
				return Succeed(error);
#else
				break;
#endif
			}

			case Transport::eNone: break;
			}

			return fail(error, ErrorCode::eUnsupportedFeature, kNoTransport);
		}

		[[nodiscard]] const char * import_failure(const VkResult result) noexcept
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

		struct ImportChain final
		{
			vk::ImportMemoryFdInfoKHR fd;
#ifdef VK_USE_PLATFORM_WIN32_KHR
			vk::ImportMemoryWin32HandleInfoKHR win32;
#endif

			int ownedFd = -1;

			void * head = nullptr;
		};

		[[nodiscard]] bool fill_import_chain(VulkanDevice * device, const ExternalHandle & handle, ImportChain & chain, Error * error) noexcept
		{
			const std::optional<vk::ExternalMemoryHandleTypeFlagBits> bit = map_memory_handle_type(handle.type);
			if (!bit)
			{
				return fail(error, ErrorCode::eUnsupportedFeature, "that handle type carries no Vulkan memory");
			}

			switch (transport_of(handle.type))
			{
			case Transport::eFd:
			{
				if (!device->externalMemoryFd)
				{
					return fail(error, ErrorCode::eUnsupportedFeature, kNoTransport);
				}

				chain.ownedFd = duplicate_for_import(handle.fd);
				if (chain.ownedFd < 0)
				{
					return fail(error, ErrorCode::eInvalidArgument, "import of a handle carrying no file descriptor, or one the process could not duplicate");
				}

				chain.fd.handleType = *bit;
				chain.fd.fd			= chain.ownedFd;
				chain.head			= &chain.fd;
				return succeed(error);
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

			return fail(error, ErrorCode::eUnsupportedFeature, kNoTransport);
		}

	}

	bool vulkan_export_buffer(void * impl, const BufferHandle buffer, const ExternalHandleType type, ExternalHandle * out, Error * error) noexcept
	{
		auto * device			= static_cast<VulkanDevice *>(impl);
		const BufferSlot * slot = resolve_buffer(device, buffer);
		if (slot == nullptr)
		{
			return fail(error, ErrorCode::eInvalidHandle, "export of an invalid buffer handle");
		}

		if (slot->allocation == nullptr)
		{
			return fail(error, ErrorCode::eInvalidArgument, "export of a buffer that owns no allocation, which a sparse or placed buffer does not");
		}

		VmaAllocationInfo info{};
		vmaGetAllocationInfo(device->allocator, slot->allocation, &info);
		return export_memory(device, vk::DeviceMemory(info.deviceMemory), slot->exportableHandleTypes, type, out, error);
	}

	bool vulkan_export_texture(void * impl, const TextureHandle texture, const ExternalHandleType type, ExternalHandle * out, Error * error) noexcept
	{
		auto * device			 = static_cast<VulkanDevice *>(impl);
		const TextureSlot * slot = device->textureSlots.resolve(texture, kHandleAlreadyChecked);
		if (slot == nullptr)
		{
			return fail(error, ErrorCode::eInvalidHandle, "export of an invalid texture handle");
		}

		if (slot->allocation == nullptr)
		{
			return fail(error, ErrorCode::eInvalidArgument, "export of a texture that owns no allocation, which a sparse or borrowed texture does not");
		}

		VmaAllocationInfo info{};
		vmaGetAllocationInfo(device->allocator, slot->allocation, &info);
		return export_memory(device, vk::DeviceMemory(info.deviceMemory), slot->exportableHandleTypes, type, out, error);
	}

	bool vulkan_export_heap(void * impl, const HeapHandle heap, const ExternalHandleType type, ExternalHandle * out, Error * error) noexcept
	{
		auto * device		  = static_cast<VulkanDevice *>(impl);
		const HeapSlot * slot = resolve_heap(device, heap);
		if (slot == nullptr)
		{
			return fail(error, ErrorCode::eInvalidHandle, "export of an invalid heap handle");
		}

		return export_memory(device, slot->memory, slot->exportableHandleTypes, type, out, error);
	}

	bool vulkan_export_timeline(void * impl, const TimelineHandle timeline, const ExternalHandleType type, ExternalHandle * out, Error * error) noexcept
	{
		auto * device			  = static_cast<VulkanDevice *>(impl);
		const TimelineSlot * slot = device->timelineSlots.resolve(timeline, kHandleAlreadyChecked);
		if (slot == nullptr)
		{
			return fail(error, ErrorCode::eInvalidHandle, "export of an invalid timeline handle");
		}

		return export_semaphore(device, slot->semaphore, slot->exportableHandleTypes, type, out, error);
	}

	bool vulkan_export_binary_semaphore(
		void * impl,
		const BinarySemaphoreHandle semaphore,
		const ExternalHandleType type,
		ExternalHandle * out,
		Error * error
	) noexcept
	{
		auto * device					 = static_cast<VulkanDevice *>(impl);
		const BinarySemaphoreSlot * slot = device->binarySemaphoreSlots.resolve(
			BinarySemaphoreHandle{ .index = semaphore.index & ~kDeviceBinarySemaphoreBit, .generation = semaphore.generation },
			kHandleAlreadyChecked
		);
		if (slot == nullptr)
		{
			return fail(error, ErrorCode::eInvalidHandle, "export of an invalid binary semaphore handle");
		}

		return export_semaphore(device, slot->semaphore, slot->exportableHandleTypes, type, out, error);
	}

	BufferHandle vulkan_import_buffer(void * impl, const ExternalBufferImportDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.vulkan.importBuffer");
		auto * device = static_cast<VulkanDevice *>(impl);
		if (desc.desc.size == 0)
		{
			return fail_value<BufferHandle>(error, ErrorCode::eInvalidArgument, "imported buffer size must be greater than zero");
		}

		if (desc.desc.allowSparseBinding)
		{
			return fail_value<BufferHandle>(error, ErrorCode::eInvalidArgument, "a sparse buffer binds its own pages and cannot be built over imported memory");
		}

		if (!vulkan_refuse_ray_tracing_usage(desc.desc.usage, device->caps.supportsRayTracing, error))
		{
			return BufferHandle{};
		}

		const std::optional<vk::ExternalMemoryHandleTypeFlagBits> bit = map_memory_handle_type(desc.handle.type);
		if (!bit)
		{
			return fail_value<BufferHandle>(error, ErrorCode::eUnsupportedFeature, "that handle type carries no Vulkan memory");
		}

		const vk::ExternalMemoryBufferCreateInfo externalInfo(*bit);
		vk::BufferCreateInfo bufferCreateInfo({}, desc.desc.size, map_buffer_usage(desc.desc.usage), vk::SharingMode::eExclusive);
		bufferCreateInfo.pNext = &externalInfo;

		ImportChain chain{};
		if (!fill_import_chain(device, desc.handle, chain, error))
		{
			return BufferHandle{};
		}

		auto fdGuard = detail::make_scope_guard(
			[&]
			{
				release_unconsumed(chain.ownedFd);
			}
		);

		const VkBufferCreateInfo bufferInfo = bufferCreateInfo;
		VmaAllocationCreateFlags allocFlags = 0;
		VmaAllocationCreateInfo allocInfo{};
		allocInfo.usage = map_buffer_memory_usage(desc.desc.memory, allocFlags);
		allocInfo.flags = allocFlags;

		VkBuffer raw			 = VK_NULL_HANDLE;
		VmaAllocation allocation = nullptr;
		if (const VkResult result = vmaCreateDedicatedBuffer(device->allocator, &bufferInfo, &allocInfo, chain.head, &raw, &allocation, nullptr);
			result != VK_SUCCESS)
		{
			return fail_native_value<BufferHandle>(error, import_failure(result), static_cast<vk::Result>(result));
		}

		fdGuard.dismiss();

		name_vulkan_object(device, vk::ObjectType::eBuffer, std::bit_cast<std::uint64_t>(raw), desc.desc.debugName);

		VkMemoryPropertyFlags memFlags = 0;
		vmaGetAllocationMemoryProperties(device->allocator, allocation, &memFlags);
		const bool mappable = (allocFlags & (VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT)) != 0;

		VmaAllocationInfo allocated{};
		vmaGetAllocationInfo(device->allocator, allocation, &allocated);
		if (mappable && allocated.pMappedData == nullptr)
		{
			vmaDestroyBuffer(device->allocator, raw, allocation);
			return fail_value<BufferHandle>(
				error,
				ErrorCode::eNativeApiError,
				"a host visible imported buffer came back from VMA without the mapping it asked for"
			);
		}

		const BufferHandle handle = device->bufferSlots.store(
			BufferSlot{
				.buffer		 = raw,
				.allocation	 = allocation,
				.size		 = desc.desc.size,
				.coherent	 = (memFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0,
				.hostVisible = mappable,
				.mapped		 = allocated.pMappedData,
				.desc		 = detail::recorded(desc.desc),
			}
		);
		if (!handle.is_valid())
		{
			vmaDestroyBuffer(device->allocator, raw, allocation);
			return fail_value<BufferHandle>(error, ErrorCode::eOutOfHostMemory, "Vulkan imported buffer handle tracking failed");
		}

		return return_value(handle, error);
	}

	TextureHandle vulkan_import_texture(void * impl, const ExternalTextureImportDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.vulkan.importTexture");
		auto * device = static_cast<VulkanDevice *>(impl);
		if (desc.desc.allowSparseBinding)
		{
			return fail_value<TextureHandle>(
				error,
				ErrorCode::eInvalidArgument,
				"a sparse texture binds its own tiles and cannot be built over imported memory"
			);
		}

		vk::ImageCreateInfo imageCreateInfo{};
		if (!vulkan_image_create_info(desc.desc, imageCreateInfo, error))
		{
			return TextureHandle{};
		}

		const std::optional<vk::ExternalMemoryHandleTypeFlagBits> bit = map_memory_handle_type(desc.handle.type);
		if (!bit)
		{
			return fail_value<TextureHandle>(error, ErrorCode::eUnsupportedFeature, "that handle type carries no Vulkan memory");
		}

		const vk::ExternalMemoryImageCreateInfo externalInfo(*bit);
		imageCreateInfo.pNext = &externalInfo;

		ImportChain chain{};
		if (!fill_import_chain(device, desc.handle, chain, error))
		{
			return TextureHandle{};
		}

		auto fdGuard = detail::make_scope_guard(
			[&]
			{
				release_unconsumed(chain.ownedFd);
			}
		);

		const VkImageCreateInfo imageInfo	= imageCreateInfo;
		VmaAllocationCreateFlags allocFlags = 0;
		VmaAllocationCreateInfo allocInfo{};
		allocInfo.usage = map_memory_usage(desc.desc.memory, allocFlags);
		allocInfo.flags = allocFlags;

		VkImage image			 = VK_NULL_HANDLE;
		VmaAllocation allocation = nullptr;
		if (const VkResult result = vmaCreateDedicatedImage(device->allocator, &imageInfo, &allocInfo, chain.head, &image, &allocation, nullptr);
			result != VK_SUCCESS)
		{
			return fail_native_value<TextureHandle>(error, import_failure(result), static_cast<vk::Result>(result));
		}

		fdGuard.dismiss();

		TextureDesc imported		   = desc.desc;
		imported.exportableHandleTypes = {};
		return vulkan_finish_texture(device, imported, image, allocation, error);
	}

	HeapHandle vulkan_import_heap(void * impl, const ExternalHeapImportDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.vulkan.importHeap");
		auto * device = static_cast<VulkanDevice *>(impl);
		if (desc.desc.size == 0)
		{
			return fail_value<HeapHandle>(error, ErrorCode::eInvalidArgument, "imported heap size must be greater than zero");
		}

		std::uint32_t typeIndex = 0;
		bool hostVisible		= false;
		bool coherent			= false;
		if (!find_memory_type_for_heap(device->phys, device->dispatch, desc.desc.type, typeIndex, hostVisible, coherent))
		{
			return fail_value<HeapHandle>(error, ErrorCode::eUnsupportedFeature, "no memory type matches the imported heap");
		}

		ImportChain chain{};
		if (!fill_import_chain(device, desc.handle, chain, error))
		{
			return HeapHandle{};
		}

		auto fdGuard = detail::make_scope_guard(
			[&]
			{
				release_unconsumed(chain.ownedFd);
			}
		);

		vk::MemoryAllocateInfo allocateInfo(desc.desc.size, typeIndex);
		allocateInfo.pNext = chain.head;

		const auto allocated = device->device.allocateMemory(allocateInfo, nullptr, device->dispatch);
		if (allocated.result != vk::Result::eSuccess)
		{
			return fail_native_value<HeapHandle>(error, import_failure(static_cast<VkResult>(allocated.result)), allocated.result);
		}

		fdGuard.dismiss();

		void * mapped = nullptr;
		if (hostVisible)
		{
			if (const vk::Result mapResult = device->device.mapMemory(allocated.value, 0, VK_WHOLE_SIZE, vk::MemoryMapFlags{}, &mapped, device->dispatch);
				mapResult != vk::Result::eSuccess)
			{
				device->device.freeMemory(allocated.value, nullptr, device->dispatch);
				return fail_native_value<HeapHandle>(error, "vkMapMemory failed for a host visible imported heap", mapResult);
			}
		}

		const HeapHandle handle = device->heapSlots.store(
			HeapSlot{
				.memory			 = allocated.value,
				.size			 = desc.desc.size,
				.memoryTypeIndex = typeIndex,
				.hostVisible	 = hostVisible,
				.coherent		 = coherent,
				.mapped			 = mapped,
			}
		);
		if (!handle.is_valid())
		{
			device->device.freeMemory(allocated.value, nullptr, device->dispatch);
			return fail_value<HeapHandle>(error, ErrorCode::eOutOfHostMemory, "Vulkan imported heap handle tracking failed");
		}

		return return_value(handle, error);
	}

	namespace
	{
		[[nodiscard]] bool import_semaphore_payload(VulkanDevice * device, const vk::Semaphore semaphore, const ExternalHandle & handle, Error * error) noexcept
		{
			const std::optional<vk::ExternalSemaphoreHandleTypeFlagBits> bit = map_semaphore_handle_type(handle.type);
			if (!bit)
			{
				return fail(error, ErrorCode::eUnsupportedFeature, "that handle type carries no Vulkan semaphore");
			}

			switch (transport_of(handle.type))
			{
			case Transport::eFd:
			{
				if (!device->externalSemaphoreFd)
				{
					return fail(error, ErrorCode::eUnsupportedFeature, kNoTransport);
				}

				const int owned = duplicate_for_import(handle.fd);
				if (owned < 0)
				{
					return fail(error, ErrorCode::eInvalidArgument, "import of a handle carrying no file descriptor, or one the process could not duplicate");
				}

				vk::ImportSemaphoreFdInfoKHR info;
				info.semaphore	= semaphore;
				info.handleType = *bit;
				info.fd			= owned;
				if (const vk::Result imported = device->device.importSemaphoreFdKHR(info, device->dispatch); imported != vk::Result::eSuccess)
				{
					release_unconsumed(owned);
					return fail_native(error, "the imported semaphore payload was refused, which is how a handle from another device fails", imported);
				}

				return succeed(error);
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
				if (const vk::Result imported = device->device.importSemaphoreWin32HandleKHR(info, device->dispatch); imported != vk::Result::eSuccess)
				{
					return FailNative(error, "the imported semaphore payload was refused, which is how a handle from another device fails", imported);
				}

				return Succeed(error);
#else
				break;
#endif
			}

			case Transport::eNone: break;
			}

			return fail(error, ErrorCode::eUnsupportedFeature, kNoTransport);
		}
	}

	TimelineHandle vulkan_import_timeline(void * impl, const ExternalTimelineImportDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.vulkan.importTimeline");
		auto * device = static_cast<VulkanDevice *>(impl);

		const vk::SemaphoreTypeCreateInfo typeInfo(vk::SemaphoreType::eTimeline, 0);
		const auto created = device->device.createSemaphore(vk::SemaphoreCreateInfo({}, &typeInfo), nullptr, device->dispatch);
		if (created.result != vk::Result::eSuccess)
		{
			return fail_native_value<TimelineHandle>(error, "Vulkan imported timeline creation failed", created.result);
		}

		const vk::Semaphore semaphore = created.value;
		auto semaphoreGuard			  = detail::make_scope_guard(
			[&]
			{
				device->device.destroySemaphore(semaphore, nullptr, device->dispatch);
			}
		);

		if (!import_semaphore_payload(device, semaphore, desc.handle, error))
		{
			return TimelineHandle{};
		}

		name_vulkan_object(device, vk::ObjectType::eSemaphore, std::bit_cast<std::uint64_t>(static_cast<VkSemaphore>(semaphore)), desc.desc.debugName);

		const TimelineHandle handle = device->timelineSlots.store(TimelineSlot{ .semaphore = semaphore });
		if (!handle.is_valid())
		{
			return fail_value<TimelineHandle>(error, ErrorCode::eOutOfHostMemory, "Vulkan imported timeline handle tracking failed");
		}

		semaphoreGuard.dismiss();
		return return_value(handle, error);
	}

	BinarySemaphoreHandle vulkan_import_binary_semaphore(void * impl, const ExternalBinarySemaphoreImportDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.vulkan.importBinarySemaphore");
		auto * device	   = static_cast<VulkanDevice *>(impl);
		const auto created = device->device.createSemaphore(vk::SemaphoreCreateInfo{}, nullptr, device->dispatch);
		if (created.result != vk::Result::eSuccess)
		{
			return fail_native_value<BinarySemaphoreHandle>(error, "Vulkan imported binary semaphore creation failed", created.result);
		}

		const vk::Semaphore semaphore = created.value;
		auto semaphoreGuard			  = detail::make_scope_guard(
			[&]
			{
				device->device.destroySemaphore(semaphore, nullptr, device->dispatch);
			}
		);

		if (!import_semaphore_payload(device, semaphore, desc.handle, error))
		{
			return BinarySemaphoreHandle{};
		}

		name_vulkan_object(device, vk::ObjectType::eSemaphore, std::bit_cast<std::uint64_t>(static_cast<VkSemaphore>(semaphore)), desc.desc.debugName);

		BinarySemaphoreHandle handle = device->binarySemaphoreSlots.store(BinarySemaphoreSlot{ .semaphore = semaphore });
		if (!handle.is_valid())
		{
			return fail_value<BinarySemaphoreHandle>(error, ErrorCode::eOutOfHostMemory, "Vulkan imported binary semaphore handle tracking failed");
		}

		semaphoreGuard.dismiss();
		handle.index |= kDeviceBinarySemaphoreBit;
		return return_value(handle, error);
	}

	bool vulkan_close_exported_handle([[maybe_unused]] void * impl, const ExternalHandle & handle, Error * error) noexcept
	{
		switch (handle.type)
		{
		case ExternalHandleType::eOpaqueFd:
		case ExternalHandleType::eDmaBuf:	release_unconsumed(handle.fd); return succeed(error);

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
			return succeed(error);

		case ExternalHandleType::eOpaqueWin32Kmt: return succeed(error);

		case ExternalHandleType::eMtlSharedEvent:
		case ExternalHandleType::eMtlSharedTexture: break;
		}

		return fail(error, ErrorCode::eInvalidArgument, "this backend does not produce handles of that type, so it has nothing to release");
	}

	const ExternalSharingApi & external_sharing_block() noexcept
	{
		static const ExternalSharingApi block{
			.exportBuffer		   = &vulkan_export_buffer,
			.exportHeap			   = &vulkan_export_heap,
			.exportTexture		   = &vulkan_export_texture,
			.exportTimeline		   = &vulkan_export_timeline,
			.exportBinarySemaphore = &vulkan_export_binary_semaphore,
			.importBuffer		   = &vulkan_import_buffer,
			.importHeap			   = &vulkan_import_heap,
			.importTexture		   = &vulkan_import_texture,
			.importTimeline		   = &vulkan_import_timeline,
			.importBinarySemaphore = &vulkan_import_binary_semaphore,
			.closeExportedHandle   = &vulkan_close_exported_handle,
		};

		return block;
	}

}

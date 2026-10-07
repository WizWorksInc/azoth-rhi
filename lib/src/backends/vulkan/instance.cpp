// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#include "azoth/rhi/backend/support/host_containers.hpp"
#include "azoth/rhi/core/enums.hpp"
#include "azoth/rhi/core/external.hpp"
#include "azoth/rhi/core/flags.hpp"
#include "azoth/rhi/core/result.hpp"
#include "azoth/rhi/device/api_tags.hpp"
#include "azoth/rhi/device/device.hpp"
#include "azoth/rhi/host/allocator.hpp"

#include "backends/vulkan/internal.hpp"
#include "support/driver_version.hpp"
#include "vulkan/vulkan.hpp"

#include <vulkan/vulkan.hpp>

#include <cstdint>
#include <optional>
#include <span>

namespace azo::rhi::vulkan
{
	GraphicsApiId vulkan_instance_api_id([[maybe_unused]] void * impl) noexcept
	{
		return VulkanApi::kId;
	}

	bool vulkan_enumerate_adapters(void * impl, std::span<AdapterInfo> adapters, std::uint32_t * out, Error * error) noexcept
	{
		if (out == nullptr)
		{
			return fail(error, ErrorCode::eInvalidArgument, "adapter count output pointer is null");
		}

		*out			= 0;
		auto * instance = static_cast<VulkanInstance *>(impl);

		const auto enumerated = instance->instance.enumeratePhysicalDevices<HostAllocatorAdapter<vk::PhysicalDevice>>(instance->dispatch);
		if (enumerated.result != vk::Result::eSuccess)
		{
			return fail_native(error, "Vulkan adapter enumeration failed", enumerated.result);
		}

		const detail::HostVector<vk::PhysicalDevice> & physicals = enumerated.value;
		instance->adapterNames.clear();
		instance->driverInfos.clear();
		instance->driverVersions.clear();
		if (!detail::try_reserve(instance->adapterNames, physicals.size()) || !detail::try_reserve(instance->driverInfos, physicals.size()) ||
			!detail::try_reserve(instance->driverVersions, physicals.size()))
		{
			return fail(error, ErrorCode::eOutOfHostMemory, "Vulkan adapter name storage allocation failed");
		}

		for (const vk::PhysicalDevice & phys : physicals)
		{
			const auto chain = phys.getProperties2<vk::PhysicalDeviceProperties2, vk::PhysicalDeviceDriverProperties>(instance->dispatch);
			const vk::PhysicalDeviceProperties & props = chain.get<vk::PhysicalDeviceProperties2>().properties;
			const auto & driverProps				   = chain.get<vk::PhysicalDeviceDriverProperties>();

			if (!detail::try_push_back(instance->adapterNames, props.deviceName.data()) ||
				!detail::try_push_back(instance->driverInfos, driverProps.driverInfo.data()) ||
				!detail::try_push_back(instance->driverVersions, format_vulkan_driver_version(map_driver_id(driverProps.driverID), props.driverVersion)))
			{
				return fail(error, ErrorCode::eOutOfHostMemory, "Vulkan adapter name storage allocation failed");
			}
		}

		std::uint32_t usable = 0;
		for (std::uint32_t i = 0; i < physicals.size(); ++i)
		{
			// NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
			if (vulkan_adapter_refusal(physicals[i], instance->dispatch) != nullptr)
			{
				continue;
			}

			const std::uint32_t slot = usable;
			++usable;
			if (slot >= adapters.size())
			{
				continue;
			}

			const auto chain = physicals[i].getProperties2<vk::PhysicalDeviceProperties2, vk::PhysicalDeviceDriverProperties, vk::PhysicalDeviceIDProperties>(
				instance->dispatch
			);
			const vk::PhysicalDeviceProperties & props = chain.get<vk::PhysicalDeviceProperties2>().properties;
			const auto & driverProps				   = chain.get<vk::PhysicalDeviceDriverProperties>();
			adapters[slot]							   = AdapterInfo{
				.type					   = map_adapter_type(props.deviceType),
				.apiId					   = VulkanApi::kId,
				.adapterIndex			   = i,
				.vendorId				   = props.vendorID,
				.deviceId				   = props.deviceID,
				.unifiedMemoryArchitecture = props.deviceType == vk::PhysicalDeviceType::eIntegratedGpu || props.deviceType == vk::PhysicalDeviceType::eCpu,
				.name					   = instance->adapterNames[i].c_str(),
				.driverId				   = map_driver_id(driverProps.driverID),
				.driverVersionRaw		   = props.driverVersion,
				.driverVersion			   = instance->driverVersions[i].c_str(),
				.driverInfo				   = instance->driverInfos[i].c_str(),
			};
			fill_adapter_identity(adapters[slot], chain.get<vk::PhysicalDeviceIDProperties>());
			// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
		}

		return store(out, usable, error);
	}

	std::optional<vk::ExternalMemoryHandleTypeFlagBits> map_memory_handle_type(const ExternalHandleType type) noexcept
	{
		switch (type)
		{
		case ExternalHandleType::eOpaqueFd:			return vk::ExternalMemoryHandleTypeFlagBits::eOpaqueFd;
		case ExternalHandleType::eOpaqueWin32:		return vk::ExternalMemoryHandleTypeFlagBits::eOpaqueWin32;
		case ExternalHandleType::eOpaqueWin32Kmt:	return vk::ExternalMemoryHandleTypeFlagBits::eOpaqueWin32Kmt;
		case ExternalHandleType::eD3D12Resource:	return vk::ExternalMemoryHandleTypeFlagBits::eD3D12Resource;
		case ExternalHandleType::eD3D12Heap:		return vk::ExternalMemoryHandleTypeFlagBits::eD3D12Heap;
		case ExternalHandleType::eDmaBuf:			return vk::ExternalMemoryHandleTypeFlagBits::eDmaBufEXT;
		case ExternalHandleType::eD3D12Fence:
		case ExternalHandleType::eMtlSharedEvent:
		case ExternalHandleType::eMtlSharedTexture: break;
		}

		return std::nullopt;
	}

	std::optional<vk::ExternalSemaphoreHandleTypeFlagBits> map_semaphore_handle_type(const ExternalHandleType type) noexcept
	{
		switch (type)
		{
		case ExternalHandleType::eOpaqueFd:			return vk::ExternalSemaphoreHandleTypeFlagBits::eOpaqueFd;
		case ExternalHandleType::eOpaqueWin32:		return vk::ExternalSemaphoreHandleTypeFlagBits::eOpaqueWin32;
		case ExternalHandleType::eOpaqueWin32Kmt:	return vk::ExternalSemaphoreHandleTypeFlagBits::eOpaqueWin32Kmt;
		case ExternalHandleType::eD3D12Fence:		return vk::ExternalSemaphoreHandleTypeFlagBits::eD3D12Fence;
		case ExternalHandleType::eD3D12Resource:
		case ExternalHandleType::eD3D12Heap:
		case ExternalHandleType::eDmaBuf:
		case ExternalHandleType::eMtlSharedEvent:
		case ExternalHandleType::eMtlSharedTexture: break;
		}

		return std::nullopt;
	}

	vk::ExternalMemoryHandleTypeFlags map_memory_handle_types(const Flags<ExternalHandleType> types) noexcept
	{
		vk::ExternalMemoryHandleTypeFlags out{};
		for (const ExternalHandleType type : kAllExternalHandleTypes)
		{
			if (const std::optional<vk::ExternalMemoryHandleTypeFlagBits> bit = map_memory_handle_type(type); bit && types.contains(type))
			{
				out |= *bit;
			}
		}

		return out;
	}

	vk::ExternalSemaphoreHandleTypeFlags map_semaphore_handle_types(const Flags<ExternalHandleType> types) noexcept
	{
		vk::ExternalSemaphoreHandleTypeFlags out{};
		for (const ExternalHandleType type : kAllExternalHandleTypes)
		{
			if (const std::optional<vk::ExternalSemaphoreHandleTypeFlagBits> bit = map_semaphore_handle_type(type); bit && types.contains(type))
			{
				out |= *bit;
			}
		}

		return out;
	}

	namespace
	{
		[[nodiscard]] Flags<ExternalHandleType> map_memory_handle_mask(const vk::ExternalMemoryHandleTypeFlags mask) noexcept
		{
			Flags<ExternalHandleType> out;
			for (const ExternalHandleType type : kAllExternalHandleTypes)
			{
				if (const std::optional<vk::ExternalMemoryHandleTypeFlagBits> bit = map_memory_handle_type(type); bit && (mask & *bit))
				{
					out |= type;
				}
			}

			return out;
		}

		[[nodiscard]] Flags<ExternalHandleType> map_semaphore_handle_mask(const vk::ExternalSemaphoreHandleTypeFlags mask) noexcept
		{
			Flags<ExternalHandleType> out;
			for (const ExternalHandleType type : kAllExternalHandleTypes)
			{
				if (const std::optional<vk::ExternalSemaphoreHandleTypeFlagBits> bit = map_semaphore_handle_type(type); bit && (mask & *bit))
				{
					out |= type;
				}
			}

			return out;
		}

		void fill_from_memory_properties(ExternalHandleSupport & out, const vk::ExternalMemoryProperties & props) noexcept
		{
			out.exportable		= static_cast<bool>(props.externalMemoryFeatures & vk::ExternalMemoryFeatureFlagBits::eExportable);
			out.importable		= static_cast<bool>(props.externalMemoryFeatures & vk::ExternalMemoryFeatureFlagBits::eImportable);
			out.compatibleTypes = map_memory_handle_mask(props.compatibleHandleTypes);
		}
	}

	ExternalHandleSupport vulkan_external_support_of(
		vk::PhysicalDevice phys,
		const vk::detail::DispatchLoaderDynamic & dispatch,
		const ExternalHandleSupportDesc & desc,
		const vk::BufferUsageFlags bufferUsage
	) noexcept
	{
		ExternalHandleSupport support{};

		switch (desc.kind)
		{
		case ExternalObjectKind::eHeap:
		case ExternalObjectKind::eBuffer:
		{
			const std::optional<vk::ExternalMemoryHandleTypeFlagBits> handleType = map_memory_handle_type(desc.handleType);
			if (!handleType)
			{
				break;
			}

			vk::PhysicalDeviceExternalBufferInfo info;
			info.handleType = *handleType;

			info.usage = bufferUsage;
			fill_from_memory_properties(support, phys.getExternalBufferProperties(info, dispatch).externalMemoryProperties);
			break;
		}

		case ExternalObjectKind::eTexture:
		{
			const std::optional<vk::ExternalMemoryHandleTypeFlagBits> handleType = map_memory_handle_type(desc.handleType);
			if (!handleType || desc.format == Format::eUndefined)
			{
				break;
			}

			vk::PhysicalDeviceImageFormatInfo2 formatInfo;
			formatInfo.format = map_format(desc.format);
			formatInfo.type	  = vk::ImageType::e2D;
			formatInfo.tiling = vk::ImageTiling::eOptimal;
			formatInfo.usage  = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst;

			vk::PhysicalDeviceExternalImageFormatInfo externalInfo;
			externalInfo.handleType = *handleType;
			formatInfo.pNext		= &externalInfo;

			vk::ImageFormatProperties2 properties;
			vk::ExternalImageFormatProperties externalProperties;
			properties.pNext = &externalProperties;

			if (phys.getImageFormatProperties2(&formatInfo, &properties, dispatch) != vk::Result::eSuccess)
			{
				break;
			}

			fill_from_memory_properties(support, externalProperties.externalMemoryProperties);
			break;
		}

		case ExternalObjectKind::eTimeline:
		case ExternalObjectKind::eBinarySemaphore:
		{
			const std::optional<vk::ExternalSemaphoreHandleTypeFlagBits> handleType = map_semaphore_handle_type(desc.handleType);
			if (!handleType)
			{
				break;
			}

			vk::PhysicalDeviceExternalSemaphoreInfo info;
			info.handleType = *handleType;

			vk::SemaphoreTypeCreateInfo semaphoreType;
			semaphoreType.semaphoreType = desc.kind == ExternalObjectKind::eTimeline ? vk::SemaphoreType::eTimeline : vk::SemaphoreType::eBinary;
			info.pNext					= &semaphoreType;

			const vk::ExternalSemaphoreProperties props = phys.getExternalSemaphoreProperties(info, dispatch);
			support.exportable		= static_cast<bool>(props.externalSemaphoreFeatures & vk::ExternalSemaphoreFeatureFlagBits::eExportable);
			support.importable		= static_cast<bool>(props.externalSemaphoreFeatures & vk::ExternalSemaphoreFeatureFlagBits::eImportable);
			support.compatibleTypes = map_semaphore_handle_mask(props.compatibleHandleTypes);
			break;
		}
		}

		return support;
	}

	bool vulkan_query_external_handle_support(void * impl, const ExternalHandleSupportDesc & desc, ExternalHandleSupport * out, Error * error) noexcept
	{
		if (out == nullptr)
		{
			return fail(error, ErrorCode::eInvalidArgument, "external handle support needs somewhere to write the result");
		}

		*out			= {};
		auto * instance = static_cast<VulkanInstance *>(impl);

		const auto enumerated = instance->instance.enumeratePhysicalDevices<HostAllocatorAdapter<vk::PhysicalDevice>>(instance->dispatch);
		if (enumerated.result != vk::Result::eSuccess)
		{
			return fail_native(error, "Vulkan adapter enumeration failed", enumerated.result);
		}
		if (desc.adapterIndex >= enumerated.value.size())
		{
			return fail(error, ErrorCode::eInvalidArgument, "external handle support asked about an adapter index this instance does not have");
		}

		// NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
		*out = vulkan_external_support_of(enumerated.value[desc.adapterIndex], instance->dispatch, desc, kExternalQueryBufferUsage);
		// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
		return succeed(error);
	}

	bool vulkan_refuse_unexportable(
		const VulkanDevice * device,
		const Flags<ExternalHandleType> declared,
		const ExternalObjectKind kind,
		const Format format,
		const vk::BufferUsageFlags bufferUsage,
		const char * what,
		Error * error
	) noexcept
	{
		if (declared.empty())
		{
			return true;
		}

		for (const ExternalHandleType type : kAllExternalHandleTypes)
		{
			if (!declared.contains(type))
			{
				continue;
			}

			const ExternalHandleSupportDesc query{ .adapterIndex = 0, .kind = kind, .handleType = type, .format = format };
			if (!vulkan_external_support_of(device->phys, device->dispatch, query, bufferUsage).exportable)
			{
				return fail(error, ErrorCode::eUnsupportedFeature, what);
			}
		}

		return true;
	}

	void * vulkan_instance_create_device(void * impl, const DeviceDesc & desc, Error * error) noexcept
	{
		void * out = make_owned_device(static_cast<VulkanInstance *>(impl), desc, error);
		if (out != nullptr && error != nullptr)
		{
			*error = {};
		}

		return out;
	}

	void * vulkan_create_instance(const void * instanceDesc, Error * error) noexcept
	{
		void * out = make_owned_instance(*static_cast<const InstanceDesc *>(instanceDesc), error);
		if (out != nullptr && error != nullptr)
		{
			*error = {};
		}

		return out;
	}

}

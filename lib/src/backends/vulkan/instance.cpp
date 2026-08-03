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

namespace azo::rhi::vulkan
{
	// Instance entries.
	GraphicsApiId VulkanInstanceApiId([[maybe_unused]] void * impl) noexcept
	{
		return VulkanApi::id;
	}

	bool VulkanEnumerateAdapters(void * impl, std::span<AdapterInfo> adapters, std::uint32_t * out, Error * error) noexcept
	{
		if (out == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidArgument, "adapter count output pointer is null");
		}

		*out			= 0;
		auto * instance = static_cast<VulkanInstance *>(impl);

		const auto enumerated = instance->instance.enumeratePhysicalDevices<HostAllocatorAdapter<vk::PhysicalDevice>>(instance->dispatch);
		if (enumerated.result != vk::Result::eSuccess)
		{
			return Fail(error, ErrorCode::eNativeApiError, "Vulkan adapter enumeration failed");
		}

		const detail::HostVector<vk::PhysicalDevice> & physicals = enumerated.value;
		instance->adapterNames.clear();
		instance->driverInfos.clear();
		instance->driverVersions.clear();
		if (!detail::TryReserve(instance->adapterNames, physicals.size()) || !detail::TryReserve(instance->driverInfos, physicals.size()) ||
			!detail::TryReserve(instance->driverVersions, physicals.size()))
		{
			return Fail(error, ErrorCode::eOutOfHostMemory, "Vulkan adapter name storage allocation failed");
		}

		for (const vk::PhysicalDevice & phys : physicals)
		{
			const auto chain = phys.getProperties2<vk::PhysicalDeviceProperties2, vk::PhysicalDeviceDriverProperties>(instance->dispatch);
			const vk::PhysicalDeviceProperties & props			   = chain.get<vk::PhysicalDeviceProperties2>().properties;
			const vk::PhysicalDeviceDriverProperties & driverProps = chain.get<vk::PhysicalDeviceDriverProperties>();

			// The three grow together and every adapter below is read out of all three by the same index, so one refusal fails the call without leaving the shorter
			// vector to be indexed with the longer one's count.
			if (!detail::TryPushBack(instance->adapterNames, props.deviceName.data()) ||
				!detail::TryPushBack(instance->driverInfos, driverProps.driverInfo.data()) ||
				!detail::TryPushBack(instance->driverVersions, FormatVulkanDriverVersion(MapDriverId(driverProps.driverID), props.driverVersion)))
			{
				return Fail(error, ErrorCode::eOutOfHostMemory, "Vulkan adapter name storage allocation failed");
			}
		}

		const auto fillCount = static_cast<std::uint32_t>(adapters.size() < physicals.size() ? adapters.size() : physicals.size());
		for (std::uint32_t i = 0; i < fillCount; ++i)
		{
			const auto chain =
				// The three lists grow together or the call fails, and the count is the smaller of the two.
				// NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
				physicals[i].getProperties2<vk::PhysicalDeviceProperties2, vk::PhysicalDeviceDriverProperties, vk::PhysicalDeviceIDProperties>(
					instance->dispatch);
			const vk::PhysicalDeviceProperties & props			   = chain.get<vk::PhysicalDeviceProperties2>().properties;
			const vk::PhysicalDeviceDriverProperties & driverProps = chain.get<vk::PhysicalDeviceDriverProperties>();
			adapters[i]											   = AdapterInfo{ .type = MapAdapterType(props.deviceType),
				.apiId						 = VulkanApi::id,
				.adapterIndex				 = i,
				.vendorId					 = props.vendorID,
				.deviceId					 = props.deviceID,
				.unifiedMemoryArchitecture	 = props.deviceType == vk::PhysicalDeviceType::eIntegratedGpu || props.deviceType == vk::PhysicalDeviceType::eCpu,
				.name						 = instance->adapterNames[i].c_str(),
				.driverId					 = MapDriverId(driverProps.driverID),
				.driverVersionRaw			 = props.driverVersion,
				.driverVersion				 = instance->driverVersions[i].c_str(),
				.driverInfo					 = instance->driverInfos[i].c_str() };
				// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
			FillAdapterIdentity(adapters[i], chain.get<vk::PhysicalDeviceIDProperties>());
		}

		return Store(out, static_cast<std::uint32_t>(physicals.size()), error);
	}

	std::optional<vk::ExternalMemoryHandleTypeFlagBits> MapMemoryHandleType(const ExternalHandleType type) noexcept
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

	std::optional<vk::ExternalSemaphoreHandleTypeFlagBits> MapSemaphoreHandleType(const ExternalHandleType type) noexcept
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

	// The declared set as Vulkan spells it, for the create infos an exportable object is built with. A type with no memory or semaphore counterpart drops out,
	// which creation has already refused by the time either of these runs.
	vk::ExternalMemoryHandleTypeFlags MapMemoryHandleTypes(const Flags<ExternalHandleType> types) noexcept
	{
		vk::ExternalMemoryHandleTypeFlags out{};
		for (const ExternalHandleType type : kAllExternalHandleTypes)
		{
			if (const std::optional<vk::ExternalMemoryHandleTypeFlagBits> bit = MapMemoryHandleType(type); bit && types.Contains(type))
			{
				out |= *bit;
			}
		}

		return out;
	}

	vk::ExternalSemaphoreHandleTypeFlags MapSemaphoreHandleTypes(const Flags<ExternalHandleType> types) noexcept
	{
		vk::ExternalSemaphoreHandleTypeFlags out{};
		for (const ExternalHandleType type : kAllExternalHandleTypes)
		{
			if (const std::optional<vk::ExternalSemaphoreHandleTypeFlagBits> bit = MapSemaphoreHandleType(type); bit && types.Contains(type))
			{
				out |= *bit;
			}
		}

		return out;
	}

	namespace
	{
		// The mask back in RHI terms. Only the types the RHI names survive, so a driver reporting one it has no name for drops out without being reported under a
		// neighbouring name.
		[[nodiscard]] Flags<ExternalHandleType> MapMemoryHandleMask(const vk::ExternalMemoryHandleTypeFlags mask) noexcept
		{
			Flags<ExternalHandleType> out;
			for (const ExternalHandleType type : kAllExternalHandleTypes)
			{
				if (const std::optional<vk::ExternalMemoryHandleTypeFlagBits> bit = MapMemoryHandleType(type); bit && (mask & *bit))
				{
					out |= type;
				}
			}

			return out;
		}

		[[nodiscard]] Flags<ExternalHandleType> MapSemaphoreHandleMask(const vk::ExternalSemaphoreHandleTypeFlags mask) noexcept
		{
			Flags<ExternalHandleType> out;
			for (const ExternalHandleType type : kAllExternalHandleTypes)
			{
				if (const std::optional<vk::ExternalSemaphoreHandleTypeFlagBits> bit = MapSemaphoreHandleType(type); bit && (mask & *bit))
				{
					out |= type;
				}
			}

			return out;
		}

		void FillFromMemoryProperties(ExternalHandleSupport & out, const vk::ExternalMemoryProperties & props) noexcept
		{
			out.exportable		= static_cast<bool>(props.externalMemoryFeatures & vk::ExternalMemoryFeatureFlagBits::eExportable);
			out.importable		= static_cast<bool>(props.externalMemoryFeatures & vk::ExternalMemoryFeatureFlagBits::eImportable);
			out.compatibleTypes = MapMemoryHandleMask(props.compatibleHandleTypes);
		}
	} // namespace

	/*
	 * What this adapter will share, asked of the driver, not inferred from the extension list and taken from the physical device and not an instance. Buffers and
	 * semaphores each have their own physical device query.
	 *
	 * Textures have neither. Vulkan resolves external image support through the image format query with the external handle type chained in, which is why the
	 * request carries a format for that kind alone.
	 */
	ExternalHandleSupport VulkanExternalSupportOf(vk::PhysicalDevice phys, const vk::detail::DispatchLoaderDynamic & dispatch,
		const ExternalHandleSupportDesc & desc, const vk::BufferUsageFlags bufferUsage) noexcept
	{
		ExternalHandleSupport support{};

		switch (desc.kind)
		{
		/*
		 * A heap answers from the buffer query, which is the narrowest honest answer and not a stand-in. Vulkan has no capability query for a bare allocation. It has
		 * one for a buffer and one for an image format, where a heap is memory that resources are placed in, not either. The buffer query is the closer of the two.
		 */
		case ExternalObjectKind::eHeap:
		case ExternalObjectKind::eBuffer:
		{
			const std::optional<vk::ExternalMemoryHandleTypeFlagBits> handleType = MapMemoryHandleType(desc.handleType);
			if (!handleType)
			{
				// A handle type that carries no memory. Not an error to ask, and the answer is no.
				break;
			}

			vk::PhysicalDeviceExternalBufferInfo info;
			info.handleType = *handleType;

			info.usage = bufferUsage;
			FillFromMemoryProperties(support, phys.getExternalBufferProperties(info, dispatch).externalMemoryProperties);
			break;
		}

		case ExternalObjectKind::eTexture:
		{
			const std::optional<vk::ExternalMemoryHandleTypeFlagBits> handleType = MapMemoryHandleType(desc.handleType);
			if (!handleType || desc.format == Format::eUndefined)
			{
				break;
			}

			vk::PhysicalDeviceImageFormatInfo2 formatInfo;
			formatInfo.format = MapFormat(desc.format);
			formatInfo.type	  = vk::ImageType::e2D;
			formatInfo.tiling = vk::ImageTiling::eOptimal;
			formatInfo.usage  = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst;

			vk::PhysicalDeviceExternalImageFormatInfo externalInfo;
			externalInfo.handleType = *handleType;
			formatInfo.pNext		= &externalInfo;

			vk::ImageFormatProperties2 properties;
			vk::ExternalImageFormatProperties externalProperties;
			properties.pNext = &externalProperties;

			// A format this adapter cannot use externally answers eErrorFormatNotSupported, which is a no and not a failure of the query.
			if (phys.getImageFormatProperties2(&formatInfo, &properties, dispatch) != vk::Result::eSuccess)
			{
				break;
			}

			FillFromMemoryProperties(support, externalProperties.externalMemoryProperties);
			break;
		}

		case ExternalObjectKind::eTimeline:
		case ExternalObjectKind::eBinarySemaphore:
		{
			const std::optional<vk::ExternalSemaphoreHandleTypeFlagBits> handleType = MapSemaphoreHandleType(desc.handleType);
			if (!handleType)
			{
				break;
			}

			vk::PhysicalDeviceExternalSemaphoreInfo info;
			info.handleType = *handleType;

			// The axis Vulkan varies, and the reason the two semaphore kinds are separate entries here and not one.
			vk::SemaphoreTypeCreateInfo semaphoreType;
			semaphoreType.semaphoreType = desc.kind == ExternalObjectKind::eTimeline ? vk::SemaphoreType::eTimeline : vk::SemaphoreType::eBinary;
			info.pNext					= &semaphoreType;

			const vk::ExternalSemaphoreProperties props = phys.getExternalSemaphoreProperties(info, dispatch);
			support.exportable		= static_cast<bool>(props.externalSemaphoreFeatures & vk::ExternalSemaphoreFeatureFlagBits::eExportable);
			support.importable		= static_cast<bool>(props.externalSemaphoreFeatures & vk::ExternalSemaphoreFeatureFlagBits::eImportable);
			support.compatibleTypes = MapSemaphoreHandleMask(props.compatibleHandleTypes);
			break;
		}
		}

		return support;
	}

	bool VulkanQueryExternalHandleSupport(void * impl, const ExternalHandleSupportDesc & desc, ExternalHandleSupport * out, Error * error) noexcept
	{
		if (out == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidArgument, "external handle support needs somewhere to write the result");
		}

		*out			= {};
		auto * instance = static_cast<VulkanInstance *>(impl);

		const auto enumerated = instance->instance.enumeratePhysicalDevices<HostAllocatorAdapter<vk::PhysicalDevice>>(instance->dispatch);
		if (enumerated.result != vk::Result::eSuccess)
		{
			return Fail(error, ErrorCode::eNativeApiError, "Vulkan adapter enumeration failed");
		}
		if (desc.adapterIndex >= enumerated.value.size())
		{
			return Fail(error, ErrorCode::eInvalidArgument, "external handle support asked about an adapter index this instance does not have");
		}

		// The three lists grow together or the call fails, and the count is the smaller of the two.
		// NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
		*out = VulkanExternalSupportOf(enumerated.value[desc.adapterIndex], instance->dispatch, desc, kExternalQueryBufferUsage);
		// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
		return Succeed(error);
	}

	/*
	 * Refuses an export declaration this adapter cannot honor, at creation, which is the last point Vulkan can still say no.
	 *
	 * Every declared type is checked, not any one of them, because a caller naming two has to get both: an object exportable to one of the pair and not the other
	 * would satisfy neither of the two programs the declaration exists to let it feed.
	 */
	bool VulkanRefuseUnexportable(const VulkanDevice * device, const Flags<ExternalHandleType> declared, const ExternalObjectKind kind, const Format format,
		const vk::BufferUsageFlags bufferUsage, const char * what, Error * error) noexcept
	{
		if (declared.Empty())
		{
			return true;
		}

		for (const ExternalHandleType type : kAllExternalHandleTypes)
		{
			if (!declared.Contains(type))
			{
				continue;
			}

			const ExternalHandleSupportDesc query{ .adapterIndex = 0, .kind = kind, .handleType = type, .format = format };
			if (!VulkanExternalSupportOf(device->phys, device->dispatch, query, bufferUsage).exportable)
			{
				return Fail(error, ErrorCode::eUnsupportedFeature, what);
			}
		}

		return true;
	}

	void * VulkanInstanceCreateDevice(void * impl, const DeviceDesc & desc, Error * error) noexcept
	{
		void * out = MakeOwnedDevice(static_cast<VulkanInstance *>(impl), desc, error);
		if (out != nullptr && error != nullptr)
		{
			*error = {};
		}

		return out;
	}

	void * VulkanCreateInstance(const void * instanceDesc, Error * error) noexcept
	{
		void * out = MakeOwnedInstance(*static_cast<const InstanceDesc *>(instanceDesc), error);
		if (out != nullptr && error != nullptr)
		{
			*error = {};
		}

		return out;
	}

} // namespace azo::rhi::vulkan

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

#pragma once

/**
 * \file
 * \brief Backend dispatch-table validation and device block resolution.
 */

#include "azoth/rhi/backend/dispatch.hpp"
#include "azoth/rhi/core/result.hpp"
#include "azoth/rhi/host/allocator.hpp"

#include <array>
#include <bit>
#include <cstddef>
// ReSharper disable once CppUnusedIncludeDirective
#include <cstdint>
#include <cstring>
#include <string_view>

namespace azo::rhi::validation
{
	[[nodiscard]] void * WrapDevice(void * deviceImpl, ValidationMode mode) noexcept;

} // namespace azo::rhi::validation

namespace azo::rhi::detail
{
	/**
	 * \brief ABI dispatch entry word used to sweep every block as a flat run of function pointers.
	 */
	using AnyDispatchEntry = void (*)();

	/**
	 * \brief Number of dispatch-entry words occupied by InterfaceHeader before function entries begin.
	 */
	inline constexpr std::size_t kBlockHeaderWords = sizeof(InterfaceHeader) / sizeof(AnyDispatchEntry);

	static_assert(sizeof(InterfaceHeader) == sizeof(AnyDispatchEntry),
		"The sweep reads a block as a flat run of words with the header occupying whole ones. A header that is not a word wide would leave the entries "
		"unaligned to that run.");

	template <typename Block>
	struct BlockEntries;

	template <>
	struct BlockEntries<InstanceApi> final
	{
		static constexpr std::array<std::string_view, 4> kNames{
			"InstanceApi::getGraphicsApiId is null",
			"InstanceApi::enumerateAdapters is null",
			"InstanceApi::createDevice is null",
			"InstanceApi::destroyInstance is null",
		};

		static constexpr std::string_view kBlockMissing{ "the backend instance publishes no InstanceApi, or a shorter one than this build reads" };

		static_assert(sizeof(InstanceApi) == (kBlockHeaderWords + kNames.size()) * sizeof(AnyDispatchEntry),
			"An entry was added to or removed from InstanceApi without updating this list. The sweep reads a block as a flat run of function "
			"pointers, so the two have to stay the same length.");
	};

	template <>
	struct BlockEntries<CoreDeviceApi> final
	{
		static constexpr std::array<std::string_view, 30> kNames{
			"CoreDeviceApi::getGraphicsApiId is null",
			"CoreDeviceApi::getGraphicsApiName is null",
			"CoreDeviceApi::createBuffer is null",
			"CoreDeviceApi::createTexture is null",
			"CoreDeviceApi::createTextureView is null",
			"CoreDeviceApi::createSampler is null",
			"CoreDeviceApi::createDescriptorSetLayout is null",
			"CoreDeviceApi::createPipelineLayout is null",
			"CoreDeviceApi::createGraphicsPipeline is null",
			"CoreDeviceApi::createComputePipeline is null",
			"CoreDeviceApi::createTimeline is null",
			"CoreDeviceApi::createBinarySemaphore is null",
			"CoreDeviceApi::createDescriptorArena is null",
			"CoreDeviceApi::createCommandPool is null",
			"CoreDeviceApi::getQueue is null",
			"CoreDeviceApi::map is null",
			"CoreDeviceApi::unmap is null",
			"CoreDeviceApi::flushMappedRange is null",
			"CoreDeviceApi::invalidateMappedRange is null",
			"CoreDeviceApi::updateDescriptorsBuffer is null",
			"CoreDeviceApi::updateDescriptorsTexture is null",
			"CoreDeviceApi::updateDescriptorsSampler is null",
			"CoreDeviceApi::getCaps is null",
			"CoreDeviceApi::getFormatSupport is null",
			"CoreDeviceApi::getAdapterInfo is null",
			"CoreDeviceApi::getValidationMessageCounts is null",
			"CoreDeviceApi::destroy is null",
			"CoreDeviceApi::collectGarbage is null",
			"CoreDeviceApi::collectGarbageTimeline is null",
			"CoreDeviceApi::destroyDevice is null",
		};

		static constexpr std::string_view kBlockMissing{ "the backend device publishes no CoreDeviceApi, or a shorter one than this build reads" };

		static_assert(sizeof(CoreDeviceApi) == (kBlockHeaderWords + kNames.size()) * sizeof(AnyDispatchEntry),
			"An entry was added to or removed from CoreDeviceApi without updating this list. The sweep reads a block as a flat run of function "
			"pointers, so the two have to stay the same length.");
	};

	template <>
	struct BlockEntries<PresentApi> final
	{
		static constexpr std::array<std::string_view, 1> kNames{ "PresentApi::createSwapchain is null" };

		static constexpr std::string_view kBlockMissing{ "the backend object publishes no PresentApi, or a shorter one than this build reads" };

		static_assert(sizeof(PresentApi) == (kBlockHeaderWords + kNames.size()) * sizeof(AnyDispatchEntry),
			"An entry was added to or removed from PresentApi without updating this list. The sweep reads a block as a flat run of function "
			"pointers, so the two have to stay the same length.");
	};

	template <>
	struct BlockEntries<PlacedMemoryApi> final
	{
		static constexpr std::array<std::string_view, 5> kNames{
			"PlacedMemoryApi::createHeap is null",
			"PlacedMemoryApi::createPlacedBuffer is null",
			"PlacedMemoryApi::createPlacedTexture is null",
			"PlacedMemoryApi::getTextureMemoryInfo is null",
			"PlacedMemoryApi::getBufferMemoryInfo is null",
		};

		static constexpr std::string_view kBlockMissing{ "the backend object publishes no PlacedMemoryApi, or a shorter one than this build reads" };

		static_assert(sizeof(PlacedMemoryApi) == (kBlockHeaderWords + kNames.size()) * sizeof(AnyDispatchEntry),
			"An entry was added to or removed from PlacedMemoryApi without updating this list. The sweep reads a block as a flat run of function "
			"pointers, so the two have to stay the same length.");
	};

	template <>
	struct BlockEntries<RayTracingApi> final
	{
		static constexpr std::array<std::string_view, 3> kNames{
			"RayTracingApi::createRayTracingPipeline is null",
			"RayTracingApi::createAccelerationStructure is null",
			"RayTracingApi::updateDescriptorsAccelerationStructure is null",
		};

		static constexpr std::string_view kBlockMissing{ "the backend object publishes no RayTracingApi, or a shorter one than this build reads" };

		static_assert(sizeof(RayTracingApi) == (kBlockHeaderWords + kNames.size()) * sizeof(AnyDispatchEntry),
			"An entry was added to or removed from RayTracingApi without updating this list. The sweep reads a block as a flat run of function "
			"pointers, so the two have to stay the same length.");
	};

	template <>
	struct BlockEntries<QueryApi> final
	{
		static constexpr std::array<std::string_view, 2> kNames{
			"QueryApi::createQueryPool is null",
			"QueryApi::calibrateTimestamp is null",
		};

		static constexpr std::string_view kBlockMissing{ "the backend object publishes no QueryApi, or a shorter one than this build reads" };

		static_assert(sizeof(QueryApi) == (kBlockHeaderWords + kNames.size()) * sizeof(AnyDispatchEntry),
			"An entry was added to or removed from QueryApi without updating this list. The sweep reads a block as a flat run of function "
			"pointers, so the two have to stay the same length.");
	};

	template <>
	struct BlockEntries<ExternalSharingApi> final
	{
		static constexpr std::array<std::string_view, 11> kNames{
			"ExternalSharingApi::exportBuffer is null",
			"ExternalSharingApi::exportHeap is null",
			"ExternalSharingApi::exportTexture is null",
			"ExternalSharingApi::exportTimeline is null",
			"ExternalSharingApi::exportBinarySemaphore is null",
			"ExternalSharingApi::importBuffer is null",
			"ExternalSharingApi::importHeap is null",
			"ExternalSharingApi::importTexture is null",
			"ExternalSharingApi::importTimeline is null",
			"ExternalSharingApi::importBinarySemaphore is null",
			"ExternalSharingApi::closeExportedHandle is null",
		};

		static constexpr std::string_view kBlockMissing{ "the backend object publishes no ExternalSharingApi, or a shorter one than this build reads" };

		static_assert(sizeof(ExternalSharingApi) == (kBlockHeaderWords + kNames.size()) * sizeof(AnyDispatchEntry),
			"An entry was added to or removed from ExternalSharingApi without updating this list. The sweep reads a block as a flat run of function "
			"pointers, so the two have to stay the same length.");
	};

	template <>
	struct BlockEntries<ExternalCapabilityApi> final
	{
		static constexpr std::array<std::string_view, 1> kNames{ "ExternalCapabilityApi::queryExternalHandleSupport is null" };

		static constexpr std::string_view kBlockMissing{ "the backend object publishes no ExternalCapabilityApi, or a shorter one than this build reads" };

		static_assert(sizeof(ExternalCapabilityApi) == (kBlockHeaderWords + kNames.size()) * sizeof(AnyDispatchEntry),
			"An entry was added to or removed from ExternalCapabilityApi without updating this list. The sweep reads a block as a flat run of function "
			"pointers, so the two have to stay the same length.");
	};

	template <>
	struct BlockEntries<PipelineCacheApi> final
	{
		static constexpr std::array<std::string_view, 2> kNames{
			"PipelineCacheApi::createPipelineCache is null",
			"PipelineCacheApi::getPipelineCacheData is null",
		};

		static constexpr std::string_view kBlockMissing{ "the backend object publishes no PipelineCacheApi, or a shorter one than this build reads" };

		static_assert(sizeof(PipelineCacheApi) == (kBlockHeaderWords + kNames.size()) * sizeof(AnyDispatchEntry),
			"An entry was added to or removed from PipelineCacheApi without updating this list. The sweep reads a block as a flat run of function "
			"pointers, so the two have to stay the same length.");
	};

	template <>
	struct BlockEntries<ResourceIntrospectionApi> final
	{
		static constexpr std::array<std::string_view, 2> kNames{ "ResourceIntrospectionApi::getTextureInfo is null",
			"ResourceIntrospectionApi::getBufferInfo is null" };

		static constexpr std::string_view kBlockMissing{ "the backend device publishes no ResourceIntrospectionApi, or a shorter one than this build reads" };

		static_assert(sizeof(ResourceIntrospectionApi) == (kBlockHeaderWords + kNames.size()) * sizeof(AnyDispatchEntry),
			"An entry was added to or removed from ResourceIntrospectionApi without updating this list. The sweep reads a block as a flat run of function "
			"pointers, so the two have to stay the same length.");
	};

	template <>
	struct BlockEntries<ResidencyApi> final
	{
		static constexpr std::array<std::string_view, 2> kNames{
			"ResidencyApi::queryMemoryBudget is null",
			"ResidencyApi::setResidencyPriority is null",
		};

		static constexpr std::string_view kBlockMissing{ "the backend object publishes no ResidencyApi, or a shorter one than this build reads" };

		static_assert(sizeof(ResidencyApi) == (kBlockHeaderWords + kNames.size()) * sizeof(AnyDispatchEntry),
			"An entry was added to or removed from ResidencyApi without updating this list. The sweep reads a block as a flat run of function "
			"pointers, so the two have to stay the same length.");
	};

	template <>
	struct BlockEntries<AdoptionApi> final
	{
		static constexpr std::array<std::string_view, 12> kNames{
			"AdoptionApi::adoptBuffer is null",
			"AdoptionApi::adoptTexture is null",
			"AdoptionApi::getNativeBuffer is null",
			"AdoptionApi::getNativeTexture is null",
			"AdoptionApi::adoptTextureView is null",
			"AdoptionApi::adoptSampler is null",
			"AdoptionApi::getNativeTextureView is null",
			"AdoptionApi::getNativeSampler is null",
			"AdoptionApi::adoptTimeline is null",
			"AdoptionApi::adoptBinarySemaphore is null",
			"AdoptionApi::getNativeTimeline is null",
			"AdoptionApi::getNativeBinarySemaphore is null",
		};

		static constexpr std::string_view kBlockMissing{ "the backend object publishes no AdoptionApi, or a shorter one than this build reads" };

		static_assert(sizeof(AdoptionApi) == (kBlockHeaderWords + kNames.size()) * sizeof(AnyDispatchEntry),
			"An entry was added to or removed from AdoptionApi without updating this list. The sweep reads a block as a flat run of function "
			"pointers, so the two have to stay the same length.");
	};

	template <>
	struct BlockEntries<QueueApi> final
	{
		static constexpr std::array<std::string_view, 9> kNames{
			"QueueApi::getType is null",
			"QueueApi::getFamilyIndex is null",
			"QueueApi::submit is null",
			"QueueApi::waitIdle is null",
			"QueueApi::getCompletedValue is null",
			"QueueApi::wait is null",
			"QueueApi::signal is null",
			"QueueApi::beginDebugLabel is null",
			"QueueApi::endDebugLabel is null",
		};

		static constexpr std::string_view kBlockMissing{ "the backend queue publishes no QueueApi, or a shorter one than this build reads" };

		static_assert(sizeof(QueueApi) == (kBlockHeaderWords + kNames.size()) * sizeof(AnyDispatchEntry),
			"An entry was added to or removed from QueueApi without updating this list. The sweep reads a block as a flat run of function "
			"pointers, so the two have to stay the same length.");
	};

	template <>
	struct BlockEntries<SparseApi> final
	{
		static constexpr std::array<std::string_view, 1> kNames{
			"SparseApi::bindSparse is null",
		};

		static constexpr std::string_view kBlockMissing{ "the backend object publishes no SparseApi, or a shorter one than this build reads" };

		static_assert(sizeof(SparseApi) == (kBlockHeaderWords + kNames.size()) * sizeof(AnyDispatchEntry),
			"An entry was added to or removed from SparseApi without updating this list. The sweep reads a block as a flat run of function "
			"pointers, so the two have to stay the same length.");
	};

	template <>
	struct BlockEntries<CommandPoolApi> final
	{
		static constexpr std::array<std::string_view, 2> kNames{
			"CommandPoolApi::allocate is null",
			"CommandPoolApi::reset is null",
		};

		static constexpr std::string_view kBlockMissing{ "the backend command pool publishes no CommandPoolApi, or a shorter one than this build reads" };

		static_assert(sizeof(CommandPoolApi) == (kBlockHeaderWords + kNames.size()) * sizeof(AnyDispatchEntry),
			"An entry was added to or removed from CommandPoolApi without updating this list. The sweep reads a block as a flat run of function "
			"pointers, so the two have to stay the same length.");
	};

	template <>
	struct BlockEntries<DescriptorArenaApi> final
	{
		static constexpr std::array<std::string_view, 2> kNames{
			"DescriptorArenaApi::allocate is null",
			"DescriptorArenaApi::reset is null",
		};

		static constexpr std::string_view kBlockMissing{
			"the backend descriptor arena publishes no DescriptorArenaApi, or a shorter one than this build reads"
		};

		static_assert(sizeof(DescriptorArenaApi) == (kBlockHeaderWords + kNames.size()) * sizeof(AnyDispatchEntry),
			"An entry was added to or removed from DescriptorArenaApi without updating this list. The sweep reads a block as a flat run of function "
			"pointers, so the two have to stay the same length.");
	};

	template <>
	struct BlockEntries<RenderCommandApi> final
	{
		static constexpr std::array<std::string_view, 30> kNames{
			"RenderCommandApi::begin is null",
			"RenderCommandApi::end is null",
			"RenderCommandApi::barriers is null",
			"RenderCommandApi::beginRendering is null",
			"RenderCommandApi::endRendering is null",
			"RenderCommandApi::setGraphicsPipeline is null",
			"RenderCommandApi::setComputePipeline is null",
			"RenderCommandApi::bindDescriptorSet is null",
			"RenderCommandApi::pushConstants is null",
			"RenderCommandApi::setViewport is null",
			"RenderCommandApi::setScissor is null",
			"RenderCommandApi::setBlendConstants is null",
			"RenderCommandApi::setStencilReference is null",
			"RenderCommandApi::setDepthBias is null",
			"RenderCommandApi::setVertexBuffer is null",
			"RenderCommandApi::setIndexBuffer is null",
			"RenderCommandApi::draw is null",
			"RenderCommandApi::drawIndexed is null",
			"RenderCommandApi::dispatch is null",
			"RenderCommandApi::copyBuffer is null",
			"RenderCommandApi::copyBufferToTexture is null",
			"RenderCommandApi::copyTextureToBuffer is null",
			"RenderCommandApi::copyTexture is null",
			"RenderCommandApi::clearBuffer is null",
			"RenderCommandApi::clearTexture is null",
			"RenderCommandApi::resolveTexture is null",
			"RenderCommandApi::blit is null",
			"RenderCommandApi::generateMips is null",
			"RenderCommandApi::beginDebugLabel is null",
			"RenderCommandApi::endDebugLabel is null",
		};

		static constexpr std::string_view kBlockMissing{ "the backend command list publishes no RenderCommandApi, or a shorter one than this build reads" };

		static_assert(sizeof(RenderCommandApi) == (kBlockHeaderWords + kNames.size()) * sizeof(AnyDispatchEntry),
			"An entry was added to or removed from RenderCommandApi without updating this list. The sweep reads a block as a flat run of function "
			"pointers, so the two have to stay the same length.");
	};

	template <>
	struct BlockEntries<AliasingCommandApi> final
	{
		static constexpr std::array<std::string_view, 1> kNames{
			"AliasingCommandApi::aliasBarriers is null",
		};

		static constexpr std::string_view kBlockMissing{ "the backend object publishes no AliasingCommandApi, or a shorter one than this build reads" };

		static_assert(sizeof(AliasingCommandApi) == (kBlockHeaderWords + kNames.size()) * sizeof(AnyDispatchEntry),
			"An entry was added to or removed from AliasingCommandApi without updating this list. The sweep reads a block as a flat run of function "
			"pointers, so the two have to stay the same length.");
	};

	template <>
	struct BlockEntries<RayTracingCommandApi> final
	{
		static constexpr std::array<std::string_view, 5> kNames{
			"RayTracingCommandApi::setRayTracingPipeline is null",
			"RayTracingCommandApi::buildAccelerationStructures is null",
			"RayTracingCommandApi::copyAccelerationStructure is null",
			"RayTracingCommandApi::compactAccelerationStructure is null",
			"RayTracingCommandApi::traceRays is null",
		};

		static constexpr std::string_view kBlockMissing{ "the backend object publishes no RayTracingCommandApi, or a shorter one than this build reads" };

		static_assert(sizeof(RayTracingCommandApi) == (kBlockHeaderWords + kNames.size()) * sizeof(AnyDispatchEntry),
			"An entry was added to or removed from RayTracingCommandApi without updating this list. The sweep reads a block as a flat run of function "
			"pointers, so the two have to stay the same length.");
	};

	template <>
	struct BlockEntries<QueryCommandApi> final
	{
		static constexpr std::array<std::string_view, 5> kNames{
			"QueryCommandApi::resetQueryPool is null",
			"QueryCommandApi::writeTimestamp is null",
			"QueryCommandApi::beginQuery is null",
			"QueryCommandApi::endQuery is null",
			"QueryCommandApi::resolveQueryData is null",
		};

		static constexpr std::string_view kBlockMissing{ "the backend object publishes no QueryCommandApi, or a shorter one than this build reads" };

		static_assert(sizeof(QueryCommandApi) == (kBlockHeaderWords + kNames.size()) * sizeof(AnyDispatchEntry),
			"An entry was added to or removed from QueryCommandApi without updating this list. The sweep reads a block as a flat run of function "
			"pointers, so the two have to stay the same length.");
	};

	template <>
	struct BlockEntries<IndirectApi> final
	{
		static constexpr std::array<std::string_view, 3> kNames{
			"IndirectApi::drawIndirect is null",
			"IndirectApi::drawIndexedIndirect is null",
			"IndirectApi::dispatchIndirect is null",
		};

		static constexpr std::string_view kBlockMissing{ "the backend object publishes no IndirectApi, or a shorter one than this build reads" };

		static_assert(sizeof(IndirectApi) == (kBlockHeaderWords + kNames.size()) * sizeof(AnyDispatchEntry),
			"An entry was added to or removed from IndirectApi without updating this list. The sweep reads a block as a flat run of function "
			"pointers, so the two have to stay the same length.");
	};

	template <>
	struct BlockEntries<IndirectCountApi> final
	{
		static constexpr std::array<std::string_view, 2> kNames{
			"IndirectCountApi::drawIndirectCount is null",
			"IndirectCountApi::drawIndexedIndirectCount is null",
		};

		static constexpr std::string_view kBlockMissing{ "the backend object publishes no IndirectCountApi, or a shorter one than this build reads" };

		static_assert(sizeof(IndirectCountApi) == (kBlockHeaderWords + kNames.size()) * sizeof(AnyDispatchEntry),
			"An entry was added to or removed from IndirectCountApi without updating this list. The sweep reads a block as a flat run of function "
			"pointers, so the two have to stay the same length.");
	};

	template <>
	struct BlockEntries<NativeEscapeApi> final
	{
		static constexpr std::array<std::string_view, 2> kNames{
			"NativeEscapeApi::beginNativeMutation is null",
			"NativeEscapeApi::endNativeMutation is null",
		};

		static constexpr std::string_view kBlockMissing{ "the backend object publishes no NativeEscapeApi, or a shorter one than this build reads" };

		static_assert(sizeof(NativeEscapeApi) == (kBlockHeaderWords + kNames.size()) * sizeof(AnyDispatchEntry),
			"An entry was added to or removed from NativeEscapeApi without updating this list. The sweep reads a block as a flat run of function "
			"pointers, so the two have to stay the same length.");
	};

	template <>
	struct BlockEntries<SwapchainApi> final
	{
		static constexpr std::array<std::string_view, 13> kNames{
			"SwapchainApi::acquireNextImage is null",
			"SwapchainApi::present is null",
			"SwapchainApi::getBackBuffer is null",
			"SwapchainApi::getBackBufferView is null",
			"SwapchainApi::getPerImagePresentSemaphore is null",
			"SwapchainApi::getFormat is null",
			"SwapchainApi::getPresentMode is null",
			"SwapchainApi::getImageCount is null",
			"SwapchainApi::getWidth is null",
			"SwapchainApi::getHeight is null",
			"SwapchainApi::resize is null",
			"SwapchainApi::setPresentMode is null",
			"SwapchainApi::supportsReadback is null",
		};

		static constexpr std::string_view kBlockMissing{ "the backend swapchain publishes no SwapchainApi, or a shorter one than this build reads" };

		static_assert(sizeof(SwapchainApi) == (kBlockHeaderWords + kNames.size()) * sizeof(AnyDispatchEntry),
			"An entry was added to or removed from SwapchainApi without updating this list. The sweep reads a block as a flat run of function "
			"pointers, so the two have to stay the same length.");
	};

	/**
	 * \brief Returns how many function entries the backend declared in a block, capped to the entries this build knows how to read.
	 *
	 * \note Longer blocks are treated as this build's known length. Shorter blocks are swept only through the bytes they actually declared.
	 */
	template <typename Block>
	[[nodiscard]] std::size_t DeclaredEntryCount(const Block & block) noexcept
	{
		constexpr std::size_t known = BlockEntries<Block>::kNames.size();

		if (block.header.byteSize < sizeof(InterfaceHeader))
		{
			return 0;
		}

		const std::size_t declared = (block.header.byteSize - sizeof(InterfaceHeader)) / sizeof(AnyDispatchEntry);
		return declared < known ? declared : known;
	}

	/**
	 * \brief Returns the first null dispatch entry, or the declared entry count when all declared entries are present.
	 *
	 * The block is copied one dispatch word at a time because a shorter block has fewer bytes than Block and must not be read through the full type.
	 */
	template <typename Block>
	[[nodiscard]] std::size_t FirstMissingEntry(const Block & block) noexcept
	{
		const std::size_t count = DeclaredEntryCount(block);

		const auto * words = reinterpret_cast<const std::byte *>(&block) + sizeof(InterfaceHeader); // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
		for (std::size_t index = 0; index < count; ++index)
		{
			AnyDispatchEntry entry = nullptr;
			std::memcpy(static_cast<void *>(&entry), words + (index * sizeof(AnyDispatchEntry)), sizeof(entry));
			if (entry == nullptr)
			{
				return index;
			}
		}

		return count;
	}

	/**
	 * \brief Requires every dispatch entry read by this build to be declared and non-null.
	 *
	 * The sweep runs on every facade creation instead of caching by block pointer. A backend can free a block and later allocate a different block at the same
	 * address.
	 */
	template <typename Block>
	[[nodiscard]] bool RequireCompleteBlock(const Block * block, Error * error) noexcept
	{
		constexpr std::size_t known = BlockEntries<Block>::kNames.size();

		const std::size_t missing = FirstMissingEntry(*block);
		if (missing == known)
		{
			return true;
		}

		if (error != nullptr)
		{
			// Short blocks are reported as the first entry they do not reach because that is the first call site that would fail. kNames entries are constexpr
			// views over string literals. Error::message carries only const char *, so the data pointer is intentional.
			*error = Error{
				.code = ErrorCode::eValidationFailed,
				// NOLINTNEXTLINE(bugprone-suspicious-stringview-data-usage)
				.message = BlockEntries<Block>::kNames[missing].data(),
			};
		}

		return false;
	}

	/**
	 * \brief Resolves and validates the required ABI block for a backend object before it becomes a public facade.
	 *
	 * Returns nullptr when the backend object is null, declines the block, publishes a shorter block, or leaves a required entry null.
	 */
	template <typename Block>
	[[nodiscard]] const Block * CheckedBlock(void * impl, Error * error) noexcept
	{
		if (impl == nullptr)
		{
			return nullptr;
		}

		const auto * block = QueryBlock<Block>(impl);
		if (block == nullptr)
		{
			if (error != nullptr)
			{
				// kBlockMissing is a constexpr view over a string literal. Error::message carries only const char *, so the data pointer is intentional.
				*error = Error{
					.code = ErrorCode::eValidationFailed,
					// NOLINTNEXTLINE(bugprone-suspicious-stringview-data-usage)
					.message = BlockEntries<Block>::kBlockMissing.data(),
				};
			}

			return nullptr;
		}

		return RequireCompleteBlock(block, error) ? block : nullptr;
	}

	/**
	 * \brief Validates a child object's required block when the cached block pointer lives on the device block set instead of the facade.
	 */
	template <typename Block>
	[[nodiscard]] bool CheckedChild(void * impl, Error * error) noexcept
	{
		return CheckedBlock<Block>(impl, error) != nullptr;
	}

	/**
	 * \brief Resolves a backend device into a block set that lives as long as the public device facade.
	 *
	 * \param deviceImpl Backend device object. This may be replaced with the validation wrapper when validation is enabled.
	 * \param desc Device creation description whose allocator, validation, threading, sync, and profiler fields affect block-set setup.
	 * \attention CoreDeviceApi is mandatory. Optional device blocks remain null when the backend declines those capabilities.
	 */
	/*
	 * Why a device could not be created, for a feature the caller said it needed.
	 *
	 * Whole sentences and not a name joined onto a prefix, because Error carries a const char * and has nowhere to build a string. The Vulkan backend has
	 * its own set for the same reason, phrased for adapter selection where this is phrased for the device that was actually made.
	 */
	[[nodiscard]] inline const char * MissingRequiredFeatureMessage(const DeviceFeature feature) noexcept
	{
		switch (feature)
		{
		case DeviceFeature::eTimestampQueries:			return "this device cannot provide a required feature: timestamp queries";
		case DeviceFeature::eSamplerAnisotropy:			return "this device cannot provide a required feature: sampler anisotropy";
		case DeviceFeature::eIndependentBlend:			return "this device cannot provide a required feature: independent blend";
		case DeviceFeature::eDepthBounds:				return "this device cannot provide a required feature: depth bounds";
		case DeviceFeature::ePipelineStatisticsQueries: return "this device cannot provide a required feature: pipeline statistics queries";
		case DeviceFeature::eMultiDrawIndirect:			return "this device cannot provide a required feature: multi-draw indirect";
		case DeviceFeature::eDrawIndirectFirstInstance: return "this device cannot provide a required feature: indirect draw first instance";
		case DeviceFeature::eShaderDrawParameters:		return "this device cannot provide a required feature: shader draw parameters";
		case DeviceFeature::eSparseResources:			return "this device cannot provide a required feature: sparse resources";
		case DeviceFeature::eSparseBuffers:				return "this device cannot provide a required feature: sparse buffers";
		case DeviceFeature::eSparseTextures:			return "this device cannot provide a required feature: sparse textures";
		case DeviceFeature::eSparseVolumes:				return "this device cannot provide a required feature: sparse volumes";
		case DeviceFeature::eTextureViewSwizzle:		return "this device cannot provide a required feature: texture view swizzle";
		case DeviceFeature::eMultiPlanarFormats:		return "this device cannot provide a required feature: multi-planar formats";
		case DeviceFeature::eSamplerYcbcrConversion:	return "this device cannot provide a required feature: sampler Y'CbCr conversion";
		}

		return "this device cannot provide a required feature";
	}

	[[nodiscard]] inline BackendBlockSet * ResolveDeviceBlocks(void *& deviceImpl, const DeviceDesc & desc, Error * error) noexcept
	{
		if (CheckedBlock<CoreDeviceApi>(deviceImpl, error) == nullptr)
		{
			return nullptr;
		}

		// Validation wraps the backend before final block resolution so facades resolve against the wrapper, not the raw backend object.
		deviceImpl = validation::WrapDevice(deviceImpl, desc.validation);

		HostUniquePtr<BackendBlockSet> blocks = HostNew<BackendBlockSet>(deviceImpl, desc);
		if (blocks == nullptr)
		{
			if (error != nullptr)
			{
				*error = Error{
					.code	 = ErrorCode::eOutOfHostMemory,
					.message = "the host allocator refused the storage a device's resolved blocks need",
				};
			}

			return nullptr;
		}

		/*
		 * A required feature the device cannot give is a refusal, not a device that quietly lacks it. Checked here so every backend gets it from one place.
		 * Vulkan also refuses during adapter selection, where it can pick a different adapter instead. This never fires there.
		 *
		 * Direct3D 12 and Metal each drive a single adapter and have nothing to select between. Reading the granted caps matches reading the adapter's for a
		 * required feature, since granting narrows to what was declared.
		 */
		for (const DeviceFeature feature : desc.requiredFeatures)
		{
			if (blocks->Caps().Supports(feature))
			{
				continue;
			}

			if (error != nullptr)
			{
				*error = Error{
					.code	 = ErrorCode::eUnsupportedFeature,
					.message = MissingRequiredFeatureMessage(feature),
				};
			}

			return nullptr;
		}

		if (blocks->Allocator() != nullptr && blocks->Device().placedMemory == nullptr)
		{
			if (error != nullptr)
			{
				*error = Error{
					.code	 = ErrorCode::eUnsupportedFeature,
					.message = "a device memory allocator was installed on a backend that publishes no placed memory support",
				};
			}

			return nullptr;
		}

		return blocks.release();
	}

	/**
	 * \brief Releases the resolved device block set when the owning public device facade is destroyed.
	 */
	inline void ReleaseDeviceBlocks(BackendBlockSet * blocks) noexcept
	{
		HostDeleter{ .size = sizeof(BackendBlockSet), .alignment = alignof(BackendBlockSet) }(blocks);
	}

	/**
	 * \brief Rejects cooperative threading unless the host supplied every synchronization callback.
	 *
	 * \note Shared by dynamic and static device creation so all entry points enforce the same threading contract.
	 */
	[[nodiscard]] inline Result<void> CheckThreading(const DeviceDesc & desc) noexcept
	{
		if (desc.threading != ThreadingMode::eCooperative || desc.sync.IsComplete())
		{
			return {};
		}

		return Error{
			.code	 = ErrorCode::eInvalidArgument,
			.message = "a cooperative device needs a complete SyncOps: create, destroy, acquire, tryAcquire, and release",
		};
	}

	/**
	 * \brief Destroys a backend device that was created successfully but cannot be driven through a complete CoreDeviceApi.
	 */
	inline void ReleaseUndrivableDevice(void * deviceImpl) noexcept
	{
		if (const auto * partial = QueryBlock<CoreDeviceApi>(deviceImpl); partial != nullptr && partial->destroyDevice != nullptr)
		{
			const LifetimeLock lifetime;
			partial->destroyDevice(deviceImpl);
		}
	}

} // namespace azo::rhi::detail

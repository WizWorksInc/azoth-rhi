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
 * \brief External handle types and the per adapter support query for sharing memory and synchronization across an API, device, or process boundary.
 */

#include "azoth/rhi/core/enums.hpp"
#include "azoth/rhi/core/flags.hpp"

#include <array>

// ReSharper disable once CppUnusedIncludeDirective
#include <cstdint>

namespace azo::rhi
{

	/**
	 * \brief Transport an external object travels over, named as the target APIs name it.
	 *
	 * \attention A handle a caller holds stays theirs until Device::CloseExportedHandle releases it, except a global share handle which nobody owns. Import
	 * duplicates a file descriptor without taking it.
	 *
	 * \note The values mix three axes: operating system transports, a producing API for the Direct3D 12 pair and contents for eDmaBuf. The two Metal types are
	 * objects moved over XPC and bridge to nothing.
	 */
	enum class ExternalHandleType : std::uint32_t // NOLINT(performance-enum-size)
	{
		/**
		 * \brief POSIX file descriptor with driver private contents.
		 */
		eOpaqueFd = 1u << 0u,

		/**
		 * \brief Win32 NT handle with driver private contents.
		 */
		eOpaqueWin32 = 1u << 1u,

		/**
		 * \brief Win32 global share handle. Owned by nobody, so releasing it does nothing and closing it yourself is a defect.
		 */
		eOpaqueWin32Kmt = 1u << 2u,

		/**
		 * \brief NT handle produced by ID3D12Device::CreateSharedHandle on a resource.
		 */
		eD3D12Resource = 1u << 3u,

		/**
		 * \brief NT handle produced by ID3D12Device::CreateSharedHandle on a heap.
		 *
		 * Distinct from eD3D12Resource because the two name different objects, not two spellings of one. A heap handle covers memory that several placed resources
		 * sit in, which is what an importer suballocating against it needs, and both Vulkan and CUDA name it separately for exactly that reason.
		 */
		eD3D12Heap = 1u << 4u,

		/**
		 * \brief NT handle produced by ID3D12Device::CreateSharedHandle on a fence. Synchronization only.
		 */
		eD3D12Fence = 1u << 5u,

		/**
		 * \brief Linux dma-buf descriptor, whose layout is self describing, not driver private.
		 */
		eDmaBuf = 1u << 6u,

		/**
		 * \brief MTLSharedEventHandle. Synchronization only.
		 */
		eMtlSharedEvent = 1u << 7u,

		/**
		 * \brief MTLSharedTextureHandle. Textures only, and there is no buffer counterpart because Metal has none.
		 */
		eMtlSharedTexture = 1u << 8u,
	};

	/**
	 * \brief Every handle type, in one place, for code that has to consider all of them.
	 *
	 * A hand written list at each such site drifts the moment the enum grows, and silently: a type absent from a refusal loop is a declaration that passes
	 * unchecked and not a compile error. Iterate this instead.
	 */
	inline constexpr std::array kAllExternalHandleTypes{
		ExternalHandleType::eOpaqueFd,
		ExternalHandleType::eOpaqueWin32,
		ExternalHandleType::eOpaqueWin32Kmt,
		ExternalHandleType::eD3D12Resource,
		ExternalHandleType::eD3D12Heap,
		ExternalHandleType::eD3D12Fence,
		ExternalHandleType::eDmaBuf,
		ExternalHandleType::eMtlSharedEvent,
		ExternalHandleType::eMtlSharedTexture,
	};

	/*
	 * The list above is provably whole, not merely maintained. Every value is one bit running from the low bit upward with no gaps, so a complete list ORs to a
	 * solid run. Adding an enumerator without adding it here breaks the build without drifting silently. A type missing from the list is a declaration nothing
	 * checks and the tests sweep this same list.
	 */
	namespace detail
	{
		[[nodiscard]] consteval std::uint32_t AllExternalHandleBits() noexcept
		{
			std::uint32_t bits = 0;
			for (const ExternalHandleType type : kAllExternalHandleTypes)
			{
				bits |= static_cast<std::uint32_t>(type);
			}

			return bits;
		}
	} // namespace detail

	static_assert(detail::AllExternalHandleBits() == (1u << kAllExternalHandleTypes.size()) - 1u,
		"kAllExternalHandleTypes is missing an ExternalHandleType, or the values are no longer one contiguous run of bits from the low bit up. Every "
		"site that sweeps handle types reads this list, so a gap here is a type nothing validates and nothing tests.");

	/**
	 * \brief One external handle, tagged by what it is.
	 *
	 * One payload with a discriminator and not a struct per handle type, which is the shape the target APIs already take: CUDA imports every one of these through
	 * a single call with a type field.
	 *
	 * \attention What a caller holds here is theirs until Device::CloseExportedHandle releases it. See ExternalHandleType for why that is one rule, not
	 * three.
	 */
	struct ExternalHandle final
	{
		ExternalHandleType type = ExternalHandleType::eOpaqueFd;

		/**
		 * \brief POSIX file descriptor, read when type is eOpaqueFd or eDmaBuf. Negative when this handle carries none.
		 */
		int fd = -1;

		/**
		 * \brief Win32 handle for the opaque and Direct3D 12 types or the Metal object for the two Metal types. Null when this handle carries none.
		 *
		 * The Metal types are not operating system handles. MTLSharedTextureHandle and MTLSharedEventHandle are objects moved between processes by serializing over
		 * XPC, so they travel here as a pointer and cannot be handed to another API.
		 */
		void * handle = nullptr;
	};

	/**
	 * \brief What an external handle carries, which is the axis backends answer support along.
	 *
	 * Timelines and binary semaphores are separate entries and not one semaphore entry with a type beside it, because Vulkan permits their supported handle type
	 * sets to differ on one device. One answer covering both would be a guess on at least one of them.
	 */
	enum class ExternalObjectKind : std::uint8_t
	{
		eBuffer,
		eTexture,
		eTimeline,
		eBinarySemaphore,

		/**
		 * \brief A memory heap, which is what a caller placing its own resources shares without sharing each one.
		 *
		 * Separate from eBuffer because the memory is the shared object and the resources over it are the importer's business. A caller that chose placed resources
		 * to control its own memory would otherwise have to give that up to share anything.
		 */
		eHeap,
	};

	/**
	 * \brief What an adapter supports for one object kind and one handle type.
	 */
	struct ExternalHandleSupportDesc final
	{
		/**
		 * \brief Which enumerated adapter is being asked about.
		 */
		std::uint32_t adapterIndex = 0;

		ExternalObjectKind kind		  = ExternalObjectKind::eBuffer;
		ExternalHandleType handleType = ExternalHandleType::eOpaqueFd;

		/**
		 * \brief Which texture format is being asked about. Read only when kind is eTexture and ignored otherwise.
		 *
		 * Textures need it because the answer is per format, not per adapter: Vulkan resolves external image support through the image format query, so there is no
		 * adapter-wide answer to give and picking a representative format here would be inventing one. Buffers and semaphores have no such axis and leave this alone.
		 */
		Format format = Format::eUndefined;
	};

	/**
	 * \brief An adapter's answer for one object kind and one handle type.
	 *
	 * \attention A true answer here is necessary and not sufficient. Legality is finally resolved by the driver at import, which is required to validate
	 * an incoming handle and fail, not produce a broken object, so an import can still be refused for reasons the handle type does not capture.
	 */
	struct ExternalHandleSupport final
	{
		/**
		 * \brief An object of this kind can be created exportable to this handle type.
		 */
		bool exportable = false;

		/**
		 * \brief A handle of this type can be imported as an object of this kind.
		 */
		bool importable = false;

		/**
		 * \brief Every handle type that can alias the same underlying payload as this one.
		 *
		 * Handle type equality is not what decides whether two components can share. This mask is, and a type absent from it cannot alias the queried one even where
		 * both are supported on their own.
		 */
		Flags<ExternalHandleType> compatibleTypes;
	};

} // namespace azo::rhi

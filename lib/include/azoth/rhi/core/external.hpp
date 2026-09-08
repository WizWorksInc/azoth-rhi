// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include "azoth/rhi/core/enums.hpp"
#include "azoth/rhi/core/flags.hpp"

#include <array>
#include <cstdint>

namespace azo::rhi
{

	enum class ExternalHandleType : std::uint32_t // NOLINT(performance-enum-size)
	{
		eOpaqueFd = 1u << 0u,

		eOpaqueWin32 = 1u << 1u,

		eOpaqueWin32Kmt = 1u << 2u,

		eD3D12Resource = 1u << 3u,

		eD3D12Heap = 1u << 4u,

		eD3D12Fence = 1u << 5u,

		eDmaBuf = 1u << 6u,

		eMtlSharedEvent = 1u << 7u,

		eMtlSharedTexture = 1u << 8u,
	};

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
	}

	static_assert(detail::AllExternalHandleBits() == (1u << kAllExternalHandleTypes.size()) - 1u,
		"kAllExternalHandleTypes is missing an ExternalHandleType, or the values are no longer one contiguous run of bits from the low bit up. Every "
		"site that sweeps handle types reads this list, so a gap here is a type nothing validates and nothing tests.");

	struct ExternalHandle final
	{
		ExternalHandleType type = ExternalHandleType::eOpaqueFd;

		int fd = -1;

		void * handle = nullptr;
	};

	enum class ExternalObjectKind : std::uint8_t
	{
		eBuffer,
		eTexture,
		eTimeline,
		eBinarySemaphore,

		eHeap,
	};

	struct ExternalHandleSupportDesc final
	{
		std::uint32_t adapterIndex = 0;

		ExternalObjectKind kind		  = ExternalObjectKind::eBuffer;
		ExternalHandleType handleType = ExternalHandleType::eOpaqueFd;

		Format format = Format::eUndefined;
	};

	struct ExternalHandleSupport final
	{
		bool exportable = false;

		bool importable = false;

		Flags<ExternalHandleType> compatibleTypes;
	};

}

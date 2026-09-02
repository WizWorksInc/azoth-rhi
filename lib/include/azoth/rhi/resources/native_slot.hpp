// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <cstdint>

namespace azo::rhi
{

	struct ShaderAbiVersion final
	{
		std::uint32_t value = 0;

		[[nodiscard]] friend constexpr bool operator==(ShaderAbiVersion lhs, ShaderAbiVersion rhs) noexcept = default;
	};

	inline constexpr ShaderAbiVersion kShaderAbiVersion{ 2 };

	inline constexpr std::uint32_t kPushConstantRegisterSpace = 0;

	[[nodiscard]] constexpr std::uint32_t D3D12RegisterSpaceForSet(const std::uint32_t set) noexcept
	{
		return set + 1;
	}

	inline constexpr std::uint32_t kMetalPushConstantBufferIndex = 0;

	[[nodiscard]] constexpr std::uint32_t MetalArgumentBufferIndexForSet(const std::uint32_t set) noexcept
	{
		return set + 1;
	}

	inline constexpr const char * kVertexSemanticName = "TEXCOORD";

	enum class NativeSlotClass : std::uint8_t
	{
		eBinding,

		eConstantBuffer,

		eShaderResource,

		eUnorderedAccess,

		eSampler,

		eBuffer,

		eTexture,

		eArgumentBufferMember,
	};

	struct NativeSlot final
	{
		std::uint32_t space	  = 0;
		std::uint32_t index	  = 0;
		NativeSlotClass klass = NativeSlotClass::eBinding;

		[[nodiscard]] friend constexpr bool operator==(NativeSlot lhs, NativeSlot rhs) noexcept = default;
	};

	struct NativeBinding final
	{
		NativeSlot resource{};

		NativeSlot sampler{};
		bool hasSampler = false;

		bool exists = false;
	};

}

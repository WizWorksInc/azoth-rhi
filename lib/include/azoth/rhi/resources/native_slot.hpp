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
 * \brief The value types the shader binding ABI speaks in: a native slot and the revision that decided it.
 *
 * Split from binding_abi.hpp so a shader binary can name a slot without dragging in the resolver. NativeBindingFor needs the whole pipeline layout and
 * therefore device.hpp, which includes pipeline.hpp, where a ShaderBinary lives. Keeping the values here breaks that cycle.
 */

// ReSharper disable once CppUnusedIncludeDirective
#include <cstdint>

namespace azo::rhi
{

	/**
	 * \brief Revision of the binding ABI a backend implements and a shader was compiled against.
	 *
	 * A caller computing slots ahead of time records which revision it targeted. The revision changes when an existing mapping changes meaning, never when a
	 * backend is added, so a shader built against a revision keeps working until this number moves.
	 */
	struct ShaderAbiVersion final
	{
		std::uint32_t value = 0;

		[[nodiscard]] friend constexpr bool operator==(ShaderAbiVersion lhs, ShaderAbiVersion rhs) noexcept = default;
	};

	/**
	 * \brief The binding ABI this build implements.
	 */
	inline constexpr ShaderAbiVersion kShaderAbiVersion{ 2 };

	/**
	 * \brief Register space push constants occupy on Direct3D 12, at register b0.
	 *
	 * Fixed, not derived, so a shader author writes register(b0, space0) for push constants whatever else the layout holds. Descriptor sets start one space above
	 * it for the same reason: a set's space must not move because a push constant range was added or removed.
	 */
	inline constexpr std::uint32_t kPushConstantRegisterSpace = 0;

	/**
	 * \brief Register space descriptor set N occupies on Direct3D 12.
	 *
	 * One space per set, which is what Shader Model 5.1 added register spaces for. The offset past kPushConstantRegisterSpace is the whole of the arithmetic, and
	 * it is deliberately not a function of what any set contains.
	 */
	[[nodiscard]] constexpr std::uint32_t D3D12RegisterSpaceForSet(const std::uint32_t set) noexcept
	{
		return set + 1;
	}

	/**
	 * \brief The Metal buffer index a push constant range binds at.
	 *
	 * Reserved whether or not a layout declares one, which is what keeps adding or removing a range from moving every set. Stated here because it is half of
	 * the published Metal binding ABI, the other half being the set mapping below, and anything recording Metal directly against this RHI's layouts needs both.
	 */
	inline constexpr std::uint32_t kMetalPushConstantBufferIndex = 0;

	/**
	 * \brief The Metal buffer index a descriptor set's argument buffer binds at, on a device that has argument buffers.
	 *
	 * Set N takes buffer N + 1, leaving buffer 0 to the push constant. One slot reserved means adding or removing a push constant range never moves a set. A
	 * conforming shader declares its push constant before its ParameterBlocks, since slangc assigns Metal buffer indices in declaration order.
	 */
	[[nodiscard]] constexpr std::uint32_t MetalArgumentBufferIndexForSet(const std::uint32_t set) noexcept
	{
		return set + 1;
	}

	/**
	 * \brief The HLSL semantic name a vertex attribute carries, its location being the semantic index.
	 *
	 * Direct3D 12 addresses vertex inputs by semantic name and index where Vulkan and Metal use location. The attribute at location N is TEXCOORDN. A shader
	 * written float2 uv : TEXCOORD2 is the attribute declared at location 2.
	 *
	 * TEXCOORD because it is the one semantic Direct3D 12 lets a shader repeat freely. A binary whose compiler emitted something else says so through
	 * ShaderBindingMap::vertexSemanticName.
	 */
	inline constexpr const char * kVertexSemanticName = "TEXCOORD";

	/**
	 * \brief Which native index space a slot counts within.
	 *
	 * Backends use the classes their API has. Direct3D 12 numbers four register classes inside a register space. Metal numbers three argument tables. Vulkan
	 * numbers one binding space per set and needs no class at all, which eBinding says.
	 */
	enum class NativeSlotClass : std::uint8_t
	{
		/**
		 * \brief Vulkan, where a binding number is the whole address and no per-kind counter exists.
		 */
		eBinding,

		/**
		 * \brief Direct3D 12 b registers.
		 */
		eConstantBuffer,

		/**
		 * \brief Direct3D 12 t registers.
		 */
		eShaderResource,

		/**
		 * \brief Direct3D 12 u registers.
		 */
		eUnorderedAccess,

		/**
		 * \brief Direct3D 12 s registers, and the Metal sampler argument table.
		 */
		eSampler,

		/**
		 * \brief The Metal buffer argument table.
		 */
		eBuffer,

		/**
		 * \brief The Metal texture argument table.
		 */
		eTexture,

		/**
		 * \brief A member of a Metal argument buffer, which is what a descriptor set is on a device with argument buffers.
		 *
		 * The space is the buffer index the set is bound at and the index is the member's position in the struct and not an argument table slot. Every member is
		 * eight bytes: a resource id for a texture or a sampler, a GPU address for a buffer.
		 */
		eArgumentBufferMember,
	};

	/**
	 * \brief One native slot: which index space, which space within it, which index.
	 */
	struct NativeSlot final
	{
		std::uint32_t space	  = 0;
		std::uint32_t index	  = 0;
		NativeSlotClass klass = NativeSlotClass::eBinding;

		[[nodiscard]] friend constexpr bool operator==(NativeSlot lhs, NativeSlot rhs) noexcept = default;
	};

	/**
	 * \brief Where one RHI binding lands.
	 *
	 * Usually one slot. A combined image sampler is two on Direct3D 12, which splits it into a t register and an s register, so the second is carried here without
	 * making a caller ask twice and know when to.
	 */
	struct NativeBinding final
	{
		NativeSlot resource{};

		NativeSlot sampler{};
		bool hasSampler = false;

		/**
		 * \brief False when the layout has no such set or no such binding in it.
		 */
		bool exists = false;
	};

} // namespace azo::rhi

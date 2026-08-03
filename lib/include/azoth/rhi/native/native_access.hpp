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
 * \brief Native declarations for adopting objects made on this device, reading back the objects handles stand for, and command-list mutation.
 */

#include "azoth/rhi/commands/sync.hpp"
#include "azoth/rhi/core/c_string.hpp"
#include "azoth/rhi/core/resource_handles.hpp"
#include "azoth/rhi/device/api_tags.hpp"
#include "azoth/rhi/resources/resources.hpp"

// ReSharper disable once CppUnusedIncludeDirective
#include <cstdint>
#include <span>

namespace azo::rhi
{

	/**
	 * \brief How a native command mutation accessed a resource.
	 */
	enum class NativeMutationAccess : std::uint8_t
	{
		eReadOnly,
		eReadWrite,
	};

	/**
	 * \brief Buffer touched by native command recording and the state it is left in.
	 */
	struct NativeTouchedBuffer final
	{
		BufferHandle buffer{};
		NativeMutationAccess access = NativeMutationAccess::eReadOnly;
		ResourceState finalState{};
	};

	/**
	 * \brief Texture subresource range touched by native command recording and the state it is left in.
	 */
	struct NativeTouchedTexture final
	{
		TextureHandle texture{};
		NativeMutationAccess access = NativeMutationAccess::eReadOnly;
		TextureSubresourceRange range{};
		ResourceState finalState{};
	};

	/**
	 * \brief Resource state reconciliation contract for a native command-list mutation.
	 *
	 * Native recording is legal only inside a ModifyNative scope that declares every touched resource and its final state.
	 */
	struct NativeMutationDesc final
	{
		std::span<const NativeTouchedBuffer> buffers;
		std::span<const NativeTouchedTexture> textures;
		CString debugName = nullptr;
	};

	/**
	 * \brief Who destroys the native object behind an adopted resource.
	 *
	 * \attention Exactly one owner destroys the object. Picking eRhiOwns for an object the caller also frees, or eCallerOwns for one it then forgets, is
	 * a double free or a leak that no validation layer reports, this being a question about the caller's own code, not about the graphics API.
	 */
	enum class AdoptedLifetime : std::uint8_t
	{
		/**
		 * \brief The RHI destroys it, for a caller that built the object natively and then wants to stop thinking about it.
		 */
		eRhiOwns,

		/**
		 * \brief The caller destroys it, through whatever library made it. Destroying the RHI handle retires the slot and leaves the object alone.
		 */
		eCallerOwns,
	};

	/**
	 * \brief Parameters for adopting a native buffer made on this same device.
	 *
	 * desc restates what the object already is, since nothing about it can be read back from a native handle portably.
	 */
	struct AdoptedBufferDesc final
	{
		BufferDesc desc{};
		AdoptedLifetime lifetime = AdoptedLifetime::eCallerOwns;

		/**
		 * \brief The state the object arrives in, which is whatever its producer left it in.
		 *
		 * The validation registry is seeded from this and from initialQueueFamily below, so the caller's first barrier names a before state that matches what the
		 * object actually arrived in.
		 */
		ResourceState initialState{};

		/**
		 * \brief The queue family that owns the object when it arrives, kIgnoreQueueFamily when no transfer is needed to use it here.
		 *
		 * Beside initialState, not folded into it because a barrier states the two separately. Seeding one and leaving the other cleared makes the first ownership
		 * transfer against an adopted object as wrong as the first layout transition would be.
		 */
		std::uint32_t initialQueueFamily = kIgnoreQueueFamily;
		CString debugName				 = nullptr;
	};

	/**
	 * \brief Parameters for adopting a native texture made on this same device.
	 *
	 * Carries the same restatement and the same two seeded fields as the buffer form.
	 */
	struct AdoptedTextureDesc final
	{
		TextureDesc desc{};
		AdoptedLifetime lifetime = AdoptedLifetime::eCallerOwns;
		ResourceState initialState{};

		/**
		 * \brief The queue family that owns the object when it arrives, kIgnoreQueueFamily when no transfer is needed to use it here.
		 *
		 * Beside initialState, not folded into it because a barrier states the two separately. Seeding one and leaving the other cleared makes the first ownership
		 * transfer against an adopted object as wrong as the first layout transition would be.
		 */
		std::uint32_t initialQueueFamily = kIgnoreQueueFamily;
		CString debugName				 = nullptr;
	};

	/**
	 * \brief Parameters for adopting a native texture view made on this same device.
	 *
	 * A view is adoptable in its own right, not rebuildable from an adopted texture, because a view can carry a Y'CbCr conversion that the RHI has no way to
	 * reconstruct and no surface for naming.
	 */
	struct AdoptedTextureViewDesc final
	{
		/**
		 * \brief The texture this views, which is required, not optional.
		 *
		 * It is what lets the validator refuse a view whose texture has already been retired, and a caller that cannot name it has no way to barrier the image behind
		 * the view anyway.
		 */
		TextureHandle texture{};

		Format format			 = Format::eUndefined;
		SampleCount samples		 = SampleCount::e1;
		AdoptedLifetime lifetime = AdoptedLifetime::eCallerOwns;
		CString debugName		 = nullptr;
	};

	/**
	 * \brief Parameters for adopting a native sampler made on this same device.
	 *
	 * Carries no state and no source, a sampler having neither. It is on this surface because a Y'CbCr sampler cannot be rebuilt from a description either, for
	 * the same reason its view cannot.
	 */
	struct AdoptedSamplerDesc final
	{
		AdoptedLifetime lifetime = AdoptedLifetime::eCallerOwns;
		CString debugName		 = nullptr;
	};

	/**
	 * \brief Backend-specialized native payload naming the buffer object itself, for both directions.
	 *
	 * The primary template is undefined so core headers never name backend-native handle types.
	 */
	template <GraphicsApiTag Api>
	struct NativeBuffer;

	/**
	 * \brief Backend-specialized native payload naming the texture object itself, for both directions.
	 *
	 * The primary template is undefined so core headers never name backend-native handle types.
	 */
	template <GraphicsApiTag Api>
	struct NativeTexture;

	/**
	 * \brief Parameters for adopting a native timeline made on this same device.
	 *
	 * Carries no initial value, unlike the state a texture arrives in. A timeline's current value is readable from the object, so a declared one could only be
	 * ignored or believed over the truth, and both are worse than not asking.
	 */
	struct AdoptedTimelineDesc final
	{
		AdoptedLifetime lifetime = AdoptedLifetime::eCallerOwns;
		CString debugName		 = nullptr;
	};

	/**
	 * \brief Parameters for adopting a native binary semaphore made on this same device.
	 */
	struct AdoptedBinarySemaphoreDesc final
	{
		AdoptedLifetime lifetime = AdoptedLifetime::eCallerOwns;
		CString debugName		 = nullptr;
	};

	/**
	 * \brief Backend-specialized native payload naming a texture view object, for both directions.
	 *
	 * Not every backend has one. Direct3D 12 builds views as descriptors written into a heap and not as objects with their own lifetime, so it declines these
	 * entries by name without inventing an object to stand behind them.
	 */
	template <GraphicsApiTag Api>
	struct NativeTextureView;

	/**
	 * \brief Backend-specialized native payload naming a sampler object, for both directions.
	 */
	template <GraphicsApiTag Api>
	struct NativeSampler;

	/**
	 * \brief Backend-specialized native payload naming a timeline object, for both directions.
	 *
	 * Kept apart from the binary semaphore payload even where a backend has one object behind both, because the two handle kinds are already separate and a
	 * timeline adopted as a binary semaphore is a confusion the slot maps are shaped to prevent.
	 */
	template <GraphicsApiTag Api>
	struct NativeTimeline;

	/**
	 * \brief Backend-specialized native payload naming a binary semaphore object, for both directions.
	 */
	template <GraphicsApiTag Api>
	struct NativeBinarySemaphore;

	namespace native
	{

		/**
		 * \brief Backend-specialized native view customization point.
		 *
		 * A specialization names native view types and builds views from RHI implementation pointers. The primary template is undefined.
		 */
		template <GraphicsApiTag Api>
		struct NativeAccess;

		/**
		 * \brief True once the backend native-access header for Api has been included.
		 *
		 * Used by native interop entry points so a missing include fails at the API boundary instead of in deeper template code.
		 */
		template <typename Api>
		concept HasNativeAccess = GraphicsApiTag<Api> && requires { sizeof(NativeAccess<Api>); };

	} // namespace native

} // namespace azo::rhi

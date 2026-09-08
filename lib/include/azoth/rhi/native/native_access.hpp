// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include "azoth/rhi/commands/sync.hpp"
#include "azoth/rhi/core/c_string.hpp"
#include "azoth/rhi/core/resource_handles.hpp"
#include "azoth/rhi/device/api_tags.hpp"
#include "azoth/rhi/resources/resources.hpp"

#include <cstdint>
#include <span>

namespace azo::rhi
{

	enum class NativeMutationAccess : std::uint8_t
	{
		eReadOnly,
		eReadWrite,
	};

	struct NativeTouchedBuffer final
	{
		BufferHandle buffer{};
		NativeMutationAccess access = NativeMutationAccess::eReadOnly;
		ResourceState finalState{};
	};

	struct NativeTouchedTexture final
	{
		TextureHandle texture{};
		NativeMutationAccess access = NativeMutationAccess::eReadOnly;
		TextureSubresourceRange range{};
		ResourceState finalState{};
	};

	struct NativeMutationDesc final
	{
		std::span<const NativeTouchedBuffer> buffers;
		std::span<const NativeTouchedTexture> textures;
		CString debugName = nullptr;
	};

	enum class AdoptedLifetime : std::uint8_t
	{
		eRhiOwns,

		eCallerOwns,
	};

	struct AdoptedBufferDesc final
	{
		BufferDesc desc{};
		AdoptedLifetime lifetime = AdoptedLifetime::eCallerOwns;

		ResourceState initialState{};

		QueueOwnership initialOwnership{};
		CString debugName = nullptr;
	};

	struct AdoptedTextureDesc final
	{
		TextureDesc desc{};
		AdoptedLifetime lifetime = AdoptedLifetime::eCallerOwns;
		ResourceState initialState{};

		QueueOwnership initialOwnership{};
		CString debugName = nullptr;
	};

	struct AdoptedTextureViewDesc final
	{
		TextureHandle texture{};

		Format format			 = Format::eUndefined;
		SampleCount samples		 = SampleCount::e1;
		AdoptedLifetime lifetime = AdoptedLifetime::eCallerOwns;
		CString debugName		 = nullptr;
	};

	struct AdoptedSamplerDesc final
	{
		AdoptedLifetime lifetime = AdoptedLifetime::eCallerOwns;
		CString debugName		 = nullptr;
	};

	template <GraphicsApiTag Api>
	struct NativeBuffer;

	template <GraphicsApiTag Api>
	struct NativeTexture;

	struct AdoptedTimelineDesc final
	{
		AdoptedLifetime lifetime = AdoptedLifetime::eCallerOwns;
		CString debugName		 = nullptr;
	};

	struct AdoptedBinarySemaphoreDesc final
	{
		AdoptedLifetime lifetime = AdoptedLifetime::eCallerOwns;
		CString debugName		 = nullptr;
	};

	template <GraphicsApiTag Api>
	struct NativeTextureView;

	template <GraphicsApiTag Api>
	struct NativeSampler;

	template <GraphicsApiTag Api>
	struct NativeTimeline;

	template <GraphicsApiTag Api>
	struct NativeBinarySemaphore;

	namespace native
	{

		template <GraphicsApiTag Api>
		struct NativeAccess;

		template <typename Api>
		concept HasNativeAccess = GraphicsApiTag<Api> && requires { sizeof(NativeAccess<Api>); };

	}

}

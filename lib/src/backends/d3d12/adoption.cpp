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

#ifdef _WIN32

	#include "backends/d3d12/internal.hpp"

namespace azo::rhi::d3d12
{
	namespace
	{
		/*
		 * What the caller asked for, honored, not assumed.
		 *
		 * Both entries used to record an adopted lifetime whatever the description said, which made eRhiOwns a value the surface accepted and ignored.
		 * A caller picking it has built the object natively and wants to stop thinking about it, and the slot owning it is what delivers that.
		 */
		[[nodiscard]] SlotLifetime LifetimeOf(const AdoptedLifetime lifetime) noexcept
		{
			return lifetime == AdoptedLifetime::eRhiOwns ? SlotLifetime::eOwned : SlotLifetime::eAdopted;
		}
	} // namespace

	BufferHandle D3D12AdoptBuffer(void * impl, GraphicsApiId api, const void * nativeImport, const AdoptedBufferDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.d3d12.adoptBuffer");

		if (api != D3D12Api::id)
		{
			return FailValue<BufferHandle>(error, ErrorCode::eUnsupportedApi, "import payload API does not match the device backend");
		}

		ID3D12Resource * external = static_cast<const NativeBuffer<D3D12Api> *>(nativeImport)->resource;
		if (external == nullptr)
		{
			return FailValue<BufferHandle>(error, ErrorCode::eInvalidArgument, "import payload has a null ID3D12Resource");
		}

		auto * device = static_cast<D3D12Device *>(impl);
		return ReturnValue(device->bufferSlots.Store(BufferSlot{
							   .resource = external,
							   .size	 = desc.desc.size,
							   .lifetime = LifetimeOf(desc.lifetime),
						   .desc	 = detail::Recorded(desc.desc),
						   }),
			error);
	}

	// Wraps an externally created ID3D12Resource as an RHI texture, copying geometry and usage from the desc.
	TextureHandle D3D12AdoptTexture(void * impl, GraphicsApiId api, const void * nativeImport, const AdoptedTextureDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.d3d12.adoptTexture");

		if (api != D3D12Api::id)
		{
			return FailValue<TextureHandle>(error, ErrorCode::eUnsupportedApi, "import payload API does not match the device backend");
		}

		ID3D12Resource * external = static_cast<const NativeTexture<D3D12Api> *>(nativeImport)->resource;
		if (external == nullptr)
		{
			return FailValue<TextureHandle>(error, ErrorCode::eInvalidArgument, "import payload has a null ID3D12Resource");
		}

		auto * device = static_cast<D3D12Device *>(impl);
		return ReturnValue(device->textureSlots.Store(TextureSlot{ .resource = external,
							   .format										 = MapFormat(desc.desc.format),
							   .rhiFormat									 = desc.desc.format,
							   .type										 = desc.desc.type,
							   .mipLevels									 = desc.desc.mipLevels,
							   .arrayLayers									 = desc.desc.arrayLayers,
							   .usage										 = desc.desc.usage,
							   .mutableFormat								 = desc.desc.allowFormatViews,
							   .lifetime									 = LifetimeOf(desc.lifetime),
						   .desc										 = detail::Recorded(desc.desc) }),
			error);
	}

	// Writes the resource backing a buffer into the export payload. The RHI keeps its reference so the caller must not release it.
	bool D3D12GetNativeBuffer(void * impl, GraphicsApiId api, BufferHandle buffer, void * outNativeImport, Error * error) noexcept
	{
		if (api != D3D12Api::id)
		{
			return Fail(error, ErrorCode::eUnsupportedApi, "export payload API does not match the device backend");
		}

		auto * device			= static_cast<D3D12Device *>(impl);
		const BufferSlot * slot = device->bufferSlots.Resolve(buffer, true);
		if (slot == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "export of a buffer this device never created");
		}

		static_cast<NativeBuffer<D3D12Api> *>(outNativeImport)->resource = slot->resource.Get();
		return Succeed(error);
	}

	// Writes the ID3D12Resource backing an RHI texture into the export payload, mirroring D3D12GetNativeBuffer.
	bool D3D12GetNativeTexture(void * impl, GraphicsApiId api, TextureHandle texture, void * outNativeImport, Error * error) noexcept
	{
		if (api != D3D12Api::id)
		{
			return Fail(error, ErrorCode::eUnsupportedApi, "export payload API does not match the device backend");
		}

		auto * device			 = static_cast<D3D12Device *>(impl);
		const TextureSlot * slot = device->textureSlots.Resolve(texture, true);
		if (slot == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "export of a texture this device never created");
		}

		static_cast<NativeTexture<D3D12Api> *>(outNativeImport)->resource = slot->resource.Get();
		return Succeed(error);
	}

	/*
	 * Direct3D 12 has no view or sampler object to adopt and this refuses by name without inventing one. A view here is a descriptor written into a heap
	 * and not an object with its own lifetime, where the binding model copies from heaps this device owns. There is nothing a caller could hand over that
	 * would mean the same thing.
	 */
	TextureViewHandle D3D12AdoptTextureView([[maybe_unused]] void * impl, [[maybe_unused]] GraphicsApiId api, [[maybe_unused]] const void * nativeImport,
		[[maybe_unused]] const AdoptedTextureViewDesc & desc, Error * error) noexcept
	{
		return FailValue<TextureViewHandle>(
			error, ErrorCode::eUnsupportedFeature, "Direct3D 12 builds views as descriptors and not objects, so there is none to adopt");
	}

	SamplerHandle D3D12AdoptSampler([[maybe_unused]] void * impl, [[maybe_unused]] GraphicsApiId api, [[maybe_unused]] const void * nativeImport,
		[[maybe_unused]] const AdoptedSamplerDesc & desc, Error * error) noexcept
	{
		return FailValue<SamplerHandle>(
			error, ErrorCode::eUnsupportedFeature, "Direct3D 12 builds samplers as descriptors and not objects, so there is none to adopt");
	}

	bool D3D12GetNativeTextureView([[maybe_unused]] void * impl, [[maybe_unused]] GraphicsApiId api, [[maybe_unused]] TextureViewHandle view,
		[[maybe_unused]] void * outNativeImport, Error * error) noexcept
	{
		return Fail(error, ErrorCode::eUnsupportedFeature, "Direct3D 12 has no view object to hand back");
	}

	bool D3D12GetNativeSampler([[maybe_unused]] void * impl, [[maybe_unused]] GraphicsApiId api, [[maybe_unused]] SamplerHandle sampler,
		[[maybe_unused]] void * outNativeImport, Error * error) noexcept
	{
		return Fail(error, ErrorCode::eUnsupportedFeature, "Direct3D 12 has no sampler object to hand back");
	}

	/*
	 * A timeline is an ID3D12Fence and adopts directly. A binary semaphore does not: this backend models one as a fence plus two counters it advances
	 * itself, and those counters cannot be recovered from a fence somebody else has been signalling, so adopting one would produce an object whose next
	 * wait targets a value that means nothing.
	 */
	TimelineHandle D3D12AdoptTimeline(void * impl, GraphicsApiId api, const void * nativeImport, const AdoptedTimelineDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.d3d12.adoptTimeline");
		if (api != D3D12Api::id)
		{
			return FailValue<TimelineHandle>(error, ErrorCode::eUnsupportedApi, "adoption payload API does not match the device backend");
		}

		ID3D12Fence * adopted = static_cast<const NativeTimeline<D3D12Api> *>(nativeImport)->fence;
		if (adopted == nullptr)
		{
			return FailValue<TimelineHandle>(error, ErrorCode::eInvalidArgument, "adoption payload has a null ID3D12Fence");
		}

		auto * device = static_cast<D3D12Device *>(impl);
		return ReturnValue(device->timelineSlots.Store(TimelineSlot{ .fence = adopted, .lifetime = LifetimeOf(desc.lifetime) }), error);
	}

	BinarySemaphoreHandle D3D12AdoptBinarySemaphore([[maybe_unused]] void * impl, [[maybe_unused]] GraphicsApiId api,
		[[maybe_unused]] const void * nativeImport, [[maybe_unused]] const AdoptedBinarySemaphoreDesc & desc, Error * error) noexcept
	{
		return FailValue<BinarySemaphoreHandle>(error,
			ErrorCode::eUnsupportedFeature,
			"Direct3D 12 models a binary semaphore as a fence plus counters this device advances, which cannot be recovered from an adopted fence");
	}

	bool D3D12GetNativeTimeline(void * impl, GraphicsApiId api, TimelineHandle timeline, void * outNativeImport, Error * error) noexcept
	{
		if (api != D3D12Api::id)
		{
			return Fail(error, ErrorCode::eUnsupportedApi, "native payload API does not match the device backend");
		}

		auto * device			  = static_cast<D3D12Device *>(impl);
		const TimelineSlot * slot = ResolveTimeline(device, timeline);
		if (slot == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "native read of an invalid timeline handle");
		}

		static_cast<NativeTimeline<D3D12Api> *>(outNativeImport)->fence = slot->fence.Get();
		return Succeed(error);
	}

	bool D3D12GetNativeBinarySemaphore(void * impl, GraphicsApiId api, BinarySemaphoreHandle semaphore, void * outNativeImport, Error * error) noexcept
	{
		if (api != D3D12Api::id)
		{
			return Fail(error, ErrorCode::eUnsupportedApi, "native payload API does not match the device backend");
		}

		auto * device					 = static_cast<D3D12Device *>(impl);
		const BinarySemaphoreSlot * slot = ResolveBinarySemaphore(device, semaphore);
		if (slot == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "native read of an invalid binary semaphore handle");
		}

		// Readable even though the mirror direction is refused: handing back the fence is a read, and the counters that make adoption impossible are this
		// device's business and not something the caller needs to reconstruct.
		static_cast<NativeBinarySemaphore<D3D12Api> *>(outNativeImport)->fence = slot->fence.Get();
		return Succeed(error);
	}

} // namespace azo::rhi::d3d12

#endif // _WIN32

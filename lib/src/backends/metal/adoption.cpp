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

#include "backends/metal/internal.hpp"

namespace azo::rhi::metal
{
	BufferHandle MetalAdoptBuffer(
		void * impl, GraphicsApiId api, const void * nativeImport, [[maybe_unused]] const AdoptedBufferDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal.adoptBuffer");

		if (api != MetalApi::id)
		{
			return FailValue<BufferHandle>(error, ErrorCode::eUnsupportedApi, "import payload API does not match the device backend");
		}

		MTL::Buffer * external = static_cast<const NativeBuffer<MetalApi> *>(nativeImport)->buffer;
		if (external == nullptr)
		{
			return FailValue<BufferHandle>(error, ErrorCode::eInvalidArgument, "import payload has a null Metal buffer");
		}

		auto * device					  = static_cast<MetalDevice *>(impl);
		NS::SharedPtr<MTL::Buffer> buffer = NS::RetainPtr(external);

		const BufferHandle handle = device->buffers.Store(MetalBufferSlot{ .buffer = std::move(buffer), .desc = detail::Recorded(desc.desc) });
		if (!handle.IsValid())
		{
			return FailValue<BufferHandle>(error, ErrorCode::eOutOfHostMemory, "Metal imported buffer tracking failed");
		}

		return ReturnValue(handle, error);
	}

	TextureHandle MetalAdoptTexture([[maybe_unused]] void * impl, [[maybe_unused]] GraphicsApiId api, [[maybe_unused]] const void * nativeImport,
		[[maybe_unused]] const AdoptedTextureDesc & desc, Error * error) noexcept
	{
		return FailValue<TextureHandle>(error, ErrorCode::eUnsupportedFeature, "Metal texture import is not implemented yet");
	}

	bool MetalGetNativeBuffer(void * impl, GraphicsApiId api, BufferHandle buffer, void * outNativeImport, Error * error) noexcept
	{
		if (api != MetalApi::id)
		{
			return Fail(error, ErrorCode::eUnsupportedApi, "export payload API does not match the device backend");
		}

		auto * device = static_cast<MetalDevice *>(impl);

		const auto * tracked = device->buffers.Resolve(buffer, kHandleAlreadyChecked);
		if (tracked == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "export of a buffer this device never created");
		}

		static_cast<NativeBuffer<MetalApi> *>(outNativeImport)->buffer = tracked->buffer.get();
		return Succeed(error);
	}

	bool MetalGetNativeTexture([[maybe_unused]] void * impl, [[maybe_unused]] GraphicsApiId api, [[maybe_unused]] TextureHandle texture,
		[[maybe_unused]] void * outNativeImport, Error * error) noexcept
	{
		return Fail(error, ErrorCode::eUnsupportedFeature, "Metal texture export is not implemented yet");
	}

	/*
	 * This backend builds and traces nothing and DeviceCaps says so with supportsRayTracing false. A handle minted here would leave a caller that read the caps
	 * bit and branched anyway holding an object that can never be built or traced with so both calls are refused instead. Refusing one and minting the other would
	 * be worse than minting both, since whichever call the caller reaches first would decide whether they find out.
	 */
	AccelerationStructureHandle MetalCreateAccelerationStructure(
		[[maybe_unused]] void * impl, [[maybe_unused]] const AccelerationStructureDesc & desc, Error * error) noexcept
	{
		return FailValue<AccelerationStructureHandle>(error, ErrorCode::eUnsupportedFeature, "Metal RHI backend does not support ray tracing");
	}

	RayTracingPipelineHandle MetalCreateRayTracingPipeline(
		[[maybe_unused]] void * impl, [[maybe_unused]] const RayTracingPipelineDesc & desc, Error * error) noexcept
	{
		return FailValue<RayTracingPipelineHandle>(error, ErrorCode::eUnsupportedFeature, "Metal RHI backend does not support ray tracing");
	}

	bool MetalBeginNativeMutation([[maybe_unused]] void * impl, GraphicsApiId api, [[maybe_unused]] const NativeMutationDesc & desc, Error * error) noexcept
	{
		if (api != MetalApi::id)
		{
			return Fail(error, ErrorCode::eUnsupportedApi, "native mutation API does not match the device backend");
		}

		return Succeed(error);
	}

	/*
	 * Metal views and samplers are objects, so both adopt naturally. A view is an MTLTexture over another texture's storage, which is the same thing this
	 * backend's own texture views already are, and a sampler is an MTLSamplerState.
	 *
	 * Reference counting settles the lifetime question that the other two backends have to declare: the slot takes its own reference and the caller keeps its own,
	 * so neither frees the other's out from under it whichever AdoptedLifetime the caller named.
	 */
	TextureViewHandle MetalAdoptTextureView(
		void * impl, const GraphicsApiId api, const void * nativeImport, const AdoptedTextureViewDesc & desc, Error * error) noexcept
	{
		if (api != MetalApi::id)
		{
			return FailValue<TextureViewHandle>(error, ErrorCode::eUnsupportedApi, "adoption payload API does not match the device backend");
		}

		MTL::Texture * adopted = static_cast<const NativeTextureView<MetalApi> *>(nativeImport)->texture;
		if (adopted == nullptr)
		{
			return FailValue<TextureViewHandle>(error, ErrorCode::eInvalidArgument, "adoption payload has a null MTLTexture");
		}

		auto * device = static_cast<MetalDevice *>(impl);
		if (device->textures.Resolve(desc.texture, kHandleAlreadyChecked) == nullptr)
		{
			return FailValue<TextureViewHandle>(error, ErrorCode::eInvalidHandle, "an adopted texture view names a texture this device never handed out");
		}

		const TextureViewHandle handle = device->textureViews.Store(MetalTextureViewSlot{ .texture = NS::RetainPtr(adopted) });
		if (!handle.IsValid())
		{
			return FailValue<TextureViewHandle>(error, ErrorCode::eOutOfHostMemory, "Metal adopted texture view tracking failed");
		}

		SetMetalLabel(adopted, desc.debugName);
		return ReturnValue(handle, error);
	}

	SamplerHandle MetalAdoptSampler(void * impl, const GraphicsApiId api, const void * nativeImport, const AdoptedSamplerDesc & desc, Error * error) noexcept
	{
		if (api != MetalApi::id)
		{
			return FailValue<SamplerHandle>(error, ErrorCode::eUnsupportedApi, "adoption payload API does not match the device backend");
		}

		MTL::SamplerState * adopted = static_cast<const NativeSampler<MetalApi> *>(nativeImport)->sampler;
		if (adopted == nullptr)
		{
			return FailValue<SamplerHandle>(error, ErrorCode::eInvalidArgument, "adoption payload has a null MTLSamplerState");
		}

		auto * device			   = static_cast<MetalDevice *>(impl);
		const SamplerHandle handle = device->samplers.Store(NS::RetainPtr(adopted));
		if (!handle.IsValid())
		{
			return FailValue<SamplerHandle>(error, ErrorCode::eOutOfHostMemory, "Metal adopted sampler tracking failed");
		}

		static_cast<void>(desc);
		return ReturnValue(handle, error);
	}

	bool MetalGetNativeTextureView(void * impl, const GraphicsApiId api, const TextureViewHandle view, void * outNativeImport, Error * error) noexcept
	{
		if (api != MetalApi::id)
		{
			return Fail(error, ErrorCode::eUnsupportedApi, "native payload API does not match the device backend");
		}

		auto * device	  = static_cast<MetalDevice *>(impl);
		const auto * slot = device->textureViews.Resolve(view, kHandleAlreadyChecked);
		if (slot == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "native read of an invalid texture view handle");
		}

		static_cast<NativeTextureView<MetalApi> *>(outNativeImport)->texture = slot->texture.get();
		return Succeed(error);
	}

	bool MetalGetNativeSampler(void * impl, const GraphicsApiId api, const SamplerHandle sampler, void * outNativeImport, Error * error) noexcept
	{
		if (api != MetalApi::id)
		{
			return Fail(error, ErrorCode::eUnsupportedApi, "native payload API does not match the device backend");
		}

		auto * device	  = static_cast<MetalDevice *>(impl);
		const auto * slot = device->samplers.Resolve(sampler, kHandleAlreadyChecked);
		if (slot == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "native read of an invalid sampler handle");
		}

		static_cast<NativeSampler<MetalApi> *>(outNativeImport)->sampler = slot->get();
		return Succeed(error);
	}

	/*
	 * Metal has one MTLSharedEvent behind both kinds, so both adopt.
	 *
	 * The binary semaphore's counter starts at zero here without being read off the event, which is the same thing creating one does. That is right for the case
	 * adoption serves, an event a producer hands over and then signals, and it is the assumption worth stating: a caller adopting an event already part way
	 * through a sequence gets a counter that disagrees with it.
	 */
	TimelineHandle MetalAdoptTimeline(void * impl, const GraphicsApiId api, const void * nativeImport, const AdoptedTimelineDesc & desc, Error * error) noexcept
	{
		if (api != MetalApi::id)
		{
			return FailValue<TimelineHandle>(error, ErrorCode::eUnsupportedApi, "adoption payload API does not match the device backend");
		}

		MTL::SharedEvent * adopted = static_cast<const NativeTimeline<MetalApi> *>(nativeImport)->event;
		if (adopted == nullptr)
		{
			return FailValue<TimelineHandle>(error, ErrorCode::eInvalidArgument, "adoption payload has a null MTLSharedEvent");
		}

		auto * device				= static_cast<MetalDevice *>(impl);
		const TimelineHandle handle = device->timelines.Store(MetalTimeline{ .event = NS::RetainPtr(adopted) });
		if (!handle.IsValid())
		{
			return FailValue<TimelineHandle>(error, ErrorCode::eOutOfHostMemory, "Metal adopted timeline tracking failed");
		}

		static_cast<void>(desc);
		return ReturnValue(handle, error);
	}

	BinarySemaphoreHandle MetalAdoptBinarySemaphore(
		void * impl, const GraphicsApiId api, const void * nativeImport, const AdoptedBinarySemaphoreDesc & desc, Error * error) noexcept
	{
		if (api != MetalApi::id)
		{
			return FailValue<BinarySemaphoreHandle>(error, ErrorCode::eUnsupportedApi, "adoption payload API does not match the device backend");
		}

		MTL::SharedEvent * adopted = static_cast<const NativeBinarySemaphore<MetalApi> *>(nativeImport)->event;
		if (adopted == nullptr)
		{
			return FailValue<BinarySemaphoreHandle>(error, ErrorCode::eInvalidArgument, "adoption payload has a null MTLSharedEvent");
		}

		auto * device					   = static_cast<MetalDevice *>(impl);
		const BinarySemaphoreHandle handle = device->binarySemaphores.Store(MetalBinarySemaphore{ .event = NS::RetainPtr(adopted), .value = 0 });
		if (!handle.IsValid())
		{
			return FailValue<BinarySemaphoreHandle>(error, ErrorCode::eOutOfHostMemory, "Metal adopted binary semaphore tracking failed");
		}

		static_cast<void>(desc);
		return ReturnValue(handle, error);
	}

	bool MetalGetNativeTimeline(void * impl, const GraphicsApiId api, const TimelineHandle timeline, void * outNativeImport, Error * error) noexcept
	{
		if (api != MetalApi::id)
		{
			return Fail(error, ErrorCode::eUnsupportedApi, "native payload API does not match the device backend");
		}

		auto * device	  = static_cast<MetalDevice *>(impl);
		const auto * slot = device->timelines.Resolve(timeline, kHandleAlreadyChecked);
		if (slot == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "native read of an invalid timeline handle");
		}

		static_cast<NativeTimeline<MetalApi> *>(outNativeImport)->event = slot->event.get();
		return Succeed(error);
	}

	bool MetalGetNativeBinarySemaphore(
		void * impl, const GraphicsApiId api, const BinarySemaphoreHandle semaphore, void * outNativeImport, Error * error) noexcept
	{
		if (api != MetalApi::id)
		{
			return Fail(error, ErrorCode::eUnsupportedApi, "native payload API does not match the device backend");
		}

		auto * device	  = static_cast<MetalDevice *>(impl);
		const auto * slot = device->binarySemaphores.Resolve(semaphore, kHandleAlreadyChecked);
		if (slot == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "native read of an invalid binary semaphore handle");
		}

		static_cast<NativeBinarySemaphore<MetalApi> *>(outNativeImport)->event = slot->event.get();
		return Succeed(error);
	}

} // namespace azo::rhi::metal

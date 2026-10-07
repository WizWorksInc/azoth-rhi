// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#include "azoth/rhi/backend/dispatch.hpp"
#include "azoth/rhi/backend/support/resource_record.hpp"
#include "azoth/rhi/core/profiling.hpp"
#include "azoth/rhi/core/resource_handles.hpp"
#include "azoth/rhi/core/result.hpp"
#include "azoth/rhi/device/api_tags.hpp"
#include "azoth/rhi/native/metal_native.hpp"
#include "azoth/rhi/native/native_access.hpp"
#include "azoth/rhi/resources/pipeline.hpp"

#include "backends/metal/internal.hpp"
#include "backends/metal_common/conversions.hpp"

#include <Foundation/NSSharedPtr.hpp>
#include <Metal/MTLBuffer.hpp>
#include <Metal/MTLEvent.hpp>
#include <Metal/MTLSampler.hpp>
#include <Metal/MTLTexture.hpp>

#include <utility>

namespace azo::rhi::metal
{
	BufferHandle metal_adopt_buffer(
		void * impl,
		GraphicsApiId api,
		const void * nativeImport,
		[[maybe_unused]] const AdoptedBufferDesc & desc,
		Error * error
	) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal.adoptBuffer");

		if (api != MetalApi::kId)
		{
			return fail_value<BufferHandle>(error, ErrorCode::eUnsupportedApi, "import payload API does not match the device backend");
		}

		MTL::Buffer * external = static_cast<const NativeBuffer<MetalApi> *>(nativeImport)->buffer;
		if (external == nullptr)
		{
			return fail_value<BufferHandle>(error, ErrorCode::eInvalidArgument, "import payload has a null Metal buffer");
		}

		auto * device					  = static_cast<MetalDevice *>(impl);
		NS::SharedPtr<MTL::Buffer> buffer = NS::RetainPtr(external);

		const BufferHandle handle = device->buffers.store(MetalBufferSlot{ .buffer = std::move(buffer), .desc = detail::recorded(desc.desc) });
		if (!handle.is_valid())
		{
			return fail_value<BufferHandle>(error, ErrorCode::eOutOfHostMemory, "Metal imported buffer tracking failed");
		}

		return return_value(handle, error);
	}

	TextureHandle metal_adopt_texture(
		[[maybe_unused]] void * impl,
		[[maybe_unused]] GraphicsApiId api,
		[[maybe_unused]] const void * nativeImport,
		[[maybe_unused]] const AdoptedTextureDesc & desc,
		Error * error
	) noexcept
	{
		return fail_value<TextureHandle>(error, ErrorCode::eUnsupportedFeature, "Metal texture import is not implemented yet");
	}

	bool metal_get_native_buffer(void * impl, GraphicsApiId api, BufferHandle buffer, void * outNativeImport, Error * error) noexcept
	{
		if (api != MetalApi::kId)
		{
			return fail(error, ErrorCode::eUnsupportedApi, "export payload API does not match the device backend");
		}

		auto * device = static_cast<MetalDevice *>(impl);

		const auto * tracked = device->buffers.resolve(buffer, kHandleAlreadyChecked);
		if (tracked == nullptr)
		{
			return fail(error, ErrorCode::eInvalidHandle, "export of a buffer this device never created");
		}

		static_cast<NativeBuffer<MetalApi> *>(outNativeImport)->buffer = tracked->buffer.get();
		return succeed(error);
	}

	bool metal_get_native_texture(
		[[maybe_unused]] void * impl,
		[[maybe_unused]] GraphicsApiId api,
		[[maybe_unused]] TextureHandle texture,
		[[maybe_unused]] void * outNativeImport,
		Error * error
	) noexcept
	{
		return fail(error, ErrorCode::eUnsupportedFeature, "Metal texture export is not implemented yet");
	}

	AccelerationStructureHandle metal_create_acceleration_structure(
		[[maybe_unused]] void * impl,
		[[maybe_unused]] const AccelerationStructureDesc & desc,
		Error * error
	) noexcept
	{
		return fail_value<AccelerationStructureHandle>(error, ErrorCode::eUnsupportedFeature, "Metal RHI backend does not support ray tracing");
	}

	RayTracingPipelineHandle metal_create_ray_tracing_pipeline(
		[[maybe_unused]] void * impl,
		[[maybe_unused]] const RayTracingPipelineDesc & desc,
		Error * error
	) noexcept
	{
		return fail_value<RayTracingPipelineHandle>(error, ErrorCode::eUnsupportedFeature, "Metal RHI backend does not support ray tracing");
	}

	bool metal_begin_native_mutation([[maybe_unused]] void * impl, GraphicsApiId api, [[maybe_unused]] const NativeMutationDesc & desc, Error * error) noexcept
	{
		if (api != MetalApi::kId)
		{
			return fail(error, ErrorCode::eUnsupportedApi, "native mutation API does not match the device backend");
		}

		return succeed(error);
	}

	TextureViewHandle metal_adopt_texture_view(
		void * impl,
		const GraphicsApiId api,
		const void * nativeImport,
		const AdoptedTextureViewDesc & desc,
		Error * error
	) noexcept
	{
		if (api != MetalApi::kId)
		{
			return fail_value<TextureViewHandle>(error, ErrorCode::eUnsupportedApi, "adoption payload API does not match the device backend");
		}

		MTL::Texture * adopted = static_cast<const NativeTextureView<MetalApi> *>(nativeImport)->texture;
		if (adopted == nullptr)
		{
			return fail_value<TextureViewHandle>(error, ErrorCode::eInvalidArgument, "adoption payload has a null MTLTexture");
		}

		auto * device = static_cast<MetalDevice *>(impl);
		if (device->textures.resolve(desc.texture, kHandleAlreadyChecked) == nullptr)
		{
			return fail_value<TextureViewHandle>(error, ErrorCode::eInvalidHandle, "an adopted texture view names a texture this device never handed out");
		}

		const TextureViewHandle handle = device->textureViews.store(MetalTextureViewSlot{ .texture = NS::RetainPtr(adopted) });
		if (!handle.is_valid())
		{
			return fail_value<TextureViewHandle>(error, ErrorCode::eOutOfHostMemory, "Metal adopted texture view tracking failed");
		}

		set_metal_label(adopted, desc.debugName);
		return return_value(handle, error);
	}

	SamplerHandle metal_adopt_sampler(void * impl, const GraphicsApiId api, const void * nativeImport, const AdoptedSamplerDesc & desc, Error * error) noexcept
	{
		if (api != MetalApi::kId)
		{
			return fail_value<SamplerHandle>(error, ErrorCode::eUnsupportedApi, "adoption payload API does not match the device backend");
		}

		MTL::SamplerState * adopted = static_cast<const NativeSampler<MetalApi> *>(nativeImport)->sampler;
		if (adopted == nullptr)
		{
			return fail_value<SamplerHandle>(error, ErrorCode::eInvalidArgument, "adoption payload has a null MTLSamplerState");
		}

		auto * device			   = static_cast<MetalDevice *>(impl);
		const SamplerHandle handle = device->samplers.store(NS::RetainPtr(adopted));
		if (!handle.is_valid())
		{
			return fail_value<SamplerHandle>(error, ErrorCode::eOutOfHostMemory, "Metal adopted sampler tracking failed");
		}

		static_cast<void>(desc);
		return return_value(handle, error);
	}

	bool metal_get_native_texture_view(void * impl, const GraphicsApiId api, const TextureViewHandle view, void * outNativeImport, Error * error) noexcept
	{
		if (api != MetalApi::kId)
		{
			return fail(error, ErrorCode::eUnsupportedApi, "native payload API does not match the device backend");
		}

		auto * device	  = static_cast<MetalDevice *>(impl);
		const auto * slot = device->textureViews.resolve(view, kHandleAlreadyChecked);
		if (slot == nullptr)
		{
			return fail(error, ErrorCode::eInvalidHandle, "native read of an invalid texture view handle");
		}

		static_cast<NativeTextureView<MetalApi> *>(outNativeImport)->texture = slot->texture.get();
		return succeed(error);
	}

	bool metal_get_native_sampler(void * impl, const GraphicsApiId api, const SamplerHandle sampler, void * outNativeImport, Error * error) noexcept
	{
		if (api != MetalApi::kId)
		{
			return fail(error, ErrorCode::eUnsupportedApi, "native payload API does not match the device backend");
		}

		auto * device	  = static_cast<MetalDevice *>(impl);
		const auto * slot = device->samplers.resolve(sampler, kHandleAlreadyChecked);
		if (slot == nullptr)
		{
			return fail(error, ErrorCode::eInvalidHandle, "native read of an invalid sampler handle");
		}

		static_cast<NativeSampler<MetalApi> *>(outNativeImport)->sampler = slot->get();
		return succeed(error);
	}

	TimelineHandle metal_adopt_timeline(
		void * impl,
		const GraphicsApiId api,
		const void * nativeImport,
		const AdoptedTimelineDesc & desc,
		Error * error
	) noexcept
	{
		if (api != MetalApi::kId)
		{
			return fail_value<TimelineHandle>(error, ErrorCode::eUnsupportedApi, "adoption payload API does not match the device backend");
		}

		MTL::SharedEvent * adopted = static_cast<const NativeTimeline<MetalApi> *>(nativeImport)->event;
		if (adopted == nullptr)
		{
			return fail_value<TimelineHandle>(error, ErrorCode::eInvalidArgument, "adoption payload has a null MTLSharedEvent");
		}

		auto * device				= static_cast<MetalDevice *>(impl);
		const TimelineHandle handle = device->timelines.store(MetalTimeline{ .event = NS::RetainPtr(adopted) });
		if (!handle.is_valid())
		{
			return fail_value<TimelineHandle>(error, ErrorCode::eOutOfHostMemory, "Metal adopted timeline tracking failed");
		}

		static_cast<void>(desc);
		return return_value(handle, error);
	}

	BinarySemaphoreHandle metal_adopt_binary_semaphore(
		void * impl,
		const GraphicsApiId api,
		const void * nativeImport,
		const AdoptedBinarySemaphoreDesc & desc,
		Error * error
	) noexcept
	{
		if (api != MetalApi::kId)
		{
			return fail_value<BinarySemaphoreHandle>(error, ErrorCode::eUnsupportedApi, "adoption payload API does not match the device backend");
		}

		MTL::SharedEvent * adopted = static_cast<const NativeBinarySemaphore<MetalApi> *>(nativeImport)->event;
		if (adopted == nullptr)
		{
			return fail_value<BinarySemaphoreHandle>(error, ErrorCode::eInvalidArgument, "adoption payload has a null MTLSharedEvent");
		}

		auto * device					   = static_cast<MetalDevice *>(impl);
		const BinarySemaphoreHandle handle = device->binarySemaphores.store(MetalBinarySemaphore{ .event = NS::RetainPtr(adopted), .value = 0 });
		if (!handle.is_valid())
		{
			return fail_value<BinarySemaphoreHandle>(error, ErrorCode::eOutOfHostMemory, "Metal adopted binary semaphore tracking failed");
		}

		static_cast<void>(desc);
		return return_value(handle, error);
	}

	bool metal_get_native_timeline(void * impl, const GraphicsApiId api, const TimelineHandle timeline, void * outNativeImport, Error * error) noexcept
	{
		if (api != MetalApi::kId)
		{
			return fail(error, ErrorCode::eUnsupportedApi, "native payload API does not match the device backend");
		}

		auto * device	  = static_cast<MetalDevice *>(impl);
		const auto * slot = device->timelines.resolve(timeline, kHandleAlreadyChecked);
		if (slot == nullptr)
		{
			return fail(error, ErrorCode::eInvalidHandle, "native read of an invalid timeline handle");
		}

		static_cast<NativeTimeline<MetalApi> *>(outNativeImport)->event = slot->event.get();
		return succeed(error);
	}

	bool metal_get_native_binary_semaphore(
		void * impl,
		const GraphicsApiId api,
		const BinarySemaphoreHandle semaphore,
		void * outNativeImport,
		Error * error
	) noexcept
	{
		if (api != MetalApi::kId)
		{
			return fail(error, ErrorCode::eUnsupportedApi, "native payload API does not match the device backend");
		}

		auto * device	  = static_cast<MetalDevice *>(impl);
		const auto * slot = device->binarySemaphores.resolve(semaphore, kHandleAlreadyChecked);
		if (slot == nullptr)
		{
			return fail(error, ErrorCode::eInvalidHandle, "native read of an invalid binary semaphore handle");
		}

		static_cast<NativeBinarySemaphore<MetalApi> *>(outNativeImport)->event = slot->event.get();
		return succeed(error);
	}

}

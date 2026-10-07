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

#include "azoth/rhi/backend/allocation_tracker.hpp"
#include "azoth/rhi/backend/dispatch.hpp"
#include "azoth/rhi/backend/support/host_containers.hpp"
#include "azoth/rhi/backend/table_validation.hpp"
#include "azoth/rhi/core/c_string.hpp"
#include "azoth/rhi/core/handle.hpp"
#include "azoth/rhi/host/allocator.hpp"
#include "azoth/rhi/native/native_access.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <span>
#include <string_view>

namespace azo::rhi
{

	namespace
	{

		void fail(Error * error, const ErrorCode code, const char * message) noexcept
		{
			if (error != nullptr)
			{
				*error = Error{
					.code	 = code,
					.message = message,
				};
			}
		}

		constexpr const char * kNoPresent			= "this backend cannot create swapchains";
		constexpr const char * kNoPlacedMemory		= "this backend cannot place resources into memory the caller granted";
		constexpr const char * kNoRayTracing		= "this backend has no ray tracing";
		constexpr const char * kNoQuery				= "this backend has no timestamp or occlusion queries";
		constexpr const char * kNoPipelineCache		= "this backend cannot serialize compiled pipelines";
		constexpr const char * kNoResidency			= "this backend reports no memory budget and takes no residency priority";
		constexpr const char * kNoIntrospection		= "this backend does not report what a resource was created with";
		constexpr const char * kNoAdoption			= "this backend cannot take a native object made on it, nor hand back the one a handle stands for";
		constexpr const char * kNoExternalSharing	= "this backend cannot move memory or synchronization across an API, device, or process boundary";
		constexpr const char * kNoSparse			= "this backend cannot bind sparse resources";
		constexpr const char * kNoAliasing			= "this backend cannot place resources, so it has nothing to alias";
		constexpr const char * kNoRayTracingCommand = "this backend has no ray tracing to record";
		constexpr const char * kNoQueryCommand		= "this backend has no queries to record";
		constexpr const char * kNoIndirect			= "this backend cannot draw or dispatch from a buffer";
		constexpr const char * kNoIndirectCount		= "this backend cannot take an indirect draw count from a buffer";
		constexpr const char * kNoNativeEscape		= "this backend hands out no native command buffer";

		template <class ValueT>
		[[nodiscard]] ValueT decline(Error * error, const char * what) noexcept
		{
			fail(error, ErrorCode::eUnsupportedFeature, what);
			return ValueT{};
		}

		template <class Tag>
		[[nodiscard]] bool Produced(const Handle<Tag> & handle) noexcept
		{
			return handle.is_valid();
		}

		[[nodiscard]] bool Produced(const bool answered) noexcept
		{
			return answered;
		}

		template <class T>
		[[nodiscard]] bool Produced(const T * block) noexcept
		{
			return block != nullptr;
		}

		[[nodiscard]] bool Produced(const MappedMemory & mapped) noexcept
		{
			return mapped.data != nullptr;
		}

		[[nodiscard]] bool Produced(const AcquireResult & acquired) noexcept
		{
			return acquired.status != SwapchainStatus::eError;
		}

		[[nodiscard]] bool Produced(const PresentResult & presented) noexcept
		{
			return presented.status != SwapchainStatus::eError;
		}

		template <class Tag>
		void Discard(Handle<Tag> & handle) noexcept
		{
			handle = {};
		}

		void Discard(bool & answered) noexcept
		{
			answered = false;
		}

		template <class T>
		void Discard(const T *& block) noexcept
		{
			block = nullptr;
		}

		void Discard(MappedMemory & mapped) noexcept
		{
			mapped = {};
		}

		void Discard(AcquireResult & acquired) noexcept
		{
			acquired = AcquireResult{ .status = SwapchainStatus::eError };
		}

		void Discard(PresentResult & presented) noexcept
		{
			presented = PresentResult{ .status = SwapchainStatus::eError };
		}

		template <class ValueT>
		void settle(ValueT & value, Error & error) noexcept
		{
			if (Produced(value) && error.code == ErrorCode::eOk)
			{
				return;
			}

			Discard(value);

			if (error.code == ErrorCode::eOk)
			{
				error = Error{
					.code	 = ErrorCode::eUnknown,
					.message = "the backend produced nothing and reported no reason",
				};
			}
		}

		template <class ValueT>
		[[nodiscard]] Result<ValueT> as_result(ValueT value, const Error & error) noexcept
		{
			if (error.code != ErrorCode::eOk)
			{
				return error;
			}

			return value;
		}

		[[nodiscard]] bool reserve_span(
			Device device,
			DeviceMemoryAllocator * allocator,
			const MemoryInfo & info,
			const HeapType heapType,
			const bool forBuffer,
			CString debugName,
			MemorySpan & out,
			Error * error
		)
		{
			const MemoryRequest request{
				.size		   = info.size,
				.alignment	   = info.alignment,
				.heapType	   = heapType,
				.forBuffer	   = forBuffer,
				.forTexture	   = !forBuffer,
				.allowAliasing = false,
				.debugName	   = debugName,
			};

			detail::check_no_guard_held();

			if (!allocator->allocate(device, request, out) || !out.is_valid())
			{
				fail(error, ErrorCode::eOutOfDeviceMemory, "the installed device memory allocator refused the request");
				return false;
			}

			if (info.alignment != 0 && out.offset % info.alignment != 0)
			{
				detail::check_no_guard_held();
				allocator->free(device, out);
				out = {};
				fail(error, ErrorCode::eInvalidArgument, "the device memory allocator returned an offset that does not satisfy the requested alignment");
				return false;
			}

			if (out.size < info.size)
			{
				detail::check_no_guard_held();
				allocator->free(device, out);
				out = {};
				fail(error, ErrorCode::eOutOfDeviceMemory, "the device memory allocator returned a span smaller than the resource needs");
				return false;
			}

			return true;
		}

		[[nodiscard]] bool record_span(
			BackendBlockSet & blocks,
			const ResourceType type,
			const std::uint32_t index,
			const std::uint32_t generation,
			const MemorySpan & span
		) noexcept
		{
			return blocks.tracker().record(
				type,
				RawHandle{
					.index		= index,
					.generation = generation,
				},
				span
			);
		}

		[[nodiscard]] bool take_retired_span(
			BackendBlockSet & blocks,
			const ResourceType type,
			const std::uint32_t index,
			const std::uint32_t generation,
			const DestroyDesc & desc,
			MemorySpan & out
		)
		{
			if (blocks.allocator() == nullptr)
			{
				return false;
			}

			return blocks.tracker().retire(
				type,
				RawHandle{
					.index		= index,
					.generation = generation,
				},
				desc,
				out
			);
		}

		template <class Collect>
		[[nodiscard]] bool collect_every_kind(BackendBlockSet & blocks, Error * error, Collect collect) noexcept
		{
			bool collected = true;
			for (std::size_t kind = 0; kind < kResourceTypeCount; ++kind)
			{
				const auto type = static_cast<ResourceType>(kind);

				Error kindError{};
				const std::scoped_lock guard(blocks.guard(type));
				if (!collect(type, error != nullptr ? &kindError : nullptr))
				{
					if (collected && error != nullptr)
					{
						*error = kindError;
					}
					collected = false;
				}
			}
			return collected;
		}

		void free_spans(Device device, DeviceMemoryAllocator * allocator, const detail::HostVector<MemorySpan> & spans) noexcept
		{
			if (allocator == nullptr)
			{
				return;
			}

			detail::check_no_guard_held();

			for (const MemorySpan & span : spans)
			{
				allocator->free(device, span);
			}
		}

	}

	void UniqueInstance::Reset() noexcept
	{
		if (m_impl != nullptr)
		{
			const detail::LifetimeLock lifetime;
			m_dispatch->destroyInstance(m_impl);
		}

		m_impl = nullptr;
	}

	GraphicsApiId Instance::get_graphics_api_id() const noexcept
	{
		return m_dispatch->getGraphicsApiId(m_impl);
	}

	bool Instance::enumerate_adapters(std::span<AdapterInfo> adapters, std::uint32_t & out) const noexcept
	{
		Error error{};
		return enumerate_adapters(adapters, out, error);
	}

	bool Instance::enumerate_adapters(std::span<AdapterInfo> adapters, std::uint32_t & out, Error & error) const noexcept
	{
		out	  = 0;
		error = {};

		bool answered = m_dispatch->enumerateAdapters(m_impl, adapters, &out, &error);
		settle(answered, error);
		if (!answered)
		{
			out = {};
		}

		return answered;
	}

	Result<std::uint32_t> Instance::enumerate_adapters_with_result(std::span<AdapterInfo> adapters) const noexcept
	{
		std::uint32_t out = 0;
		Error error{};
		static_cast<void>(enumerate_adapters(adapters, out, error));
		return as_result(out, error);
	}

	bool Instance::query_external_handle_support(const ExternalHandleSupportDesc & desc, ExternalHandleSupport & out) const noexcept
	{
		Error error{};
		return query_external_handle_support(desc, out, error);
	}

	bool Instance::query_external_handle_support(const ExternalHandleSupportDesc & desc, ExternalHandleSupport & out, Error & error) const noexcept
	{
		out	  = {};
		error = {};

		const auto * block = detail::query_block<ExternalCapabilityApi>(m_impl);
		if (block == nullptr || block->queryExternalHandleSupport == nullptr)
		{
			return true;
		}

		bool answered = block->queryExternalHandleSupport(m_impl, desc, &out, &error);
		settle(answered, error);

		if (!answered)
		{
			out = {};
		}

		return answered;
	}

	Result<ExternalHandleSupport> Instance::query_external_handle_support_with_result(const ExternalHandleSupportDesc & desc) const noexcept
	{
		ExternalHandleSupport out{};
		Error error{};
		static_cast<void>(query_external_handle_support(desc, out, error));
		return as_result(out, error);
	}

	GraphicsApiId Device::get_graphics_api_id() const noexcept
	{
		return m_blocks->device().core->getGraphicsApiId(m_impl);
	}

	std::string_view Device::get_graphics_api_name() const noexcept
	{
		return m_blocks->device().core->getGraphicsApiName(m_impl);
	}

	BufferHandle Device::create_buffer(const BufferDesc & desc) noexcept
	{
		Error error{};
		return create_buffer(desc, error);
	}

	BufferHandle Device::create_buffer(const BufferDesc & desc, Error & error) noexcept
	{
		error = {};

		BufferHandle produced = CreateBufferRouted(desc, &error);
		settle(produced, error);
		return produced;
	}

	BufferHandle Device::CreateBufferRouted(const BufferDesc & desc, Error * error) noexcept
	{
		if (!m_blocks->allocates_placed())
		{
			const std::scoped_lock guard(m_blocks->guard(ResourceType::eBuffer));
			return m_blocks->device().core->createBuffer(m_impl, desc, error);
		}

		MemoryInfo info{};
		{
			const std::scoped_lock guard(m_blocks->guard(ResourceType::eBuffer));
			if (!m_blocks->device().placedMemory->getBufferMemoryInfo(m_impl, desc, &info, error))
			{
				return {};
			}
		}

		MemorySpan span{};
		if (!reserve_span(*this, m_blocks->allocator(), info, heap_type_for_usage(desc.memory), true, desc.debugName, span, error))
		{
			return {};
		}

		const PlacedBufferDesc placed{
			.buffer = desc,
			.heap	= span.heap,
			.offset = span.offset,
		};

		BufferHandle handle{};
		{
			const std::scoped_lock guard(m_blocks->guard(ResourceType::eBuffer));
			handle = m_blocks->device().placedMemory->createPlacedBuffer(m_impl, placed, error);

			if (handle.is_valid() && !record_span(*m_blocks, ResourceType::eBuffer, handle.index, handle.generation, span))
			{
				static_cast<void>(m_blocks->device().core->destroy(
					m_impl,
					ResourceType::eBuffer,
					{
						.index		= handle.index,
						.generation = handle.generation,
					},
					DestroyDesc{},
					nullptr
				));
				handle = {};
				fail(error, ErrorCode::eOutOfHostMemory, "the allocation tracker could not record the span backing this buffer");
			}
		}

		if (!handle.is_valid())
		{
			detail::check_no_guard_held();
			m_blocks->allocator()->free(*this, span);
		}

		return handle;
	}

	Result<BufferHandle> Device::create_buffer_with_result(const BufferDesc & desc) noexcept
	{
		Error error{};
		return as_result(create_buffer(desc, error), error);
	}

	TextureHandle Device::create_texture(const TextureDesc & desc) noexcept
	{
		Error error{};
		return create_texture(desc, error);
	}

	TextureHandle Device::create_texture(const TextureDesc & desc, Error & error) noexcept
	{
		error = {};

		TextureHandle produced = CreateTextureRouted(desc, &error);
		settle(produced, error);
		return produced;
	}

	TextureHandle Device::CreateTextureRouted(const TextureDesc & desc, Error * error) noexcept
	{
		if (!m_blocks->allocates_placed())
		{
			const std::scoped_lock guard(m_blocks->guard(ResourceType::eTexture));
			return m_blocks->device().core->createTexture(m_impl, desc, error);
		}

		MemoryInfo info{};
		{
			const std::scoped_lock guard(m_blocks->guard(ResourceType::eTexture));
			if (!m_blocks->device().placedMemory->getTextureMemoryInfo(m_impl, desc, &info, error))
			{
				return {};
			}
		}

		MemorySpan span{};
		if (!reserve_span(*this, m_blocks->allocator(), info, heap_type_for_usage(desc.memory), false, desc.debugName, span, error))
		{
			return {};
		}

		const PlacedTextureDesc placed{
			.texture = desc,
			.heap	 = span.heap,
			.offset	 = span.offset,
		};

		TextureHandle handle{};
		{
			const std::scoped_lock guard(m_blocks->guard(ResourceType::eTexture));
			handle = m_blocks->device().placedMemory->createPlacedTexture(m_impl, placed, error);

			if (handle.is_valid() && !record_span(*m_blocks, ResourceType::eTexture, handle.index, handle.generation, span))
			{
				static_cast<void>(m_blocks->device().core->destroy(
					m_impl,
					ResourceType::eTexture,
					{
						.index		= handle.index,
						.generation = handle.generation,
					},
					DestroyDesc{},
					nullptr
				));
				handle = {};
				fail(error, ErrorCode::eOutOfHostMemory, "the allocation tracker could not record the span backing this texture");
			}
		}

		if (!handle.is_valid())
		{
			detail::check_no_guard_held();
			m_blocks->allocator()->free(*this, span);
		}

		return handle;
	}

	Result<TextureHandle> Device::create_texture_with_result(const TextureDesc & desc) noexcept
	{
		Error error{};
		return as_result(create_texture(desc, error), error);
	}

	TextureViewHandle Device::create_texture_view(TextureHandle texture, const TextureViewDesc & desc) noexcept
	{
		Error error{};
		return create_texture_view(texture, desc, error);
	}

	TextureViewHandle Device::create_texture_view(TextureHandle texture, const TextureViewDesc & desc, Error & error) noexcept
	{
		error = {};
		const std::scoped_lock guard(m_blocks->guard(ResourceType::eTextureView));
		TextureViewHandle produced = m_blocks->device().core->createTextureView(m_impl, texture, desc, &error);
		settle(produced, error);
		return produced;
	}

	Result<TextureViewHandle> Device::create_texture_view_with_result(TextureHandle texture, const TextureViewDesc & desc) noexcept
	{
		Error error{};
		return as_result(create_texture_view(texture, desc, error), error);
	}

	SamplerHandle Device::create_sampler(const SamplerDesc & desc) noexcept
	{
		Error error{};
		return create_sampler(desc, error);
	}

	SamplerHandle Device::create_sampler(const SamplerDesc & desc, Error & error) noexcept
	{
		error = {};
		const std::scoped_lock guard(m_blocks->guard(ResourceType::eSampler));
		SamplerHandle produced = m_blocks->device().core->createSampler(m_impl, desc, &error);
		settle(produced, error);
		return produced;
	}

	Result<SamplerHandle> Device::create_sampler_with_result(const SamplerDesc & desc) noexcept
	{
		Error error{};
		return as_result(create_sampler(desc, error), error);
	}

	HeapHandle Device::create_heap(const HeapDesc & desc) noexcept
	{
		Error error{};
		return create_heap(desc, error);
	}

	HeapHandle Device::create_heap(const HeapDesc & desc, Error & error) noexcept
	{
		error = {};
		if (m_blocks->device().placedMemory == nullptr)
		{
			return decline<HeapHandle>(&error, kNoPlacedMemory);
		}

		const std::scoped_lock guard(m_blocks->guard(ResourceType::eHeap));
		HeapHandle produced = m_blocks->device().placedMemory->createHeap(m_impl, desc, &error);
		settle(produced, error);
		return produced;
	}

	Result<HeapHandle> Device::create_heap_with_result(const HeapDesc & desc) noexcept
	{
		Error error{};
		return as_result(create_heap(desc, error), error);
	}

	BufferHandle Device::create_placed_buffer(const PlacedBufferDesc & desc) noexcept
	{
		Error error{};
		return create_placed_buffer(desc, error);
	}

	BufferHandle Device::create_placed_buffer(const PlacedBufferDesc & desc, Error & error) noexcept
	{
		error = {};
		if (m_blocks->device().placedMemory == nullptr)
		{
			return decline<BufferHandle>(&error, kNoPlacedMemory);
		}

		const std::scoped_lock guard(m_blocks->guard(ResourceType::eBuffer));
		BufferHandle produced = m_blocks->device().placedMemory->createPlacedBuffer(m_impl, desc, &error);
		settle(produced, error);
		return produced;
	}

	Result<BufferHandle> Device::create_placed_buffer_with_result(const PlacedBufferDesc & desc) noexcept
	{
		Error error{};
		return as_result(create_placed_buffer(desc, error), error);
	}

	TextureHandle Device::create_placed_texture(const PlacedTextureDesc & desc) noexcept
	{
		Error error{};
		return create_placed_texture(desc, error);
	}

	TextureHandle Device::create_placed_texture(const PlacedTextureDesc & desc, Error & error) noexcept
	{
		error = {};
		if (m_blocks->device().placedMemory == nullptr)
		{
			return decline<TextureHandle>(&error, kNoPlacedMemory);
		}

		const std::scoped_lock guard(m_blocks->guard(ResourceType::eTexture));
		TextureHandle produced = m_blocks->device().placedMemory->createPlacedTexture(m_impl, desc, &error);
		settle(produced, error);
		return produced;
	}

	Result<TextureHandle> Device::create_placed_texture_with_result(const PlacedTextureDesc & desc) noexcept
	{
		Error error{};
		return as_result(create_placed_texture(desc, error), error);
	}

	bool Device::get_texture_memory_info(const TextureDesc & desc, MemoryInfo & out) const noexcept
	{
		Error error{};
		return get_texture_memory_info(desc, out, error);
	}

	bool Device::get_texture_memory_info(const TextureDesc & desc, MemoryInfo & out, Error & error) const noexcept
	{
		out	  = {};
		error = {};

		if (m_blocks->device().placedMemory == nullptr)
		{
			return decline<bool>(&error, kNoPlacedMemory);
		}

		bool answered = m_blocks->device().placedMemory->getTextureMemoryInfo(m_impl, desc, &out, &error);
		settle(answered, error);
		if (!answered)
		{
			out = {};
		}

		return answered;
	}

	Result<MemoryInfo> Device::get_texture_memory_info_with_result(const TextureDesc & desc) const noexcept
	{
		MemoryInfo out{};
		Error error{};
		static_cast<void>(get_texture_memory_info(desc, out, error));
		return as_result(out, error);
	}

	bool Device::get_buffer_memory_info(const BufferDesc & desc, MemoryInfo & out) const noexcept
	{
		Error error{};
		return get_buffer_memory_info(desc, out, error);
	}

	bool Device::get_buffer_memory_info(const BufferDesc & desc, MemoryInfo & out, Error & error) const noexcept
	{
		out	  = {};
		error = {};

		if (m_blocks->device().placedMemory == nullptr)
		{
			return decline<bool>(&error, kNoPlacedMemory);
		}

		bool answered = m_blocks->device().placedMemory->getBufferMemoryInfo(m_impl, desc, &out, &error);
		settle(answered, error);
		if (!answered)
		{
			out = {};
		}

		return answered;
	}

	Result<MemoryInfo> Device::get_buffer_memory_info_with_result(const BufferDesc & desc) const noexcept
	{
		MemoryInfo out{};
		Error error{};
		static_cast<void>(get_buffer_memory_info(desc, out, error));
		return as_result(out, error);
	}

	DescriptorSetLayoutHandle Device::create_descriptor_set_layout(const DescriptorSetLayoutDesc & desc) noexcept
	{
		Error error{};
		return create_descriptor_set_layout(desc, error);
	}

	DescriptorSetLayoutHandle Device::create_descriptor_set_layout(const DescriptorSetLayoutDesc & desc, Error & error) noexcept
	{
		error = {};
		const std::scoped_lock guard(m_blocks->guard(ResourceType::eDescriptorSetLayout));
		DescriptorSetLayoutHandle produced = m_blocks->device().core->createDescriptorSetLayout(m_impl, desc, &error);
		settle(produced, error);
		return produced;
	}

	Result<DescriptorSetLayoutHandle> Device::create_descriptor_set_layout_with_result(const DescriptorSetLayoutDesc & desc) noexcept
	{
		Error error{};
		return as_result(create_descriptor_set_layout(desc, error), error);
	}

	PipelineLayoutHandle Device::create_pipeline_layout(const PipelineLayoutDesc & desc) noexcept
	{
		Error error{};
		return create_pipeline_layout(desc, error);
	}

	PipelineLayoutHandle Device::create_pipeline_layout(const PipelineLayoutDesc & desc, Error & error) noexcept
	{
		error = {};
		const std::scoped_lock guard(m_blocks->guard(ResourceType::ePipelineLayout));
		PipelineLayoutHandle produced = m_blocks->device().core->createPipelineLayout(m_impl, desc, &error);
		settle(produced, error);
		return produced;
	}

	Result<PipelineLayoutHandle> Device::create_pipeline_layout_with_result(const PipelineLayoutDesc & desc) noexcept
	{
		Error error{};
		return as_result(create_pipeline_layout(desc, error), error);
	}

	GraphicsPipelineHandle Device::create_graphics_pipeline(const GraphicsPipelineDesc & desc) noexcept
	{
		Error error{};
		return create_graphics_pipeline(desc, error);
	}

	GraphicsPipelineHandle Device::create_graphics_pipeline(const GraphicsPipelineDesc & desc, Error & error) noexcept
	{
		error = {};
		const std::scoped_lock guard(m_blocks->guard(ResourceType::eGraphicsPipeline));
		GraphicsPipelineHandle produced = m_blocks->device().core->createGraphicsPipeline(m_impl, desc, &error);
		settle(produced, error);
		return produced;
	}

	Result<GraphicsPipelineHandle> Device::create_graphics_pipeline_with_result(const GraphicsPipelineDesc & desc) noexcept
	{
		Error error{};
		return as_result(create_graphics_pipeline(desc, error), error);
	}

	ComputePipelineHandle Device::create_compute_pipeline(const ComputePipelineDesc & desc) noexcept
	{
		Error error{};
		return create_compute_pipeline(desc, error);
	}

	ComputePipelineHandle Device::create_compute_pipeline(const ComputePipelineDesc & desc, Error & error) noexcept
	{
		error = {};
		const std::scoped_lock guard(m_blocks->guard(ResourceType::eComputePipeline));
		ComputePipelineHandle produced = m_blocks->device().core->createComputePipeline(m_impl, desc, &error);
		settle(produced, error);
		return produced;
	}

	Result<ComputePipelineHandle> Device::create_compute_pipeline_with_result(const ComputePipelineDesc & desc) noexcept
	{
		Error error{};
		return as_result(create_compute_pipeline(desc, error), error);
	}

	RayTracingPipelineHandle Device::create_ray_tracing_pipeline(const RayTracingPipelineDesc & desc) noexcept
	{
		Error error{};
		return create_ray_tracing_pipeline(desc, error);
	}

	RayTracingPipelineHandle Device::create_ray_tracing_pipeline(const RayTracingPipelineDesc & desc, Error & error) noexcept
	{
		error = {};
		if (m_blocks->device().rayTracing == nullptr)
		{
			return decline<RayTracingPipelineHandle>(&error, kNoRayTracing);
		}

		const std::scoped_lock guard(m_blocks->guard(ResourceType::eRayTracingPipeline));
		RayTracingPipelineHandle produced = m_blocks->device().rayTracing->createRayTracingPipeline(m_impl, desc, &error);
		settle(produced, error);
		return produced;
	}

	Result<RayTracingPipelineHandle> Device::create_ray_tracing_pipeline_with_result(const RayTracingPipelineDesc & desc) noexcept
	{
		Error error{};
		return as_result(create_ray_tracing_pipeline(desc, error), error);
	}

	PipelineCacheHandle Device::create_pipeline_cache(const PipelineCacheDesc & desc) noexcept
	{
		Error error{};
		return create_pipeline_cache(desc, error);
	}

	PipelineCacheHandle Device::create_pipeline_cache(const PipelineCacheDesc & desc, Error & error) noexcept
	{
		error = {};
		if (m_blocks->device().pipelineCache == nullptr)
		{
			return decline<PipelineCacheHandle>(&error, kNoPipelineCache);
		}

		const std::scoped_lock guard(m_blocks->guard(ResourceType::ePipelineCache));
		PipelineCacheHandle produced = m_blocks->device().pipelineCache->createPipelineCache(m_impl, desc, &error);
		settle(produced, error);
		return produced;
	}

	Result<PipelineCacheHandle> Device::create_pipeline_cache_with_result(const PipelineCacheDesc & desc) noexcept
	{
		Error error{};
		return as_result(create_pipeline_cache(desc, error), error);
	}

	bool Device::get_pipeline_cache_data(PipelineCacheHandle cache, PipelineCacheData & out) noexcept
	{
		Error error{};
		return get_pipeline_cache_data(cache, out, error);
	}

	bool Device::get_pipeline_cache_data(PipelineCacheHandle cache, PipelineCacheData & out, Error & error) noexcept
	{
		out	  = {};
		error = {};

		if (m_blocks->device().pipelineCache == nullptr)
		{
			return decline<bool>(&error, kNoPipelineCache);
		}

		bool answered = m_blocks->device().pipelineCache->getPipelineCacheData(m_impl, cache, &out, &error);
		settle(answered, error);
		if (!answered)
		{
			out = {};
		}

		return answered;
	}

	Result<PipelineCacheData> Device::get_pipeline_cache_data_with_result(PipelineCacheHandle cache) noexcept
	{
		PipelineCacheData out{};
		Error error{};
		static_cast<void>(get_pipeline_cache_data(cache, out, error));
		return as_result(out, error);
	}

	AccelerationStructureHandle Device::create_acceleration_structure(const AccelerationStructureDesc & desc) noexcept
	{
		Error error{};
		return create_acceleration_structure(desc, error);
	}

	AccelerationStructureHandle Device::create_acceleration_structure(const AccelerationStructureDesc & desc, Error & error) noexcept
	{
		error = {};
		if (m_blocks->device().rayTracing == nullptr)
		{
			return decline<AccelerationStructureHandle>(&error, kNoRayTracing);
		}

		const std::scoped_lock guard(m_blocks->guard(ResourceType::eAccelerationStructure));
		AccelerationStructureHandle produced = m_blocks->device().rayTracing->createAccelerationStructure(m_impl, desc, &error);
		settle(produced, error);
		return produced;
	}

	Result<AccelerationStructureHandle> Device::create_acceleration_structure_with_result(const AccelerationStructureDesc & desc) noexcept
	{
		Error error{};
		return as_result(create_acceleration_structure(desc, error), error);
	}

	QueryPoolHandle Device::create_query_pool(const QueryPoolDesc & desc) noexcept
	{
		Error error{};
		return create_query_pool(desc, error);
	}

	QueryPoolHandle Device::create_query_pool(const QueryPoolDesc & desc, Error & error) noexcept
	{
		error = {};
		if (m_blocks->device().query == nullptr)
		{
			return decline<QueryPoolHandle>(&error, kNoQuery);
		}

		const std::scoped_lock guard(m_blocks->guard(ResourceType::eQueryPool));
		QueryPoolHandle produced = m_blocks->device().query->createQueryPool(m_impl, desc, &error);
		settle(produced, error);
		return produced;
	}

	Result<QueryPoolHandle> Device::create_query_pool_with_result(const QueryPoolDesc & desc) noexcept
	{
		Error error{};
		return as_result(create_query_pool(desc, error), error);
	}

	TimelineHandle Device::create_timeline(const TimelineDesc & desc) noexcept
	{
		Error error{};
		return create_timeline(desc, error);
	}

	TimelineHandle Device::create_timeline(const TimelineDesc & desc, Error & error) noexcept
	{
		error = {};
		const std::scoped_lock guard(m_blocks->guard(ResourceType::eTimeline));
		TimelineHandle produced = m_blocks->device().core->createTimeline(m_impl, desc, &error);
		settle(produced, error);
		return produced;
	}

	Result<TimelineHandle> Device::create_timeline_with_result(const TimelineDesc & desc) noexcept
	{
		Error error{};
		return as_result(create_timeline(desc, error), error);
	}

	BinarySemaphoreHandle Device::create_binary_semaphore(const BinarySemaphoreDesc & desc) noexcept
	{
		Error error{};
		return create_binary_semaphore(desc, error);
	}

	BinarySemaphoreHandle Device::create_binary_semaphore(const BinarySemaphoreDesc & desc, Error & error) noexcept
	{
		error = {};
		const std::scoped_lock guard(m_blocks->guard(ResourceType::eBinarySemaphore));
		BinarySemaphoreHandle produced = m_blocks->device().core->createBinarySemaphore(m_impl, desc, &error);
		settle(produced, error);
		return produced;
	}

	Result<BinarySemaphoreHandle> Device::create_binary_semaphore_with_result(const BinarySemaphoreDesc & desc) noexcept
	{
		Error error{};
		return as_result(create_binary_semaphore(desc, error), error);
	}

	DescriptorArena Device::create_descriptor_arena(const DescriptorArenaDesc & desc) noexcept
	{
		Error error{};
		return create_descriptor_arena(desc, error);
	}

	DescriptorArena Device::create_descriptor_arena(const DescriptorArenaDesc & desc, Error & error) noexcept
	{
		error = {};
		const std::scoped_lock guard(m_blocks->object_guard());
		void * impl		   = m_blocks->device().core->createDescriptorArena(m_impl, desc, &error);
		const auto * block = detail::checked_block<DescriptorArenaApi>(impl, &error);
		settle(block, error);

		return block != nullptr ? detail::FacadeBuilder::make_descriptor_arena(impl, block, m_blocks) : DescriptorArena{};
	}

	Result<DescriptorArena> Device::create_descriptor_arena_with_result(const DescriptorArenaDesc & desc) noexcept
	{
		Error error{};
		return as_result(create_descriptor_arena(desc, error), error);
	}

	CommandPool Device::create_command_pool(const CommandPoolDesc & desc) noexcept
	{
		Error error{};
		return create_command_pool(desc, error);
	}

	CommandPool Device::create_command_pool(const CommandPoolDesc & desc, Error & error) noexcept
	{
		error = {};
		const std::scoped_lock guard(m_blocks->object_guard());
		void * impl		   = m_blocks->device().core->createCommandPool(m_impl, desc, &error);
		const auto * block = detail::checked_block<CommandPoolApi>(impl, &error);
		settle(block, error);

		return block != nullptr ? detail::FacadeBuilder::make_command_pool(impl, block, m_blocks) : CommandPool{};
	}

	Result<CommandPool> Device::create_command_pool_with_result(const CommandPoolDesc & desc) noexcept
	{
		Error error{};
		return as_result(create_command_pool(desc, error), error);
	}

	Swapchain Device::create_swapchain(const SwapchainDesc & desc) noexcept
	{
		Error error{};
		return create_swapchain(desc, error);
	}

	Swapchain Device::create_swapchain(const SwapchainDesc & desc, Error & error) noexcept
	{
		error = {};
		if (m_blocks->device().present == nullptr)
		{
			return decline<Swapchain>(&error, kNoPresent);
		}

		const std::scoped_lock guard(m_blocks->object_guard());
		void * impl		   = m_blocks->device().present->createSwapchain(m_impl, desc, &error);
		const auto * block = detail::checked_block<SwapchainApi>(impl, &error);
		settle(block, error);

		return block != nullptr ? detail::FacadeBuilder::make_swapchain(impl, block) : Swapchain{};
	}

	Result<Swapchain> Device::create_swapchain_with_result(const SwapchainDesc & desc) noexcept
	{
		Error error{};
		return as_result(create_swapchain(desc, error), error);
	}

	Queue Device::get_queue(QueueType type, std::uint32_t index) noexcept
	{
		Error error{};
		return get_queue(type, index, error);
	}

	Queue Device::get_queue(QueueType type, std::uint32_t index, Error & error) noexcept
	{
		error = {};
		const std::scoped_lock guard(m_blocks->object_guard());
		void * impl				   = m_blocks->device().core->getQueue(m_impl, type, index, &error);
		const QueueBlocks * blocks = detail::checked_child<QueueApi>(impl, &error) ? m_blocks->queue(impl) : nullptr;
		settle(blocks, error);

		return blocks != nullptr ? detail::FacadeBuilder::make_queue(impl, blocks) : Queue{};
	}

	Result<Queue> Device::get_queue_with_result(QueueType type, std::uint32_t index) noexcept
	{
		Error error{};
		return as_result(get_queue(type, index, error), error);
	}

	std::uint32_t Device::get_queue_count(QueueType type) const noexcept
	{
		const DeviceCaps & caps = m_blocks->device().core->getCaps(m_impl);
		switch (type)
		{
		case QueueType::eGraphics: return caps.graphicsQueueCount;
		case QueueType::eCompute:  return caps.computeQueueCount;
		case QueueType::eCopy:	   return caps.copyQueueCount;
		}

		return 0;
	}

	MappedMemory Device::map(BufferHandle buffer, const MapDesc & desc) noexcept
	{
		Error error{};
		return map(buffer, desc, error);
	}

	MappedMemory Device::map(BufferHandle buffer, const MapDesc & desc, Error & error) noexcept
	{
		error				  = {};
		MappedMemory produced = m_blocks->device().core->map(m_impl, buffer, desc, &error);
		settle(produced, error);
		return produced;
	}

	Result<MappedMemory> Device::map_with_result(BufferHandle buffer, const MapDesc & desc) noexcept
	{
		Error error{};
		return as_result(map(buffer, desc, error), error);
	}

	bool Device::unmap(BufferHandle buffer) noexcept
	{
		return m_blocks->device().core->unmap(m_impl, buffer, nullptr);
	}

	bool Device::unmap(BufferHandle buffer, Error & error) noexcept
	{
		error = {};
		return m_blocks->device().core->unmap(m_impl, buffer, &error);
	}

	bool Device::flush_mapped_range(BufferHandle buffer, std::uint64_t offset, std::uint64_t size) noexcept
	{
		return m_blocks->device().core->flushMappedRange(m_impl, buffer, offset, size, nullptr);
	}

	bool Device::flush_mapped_range(BufferHandle buffer, std::uint64_t offset, std::uint64_t size, Error & error) noexcept
	{
		error = {};
		return m_blocks->device().core->flushMappedRange(m_impl, buffer, offset, size, &error);
	}

	bool Device::invalidate_mapped_range(BufferHandle buffer, std::uint64_t offset, std::uint64_t size) noexcept
	{
		return m_blocks->device().core->invalidateMappedRange(m_impl, buffer, offset, size, nullptr);
	}

	bool Device::invalidate_mapped_range(BufferHandle buffer, std::uint64_t offset, std::uint64_t size, Error & error) noexcept
	{
		error = {};
		return m_blocks->device().core->invalidateMappedRange(m_impl, buffer, offset, size, &error);
	}

	bool Device::update_descriptors(std::span<const DescriptorWriteBuffer> writes) noexcept
	{
		const std::scoped_lock guard(m_blocks->guard(ResourceType::eDescriptorSet));
		return m_blocks->device().core->updateDescriptorsBuffer(m_impl, writes, nullptr);
	}

	bool Device::update_descriptors(std::span<const DescriptorWriteBuffer> writes, Error & error) noexcept
	{
		error = {};
		const std::scoped_lock guard(m_blocks->guard(ResourceType::eDescriptorSet));
		return m_blocks->device().core->updateDescriptorsBuffer(m_impl, writes, &error);
	}

	bool Device::update_descriptors(std::span<const DescriptorWriteTexture> writes) noexcept
	{
		const std::scoped_lock guard(m_blocks->guard(ResourceType::eDescriptorSet));
		return m_blocks->device().core->updateDescriptorsTexture(m_impl, writes, nullptr);
	}

	bool Device::update_descriptors(std::span<const DescriptorWriteTexture> writes, Error & error) noexcept
	{
		error = {};
		const std::scoped_lock guard(m_blocks->guard(ResourceType::eDescriptorSet));
		return m_blocks->device().core->updateDescriptorsTexture(m_impl, writes, &error);
	}

	bool Device::update_descriptors(std::span<const DescriptorWriteSampler> writes) noexcept
	{
		const std::scoped_lock guard(m_blocks->guard(ResourceType::eDescriptorSet));
		return m_blocks->device().core->updateDescriptorsSampler(m_impl, writes, nullptr);
	}

	bool Device::update_descriptors(std::span<const DescriptorWriteSampler> writes, Error & error) noexcept
	{
		error = {};
		const std::scoped_lock guard(m_blocks->guard(ResourceType::eDescriptorSet));
		return m_blocks->device().core->updateDescriptorsSampler(m_impl, writes, &error);
	}

	bool Device::update_descriptors(std::span<const DescriptorWriteAccelerationStructure> writes) noexcept
	{
		if (m_blocks->device().rayTracing == nullptr)
		{
			return decline<bool>(nullptr, kNoRayTracing);
		}

		const std::scoped_lock guard(m_blocks->guard(ResourceType::eDescriptorSet));
		return m_blocks->device().rayTracing->updateDescriptorsAccelerationStructure(m_impl, writes, nullptr);
	}

	bool Device::update_descriptors(std::span<const DescriptorWriteAccelerationStructure> writes, Error & error) noexcept
	{
		error = {};
		if (m_blocks->device().rayTracing == nullptr)
		{
			return decline<bool>(&error, kNoRayTracing);
		}

		const std::scoped_lock guard(m_blocks->guard(ResourceType::eDescriptorSet));
		return m_blocks->device().rayTracing->updateDescriptorsAccelerationStructure(m_impl, writes, &error);
	}

	bool Device::query_memory_budget(HeapType heap, MemoryBudgetInfo & out) const noexcept
	{
		Error error{};
		return query_memory_budget(heap, out, error);
	}

	bool Device::query_memory_budget(HeapType heap, MemoryBudgetInfo & out, Error & error) const noexcept
	{
		out	  = {};
		error = {};

		if (m_blocks->device().residency == nullptr)
		{
			return decline<bool>(&error, kNoResidency);
		}

		bool answered = m_blocks->device().residency->queryMemoryBudget(m_impl, heap, &out, &error);
		settle(answered, error);
		if (!answered)
		{
			out = {};
		}

		return answered;
	}

	Result<MemoryBudgetInfo> Device::query_memory_budget_with_result(HeapType heap) const noexcept
	{
		MemoryBudgetInfo out{};
		Error error{};
		static_cast<void>(query_memory_budget(heap, out, error));
		return as_result(out, error);
	}

	bool Device::set_residency_priority(std::span<const ResidencyPriorityDesc> priorities) noexcept
	{
		return m_blocks->device().residency != nullptr ? m_blocks->device().residency->setResidencyPriority(m_impl, priorities, nullptr)
													   : decline<bool>(nullptr, kNoResidency);
	}

	bool Device::set_residency_priority(std::span<const ResidencyPriorityDesc> priorities, Error & error) noexcept
	{
		error = {};
		return m_blocks->device().residency != nullptr ? m_blocks->device().residency->setResidencyPriority(m_impl, priorities, &error)
													   : decline<bool>(&error, kNoResidency);
	}

	bool Device::calibrate_timestamp(QueueType queueType, TimestampCalibration & out) const noexcept
	{
		Error error{};
		return calibrate_timestamp(queueType, out, error);
	}

	bool Device::calibrate_timestamp(QueueType queueType, TimestampCalibration & out, Error & error) const noexcept
	{
		out	  = {};
		error = {};

		if (m_blocks->device().query == nullptr)
		{
			return decline<bool>(&error, kNoQuery);
		}

		bool answered = m_blocks->device().query->calibrateTimestamp(m_impl, queueType, &out, &error);
		settle(answered, error);
		if (!answered)
		{
			out = {};
		}

		return answered;
	}

	Result<TimestampCalibration> Device::calibrate_timestamp_with_result(QueueType queueType) const noexcept
	{
		TimestampCalibration out{};
		Error error{};
		static_cast<void>(calibrate_timestamp(queueType, out, error));
		return as_result(out, error);
	}

	const DeviceCaps & Device::get_caps() const noexcept
	{
		return m_blocks->caps();
	}

	FormatSupport Device::get_format_support(Format format) const noexcept
	{
		FormatSupport support = m_blocks->device().core->getFormatSupport(m_impl, format);

		if (!m_blocks->caps().supportsScaledBlit)
		{
			support.blitSrc = false;
			support.blitDst = false;
		}

		return support;
	}

	const AdapterInfo & Device::get_adapter_info() const noexcept
	{
		return m_blocks->device().core->getAdapterInfo(m_impl);
	}

	bool Device::get_texture_info(const TextureHandle texture, TextureInfo & out) const noexcept
	{
		Error error{};
		return get_texture_info(texture, out, error);
	}

	bool Device::get_texture_info(const TextureHandle texture, TextureInfo & out, Error & error) const noexcept
	{
		out	  = {};
		error = {};

		if (m_blocks->device().introspection == nullptr)
		{
			return decline<bool>(&error, kNoIntrospection);
		}

		bool answered = m_blocks->device().introspection->getTextureInfo(m_impl, texture, &out, &error);
		settle(answered, error);
		if (!answered)
		{
			out = {};
		}

		return answered;
	}

	Result<TextureInfo> Device::get_texture_info_with_result(const TextureHandle texture) const noexcept
	{
		TextureInfo out{};
		Error error{};
		static_cast<void>(get_texture_info(texture, out, error));
		return as_result(out, error);
	}

	bool Device::get_buffer_info(const BufferHandle buffer, BufferInfo & out) const noexcept
	{
		Error error{};
		return get_buffer_info(buffer, out, error);
	}

	bool Device::get_buffer_info(const BufferHandle buffer, BufferInfo & out, Error & error) const noexcept
	{
		out	  = {};
		error = {};

		if (m_blocks->device().introspection == nullptr)
		{
			return decline<bool>(&error, kNoIntrospection);
		}

		bool answered = m_blocks->device().introspection->getBufferInfo(m_impl, buffer, &out, &error);
		settle(answered, error);
		if (!answered)
		{
			out = {};
		}

		return answered;
	}

	Result<BufferInfo> Device::get_buffer_info_with_result(const BufferHandle buffer) const noexcept
	{
		BufferInfo out{};
		Error error{};
		static_cast<void>(get_buffer_info(buffer, out, error));
		return as_result(out, error);
	}

	ValidationMessageCounts Device::get_validation_message_counts() const noexcept
	{
		return m_blocks->device().core->getValidationMessageCounts(m_impl);
	}

	bool Device::destroy(BufferHandle handle, const DestroyDesc & desc) noexcept
	{
		MemorySpan released{};
		bool hasSpan = false;

		bool destroyed = false;
		{
			const std::scoped_lock guard(m_blocks->guard(ResourceType::eBuffer));
			destroyed = m_blocks->device().core->destroy(
				m_impl,
				ResourceType::eBuffer,
				{
					.index		= handle.index,
					.generation = handle.generation,
				},
				desc,
				nullptr
			);
			if (destroyed)
			{
				hasSpan = take_retired_span(*m_blocks, ResourceType::eBuffer, handle.index, handle.generation, desc, released);
			}
		}

		if (hasSpan)
		{
			detail::check_no_guard_held();
			m_blocks->allocator()->free(*this, released);
		}

		return destroyed;
	}

	bool Device::destroy(BufferHandle handle, const DestroyDesc & desc, Error & error) noexcept
	{
		error = {};
		MemorySpan released{};
		bool hasSpan = false;

		bool destroyed = false;
		{
			const std::scoped_lock guard(m_blocks->guard(ResourceType::eBuffer));
			destroyed = m_blocks->device().core->destroy(
				m_impl,
				ResourceType::eBuffer,
				{
					.index		= handle.index,
					.generation = handle.generation,
				},
				desc,
				&error
			);
			if (destroyed)
			{
				hasSpan = take_retired_span(*m_blocks, ResourceType::eBuffer, handle.index, handle.generation, desc, released);
			}
		}

		if (hasSpan)
		{
			detail::check_no_guard_held();
			m_blocks->allocator()->free(*this, released);
		}

		return destroyed;
	}

	bool Device::destroy(TextureHandle handle, const DestroyDesc & desc) noexcept
	{
		MemorySpan released{};
		bool hasSpan = false;

		bool destroyed = false;
		{
			const std::scoped_lock guard(m_blocks->guard(ResourceType::eTexture));
			destroyed = m_blocks->device().core->destroy(
				m_impl,
				ResourceType::eTexture,
				{
					.index		= handle.index,
					.generation = handle.generation,
				},
				desc,
				nullptr
			);
			if (destroyed)
			{
				hasSpan = take_retired_span(*m_blocks, ResourceType::eTexture, handle.index, handle.generation, desc, released);
			}
		}

		if (hasSpan)
		{
			detail::check_no_guard_held();
			m_blocks->allocator()->free(*this, released);
		}

		return destroyed;
	}

	bool Device::destroy(TextureHandle handle, const DestroyDesc & desc, Error & error) noexcept
	{
		error = {};
		MemorySpan released{};
		bool hasSpan = false;

		bool destroyed = false;
		{
			const std::scoped_lock guard(m_blocks->guard(ResourceType::eTexture));
			destroyed = m_blocks->device().core->destroy(
				m_impl,
				ResourceType::eTexture,
				{
					.index		= handle.index,
					.generation = handle.generation,
				},
				desc,
				&error
			);
			if (destroyed)
			{
				hasSpan = take_retired_span(*m_blocks, ResourceType::eTexture, handle.index, handle.generation, desc, released);
			}
		}

		if (hasSpan)
		{
			detail::check_no_guard_held();
			m_blocks->allocator()->free(*this, released);
		}

		return destroyed;
	}

	bool Device::destroy(TextureViewHandle handle, const DestroyDesc & desc) noexcept
	{
		const std::scoped_lock guard(m_blocks->guard(ResourceType::eTextureView));
		return m_blocks->device().core->destroy(
			m_impl,
			ResourceType::eTextureView,
			{
				.index		= handle.index,
				.generation = handle.generation,
			},
			desc,
			nullptr
		);
	}

	bool Device::destroy(TextureViewHandle handle, const DestroyDesc & desc, Error & error) noexcept
	{
		error = {};
		const std::scoped_lock guard(m_blocks->guard(ResourceType::eTextureView));
		return m_blocks->device().core->destroy(
			m_impl,
			ResourceType::eTextureView,
			{
				.index		= handle.index,
				.generation = handle.generation,
			},
			desc,
			&error
		);
	}

	bool Device::destroy(SamplerHandle handle, const DestroyDesc & desc) noexcept
	{
		const std::scoped_lock guard(m_blocks->guard(ResourceType::eSampler));
		return m_blocks->device().core->destroy(
			m_impl,
			ResourceType::eSampler,
			{
				.index		= handle.index,
				.generation = handle.generation,
			},
			desc,
			nullptr
		);
	}

	bool Device::destroy(SamplerHandle handle, const DestroyDesc & desc, Error & error) noexcept
	{
		error = {};
		const std::scoped_lock guard(m_blocks->guard(ResourceType::eSampler));
		return m_blocks->device().core->destroy(
			m_impl,
			ResourceType::eSampler,
			{
				.index		= handle.index,
				.generation = handle.generation,
			},
			desc,
			&error
		);
	}

	bool Device::destroy(HeapHandle handle, const DestroyDesc & desc) noexcept
	{
		const std::scoped_lock guard(m_blocks->guard(ResourceType::eHeap));
		return m_blocks->device().core->destroy(
			m_impl,
			ResourceType::eHeap,
			{
				.index		= handle.index,
				.generation = handle.generation,
			},
			desc,
			nullptr
		);
	}

	bool Device::destroy(HeapHandle handle, const DestroyDesc & desc, Error & error) noexcept
	{
		error = {};
		const std::scoped_lock guard(m_blocks->guard(ResourceType::eHeap));
		return m_blocks->device().core->destroy(
			m_impl,
			ResourceType::eHeap,
			{
				.index		= handle.index,
				.generation = handle.generation,
			},
			desc,
			&error
		);
	}

	bool Device::destroy(DescriptorSetLayoutHandle handle, const DestroyDesc & desc) noexcept
	{
		const std::scoped_lock guard(m_blocks->guard(ResourceType::eDescriptorSetLayout));
		return m_blocks->device().core->destroy(
			m_impl,
			ResourceType::eDescriptorSetLayout,
			{
				.index		= handle.index,
				.generation = handle.generation,
			},
			desc,
			nullptr
		);
	}

	bool Device::destroy(DescriptorSetLayoutHandle handle, const DestroyDesc & desc, Error & error) noexcept
	{
		error = {};
		const std::scoped_lock guard(m_blocks->guard(ResourceType::eDescriptorSetLayout));
		return m_blocks->device().core->destroy(
			m_impl,
			ResourceType::eDescriptorSetLayout,
			{
				.index		= handle.index,
				.generation = handle.generation,
			},
			desc,
			&error
		);
	}

	bool Device::destroy(DescriptorSetHandle handle, const DestroyDesc & desc) noexcept
	{
		const std::scoped_lock guard(m_blocks->guard(ResourceType::eDescriptorSet));
		return m_blocks->device().core->destroy(
			m_impl,
			ResourceType::eDescriptorSet,
			{
				.index		= handle.index,
				.generation = handle.generation,
			},
			desc,
			nullptr
		);
	}

	bool Device::destroy(DescriptorSetHandle handle, const DestroyDesc & desc, Error & error) noexcept
	{
		error = {};
		const std::scoped_lock guard(m_blocks->guard(ResourceType::eDescriptorSet));
		return m_blocks->device().core->destroy(
			m_impl,
			ResourceType::eDescriptorSet,
			{
				.index		= handle.index,
				.generation = handle.generation,
			},
			desc,
			&error
		);
	}

	bool Device::destroy(PipelineLayoutHandle handle, const DestroyDesc & desc) noexcept
	{
		const std::scoped_lock guard(m_blocks->guard(ResourceType::ePipelineLayout));
		return m_blocks->device().core->destroy(
			m_impl,
			ResourceType::ePipelineLayout,
			{
				.index		= handle.index,
				.generation = handle.generation,
			},
			desc,
			nullptr
		);
	}

	bool Device::destroy(PipelineLayoutHandle handle, const DestroyDesc & desc, Error & error) noexcept
	{
		error = {};
		const std::scoped_lock guard(m_blocks->guard(ResourceType::ePipelineLayout));
		return m_blocks->device().core->destroy(
			m_impl,
			ResourceType::ePipelineLayout,
			{
				.index		= handle.index,
				.generation = handle.generation,
			},
			desc,
			&error
		);
	}

	bool Device::destroy(GraphicsPipelineHandle handle, const DestroyDesc & desc) noexcept
	{
		const std::scoped_lock guard(m_blocks->guard(ResourceType::eGraphicsPipeline));
		return m_blocks->device().core->destroy(
			m_impl,
			ResourceType::eGraphicsPipeline,
			{
				.index		= handle.index,
				.generation = handle.generation,
			},
			desc,
			nullptr
		);
	}

	bool Device::destroy(GraphicsPipelineHandle handle, const DestroyDesc & desc, Error & error) noexcept
	{
		error = {};
		const std::scoped_lock guard(m_blocks->guard(ResourceType::eGraphicsPipeline));
		return m_blocks->device().core->destroy(
			m_impl,
			ResourceType::eGraphicsPipeline,
			{
				.index		= handle.index,
				.generation = handle.generation,
			},
			desc,
			&error
		);
	}

	bool Device::destroy(ComputePipelineHandle handle, const DestroyDesc & desc) noexcept
	{
		const std::scoped_lock guard(m_blocks->guard(ResourceType::eComputePipeline));
		return m_blocks->device().core->destroy(
			m_impl,
			ResourceType::eComputePipeline,
			{
				.index		= handle.index,
				.generation = handle.generation,
			},
			desc,
			nullptr
		);
	}

	bool Device::destroy(ComputePipelineHandle handle, const DestroyDesc & desc, Error & error) noexcept
	{
		error = {};
		const std::scoped_lock guard(m_blocks->guard(ResourceType::eComputePipeline));
		return m_blocks->device().core->destroy(
			m_impl,
			ResourceType::eComputePipeline,
			{
				.index		= handle.index,
				.generation = handle.generation,
			},
			desc,
			&error
		);
	}

	bool Device::destroy(RayTracingPipelineHandle handle, const DestroyDesc & desc) noexcept
	{
		const std::scoped_lock guard(m_blocks->guard(ResourceType::eRayTracingPipeline));
		return m_blocks->device().core->destroy(
			m_impl,
			ResourceType::eRayTracingPipeline,
			{
				.index		= handle.index,
				.generation = handle.generation,
			},
			desc,
			nullptr
		);
	}

	bool Device::destroy(RayTracingPipelineHandle handle, const DestroyDesc & desc, Error & error) noexcept
	{
		error = {};
		const std::scoped_lock guard(m_blocks->guard(ResourceType::eRayTracingPipeline));
		return m_blocks->device().core->destroy(
			m_impl,
			ResourceType::eRayTracingPipeline,
			{
				.index		= handle.index,
				.generation = handle.generation,
			},
			desc,
			&error
		);
	}

	bool Device::destroy(PipelineCacheHandle handle, const DestroyDesc & desc) noexcept
	{
		const std::scoped_lock guard(m_blocks->guard(ResourceType::ePipelineCache));
		return m_blocks->device().core->destroy(
			m_impl,
			ResourceType::ePipelineCache,
			{
				.index		= handle.index,
				.generation = handle.generation,
			},
			desc,
			nullptr
		);
	}

	bool Device::destroy(PipelineCacheHandle handle, const DestroyDesc & desc, Error & error) noexcept
	{
		error = {};
		const std::scoped_lock guard(m_blocks->guard(ResourceType::ePipelineCache));
		return m_blocks->device().core->destroy(
			m_impl,
			ResourceType::ePipelineCache,
			{
				.index		= handle.index,
				.generation = handle.generation,
			},
			desc,
			&error
		);
	}

	bool Device::destroy(AccelerationStructureHandle handle, const DestroyDesc & desc) noexcept
	{
		const std::scoped_lock guard(m_blocks->guard(ResourceType::eAccelerationStructure));
		return m_blocks->device().core->destroy(
			m_impl,
			ResourceType::eAccelerationStructure,
			{
				.index		= handle.index,
				.generation = handle.generation,
			},
			desc,
			nullptr
		);
	}

	bool Device::destroy(AccelerationStructureHandle handle, const DestroyDesc & desc, Error & error) noexcept
	{
		error = {};
		const std::scoped_lock guard(m_blocks->guard(ResourceType::eAccelerationStructure));
		return m_blocks->device().core->destroy(
			m_impl,
			ResourceType::eAccelerationStructure,
			{
				.index		= handle.index,
				.generation = handle.generation,
			},
			desc,
			&error
		);
	}

	bool Device::destroy(QueryPoolHandle handle, const DestroyDesc & desc) noexcept
	{
		const std::scoped_lock guard(m_blocks->guard(ResourceType::eQueryPool));
		return m_blocks->device().core->destroy(
			m_impl,
			ResourceType::eQueryPool,
			{
				.index		= handle.index,
				.generation = handle.generation,
			},
			desc,
			nullptr
		);
	}

	bool Device::destroy(QueryPoolHandle handle, const DestroyDesc & desc, Error & error) noexcept
	{
		error = {};
		const std::scoped_lock guard(m_blocks->guard(ResourceType::eQueryPool));
		return m_blocks->device().core->destroy(
			m_impl,
			ResourceType::eQueryPool,
			{
				.index		= handle.index,
				.generation = handle.generation,
			},
			desc,
			&error
		);
	}

	bool Device::destroy(TimelineHandle handle, const DestroyDesc & desc) noexcept
	{
		const std::scoped_lock guard(m_blocks->guard(ResourceType::eTimeline));
		return m_blocks->device().core->destroy(
			m_impl,
			ResourceType::eTimeline,
			{
				.index		= handle.index,
				.generation = handle.generation,
			},
			desc,
			nullptr
		);
	}

	bool Device::destroy(TimelineHandle handle, const DestroyDesc & desc, Error & error) noexcept
	{
		error = {};
		const std::scoped_lock guard(m_blocks->guard(ResourceType::eTimeline));
		return m_blocks->device().core->destroy(
			m_impl,
			ResourceType::eTimeline,
			{
				.index		= handle.index,
				.generation = handle.generation,
			},
			desc,
			&error
		);
	}

	bool Device::destroy(BinarySemaphoreHandle handle, const DestroyDesc & desc) noexcept
	{
		const std::scoped_lock guard(m_blocks->guard(ResourceType::eBinarySemaphore));
		return m_blocks->device().core->destroy(
			m_impl,
			ResourceType::eBinarySemaphore,
			{
				.index		= handle.index,
				.generation = handle.generation,
			},
			desc,
			nullptr
		);
	}

	bool Device::destroy(BinarySemaphoreHandle handle, const DestroyDesc & desc, Error & error) noexcept
	{
		error = {};
		const std::scoped_lock guard(m_blocks->guard(ResourceType::eBinarySemaphore));
		return m_blocks->device().core->destroy(
			m_impl,
			ResourceType::eBinarySemaphore,
			{
				.index		= handle.index,
				.generation = handle.generation,
			},
			desc,
			&error
		);
	}

	bool Device::collect_garbage() noexcept
	{
		const CoreDeviceApi * core = m_blocks->device().core;

		detail::HostVector<MemorySpan> released;
		const bool collected = collect_every_kind(
			*m_blocks,
			nullptr,
			[&](const ResourceType type, Error * kindError) noexcept
			{
				m_blocks->tracker().take_all(type, released);
				return core->collectGarbage(m_impl, type, kindError);
			}
		);

		free_spans(*this, m_blocks->allocator(), released);
		return collected;
	}

	bool Device::collect_garbage(Error & error) noexcept
	{
		error					   = {};
		const CoreDeviceApi * core = m_blocks->device().core;

		detail::HostVector<MemorySpan> released;
		const bool collected = collect_every_kind(
			*m_blocks,
			&error,
			[&](const ResourceType type, Error * kindError) noexcept
			{
				m_blocks->tracker().take_all(type, released);
				return core->collectGarbage(m_impl, type, kindError);
			}
		);

		free_spans(*this, m_blocks->allocator(), released);
		return collected;
	}

	bool Device::collect_garbage(TimelineHandle timeline, std::uint64_t completedValue) noexcept
	{
		const CoreDeviceApi * core = m_blocks->device().core;

		detail::HostVector<MemorySpan> released;
		const bool collected = collect_every_kind(
			*m_blocks,
			nullptr,
			[&](const ResourceType type, Error * kindError) noexcept
			{
				m_blocks->tracker().take_releasable(type, timeline, completedValue, released);
				return core->collectGarbageTimeline(m_impl, type, timeline, completedValue, kindError);
			}
		);

		free_spans(*this, m_blocks->allocator(), released);
		return collected;
	}

	bool Device::collect_garbage(TimelineHandle timeline, std::uint64_t completedValue, Error & error) noexcept
	{
		error					   = {};
		const CoreDeviceApi * core = m_blocks->device().core;

		detail::HostVector<MemorySpan> released;
		const bool collected = collect_every_kind(
			*m_blocks,
			&error,
			[&](const ResourceType type, Error * kindError) noexcept
			{
				m_blocks->tracker().take_releasable(type, timeline, completedValue, released);
				return core->collectGarbageTimeline(m_impl, type, timeline, completedValue, kindError);
			}
		);

		free_spans(*this, m_blocks->allocator(), released);
		return collected;
	}

	BufferHandle Device::AdoptBufferRaw(GraphicsApiId api, const void * nativeImport, const AdoptedBufferDesc & desc, Error * error) noexcept
	{
		if (m_blocks->device().adoption == nullptr)
		{
			return decline<BufferHandle>(error, kNoAdoption);
		}

		const std::scoped_lock guard(m_blocks->guard(ResourceType::eBuffer));
		return m_blocks->device().adoption->adoptBuffer(m_impl, api, nativeImport, desc, error);
	}

	TextureHandle Device::AdoptTextureRaw(GraphicsApiId api, const void * nativeImport, const AdoptedTextureDesc & desc, Error * error) noexcept
	{
		if (m_blocks->device().adoption == nullptr)
		{
			return decline<TextureHandle>(error, kNoAdoption);
		}

		const std::scoped_lock guard(m_blocks->guard(ResourceType::eTexture));
		return m_blocks->device().adoption->adoptTexture(m_impl, api, nativeImport, desc, error);
	}

	bool Device::GetNativeBufferRaw(GraphicsApiId api, BufferHandle buffer, void * outNativeImport, Error * error) noexcept
	{
		return m_blocks->device().adoption != nullptr ? m_blocks->device().adoption->getNativeBuffer(m_impl, api, buffer, outNativeImport, error)
													  : decline<bool>(error, kNoAdoption);
	}

	bool Device::GetNativeTextureRaw(GraphicsApiId api, TextureHandle texture, void * outNativeImport, Error * error) noexcept
	{
		return m_blocks->device().adoption != nullptr ? m_blocks->device().adoption->getNativeTexture(m_impl, api, texture, outNativeImport, error)
													  : decline<bool>(error, kNoAdoption);
	}

	bool Device::export_buffer(BufferHandle buffer, ExternalHandleType type, ExternalHandle & out) noexcept
	{
		Error error{};
		return export_buffer(buffer, type, out, error);
	}

	bool Device::export_buffer(BufferHandle buffer, ExternalHandleType type, ExternalHandle & out, Error & error) noexcept
	{
		out	  = {};
		error = {};
		if (m_blocks->device().externalSharing == nullptr)
		{
			return decline<bool>(&error, kNoExternalSharing);
		}

		const std::scoped_lock guard(m_blocks->guard(ResourceType::eBuffer));
		bool exported = m_blocks->device().externalSharing->exportBuffer(m_impl, buffer, type, &out, &error);
		settle(exported, error);

		if (!exported)
		{
			out = {};
		}

		return exported;
	}

	Result<ExternalHandle> Device::export_buffer_with_result(BufferHandle buffer, ExternalHandleType type) noexcept
	{
		ExternalHandle out{};
		Error error{};
		static_cast<void>(export_buffer(buffer, type, out, error));
		return as_result(out, error);
	}

	bool Device::export_heap(HeapHandle heap, ExternalHandleType type, ExternalHandle & out) noexcept
	{
		Error error{};
		return export_heap(heap, type, out, error);
	}

	bool Device::export_heap(HeapHandle heap, ExternalHandleType type, ExternalHandle & out, Error & error) noexcept
	{
		out	  = {};
		error = {};
		if (m_blocks->device().externalSharing == nullptr)
		{
			return decline<bool>(&error, kNoExternalSharing);
		}

		const std::scoped_lock guard(m_blocks->guard(ResourceType::eHeap));
		bool exported = m_blocks->device().externalSharing->exportHeap(m_impl, heap, type, &out, &error);
		settle(exported, error);

		if (!exported)
		{
			out = {};
		}

		return exported;
	}

	Result<ExternalHandle> Device::export_heap_with_result(HeapHandle heap, ExternalHandleType type) noexcept
	{
		ExternalHandle out{};
		Error error{};
		static_cast<void>(export_heap(heap, type, out, error));
		return as_result(out, error);
	}

	bool Device::export_texture(TextureHandle texture, ExternalHandleType type, ExternalHandle & out) noexcept
	{
		Error error{};
		return export_texture(texture, type, out, error);
	}

	bool Device::export_texture(TextureHandle texture, ExternalHandleType type, ExternalHandle & out, Error & error) noexcept
	{
		out	  = {};
		error = {};
		if (m_blocks->device().externalSharing == nullptr)
		{
			return decline<bool>(&error, kNoExternalSharing);
		}

		const std::scoped_lock guard(m_blocks->guard(ResourceType::eTexture));
		bool exported = m_blocks->device().externalSharing->exportTexture(m_impl, texture, type, &out, &error);
		settle(exported, error);

		if (!exported)
		{
			out = {};
		}

		return exported;
	}

	Result<ExternalHandle> Device::export_texture_with_result(TextureHandle texture, ExternalHandleType type) noexcept
	{
		ExternalHandle out{};
		Error error{};
		static_cast<void>(export_texture(texture, type, out, error));
		return as_result(out, error);
	}

	bool Device::export_timeline(TimelineHandle timeline, ExternalHandleType type, ExternalHandle & out) noexcept
	{
		Error error{};
		return export_timeline(timeline, type, out, error);
	}

	bool Device::export_timeline(TimelineHandle timeline, ExternalHandleType type, ExternalHandle & out, Error & error) noexcept
	{
		out	  = {};
		error = {};
		if (m_blocks->device().externalSharing == nullptr)
		{
			return decline<bool>(&error, kNoExternalSharing);
		}

		const std::scoped_lock guard(m_blocks->guard(ResourceType::eTimeline));
		bool exported = m_blocks->device().externalSharing->exportTimeline(m_impl, timeline, type, &out, &error);
		settle(exported, error);

		if (!exported)
		{
			out = {};
		}

		return exported;
	}

	Result<ExternalHandle> Device::export_timeline_with_result(TimelineHandle timeline, ExternalHandleType type) noexcept
	{
		ExternalHandle out{};
		Error error{};
		static_cast<void>(export_timeline(timeline, type, out, error));
		return as_result(out, error);
	}

	bool Device::export_binary_semaphore(BinarySemaphoreHandle semaphore, ExternalHandleType type, ExternalHandle & out) noexcept
	{
		Error error{};
		return export_binary_semaphore(semaphore, type, out, error);
	}

	bool Device::export_binary_semaphore(BinarySemaphoreHandle semaphore, ExternalHandleType type, ExternalHandle & out, Error & error) noexcept
	{
		out	  = {};
		error = {};
		if (m_blocks->device().externalSharing == nullptr)
		{
			return decline<bool>(&error, kNoExternalSharing);
		}

		const std::scoped_lock guard(m_blocks->guard(ResourceType::eBinarySemaphore));
		bool exported = m_blocks->device().externalSharing->exportBinarySemaphore(m_impl, semaphore, type, &out, &error);
		settle(exported, error);

		if (!exported)
		{
			out = {};
		}

		return exported;
	}

	Result<ExternalHandle> Device::export_binary_semaphore_with_result(BinarySemaphoreHandle semaphore, ExternalHandleType type) noexcept
	{
		ExternalHandle out{};
		Error error{};
		static_cast<void>(export_binary_semaphore(semaphore, type, out, error));
		return as_result(out, error);
	}

	BufferHandle Device::import_buffer(const ExternalBufferImportDesc & desc) noexcept
	{
		Error error{};
		return import_buffer(desc, error);
	}

	BufferHandle Device::import_buffer(const ExternalBufferImportDesc & desc, Error & error) noexcept
	{
		error = {};
		if (m_blocks->device().externalSharing == nullptr)
		{
			return decline<BufferHandle>(&error, kNoExternalSharing);
		}

		const std::scoped_lock guard(m_blocks->guard(ResourceType::eBuffer));
		BufferHandle produced = m_blocks->device().externalSharing->importBuffer(m_impl, desc, &error);
		settle(produced, error);
		return produced;
	}

	Result<BufferHandle> Device::import_buffer_with_result(const ExternalBufferImportDesc & desc) noexcept
	{
		Error error{};
		return as_result(import_buffer(desc, error), error);
	}

	HeapHandle Device::import_heap(const ExternalHeapImportDesc & desc) noexcept
	{
		Error error{};
		return import_heap(desc, error);
	}

	HeapHandle Device::import_heap(const ExternalHeapImportDesc & desc, Error & error) noexcept
	{
		error = {};
		if (m_blocks->device().externalSharing == nullptr)
		{
			return decline<HeapHandle>(&error, kNoExternalSharing);
		}

		const std::scoped_lock guard(m_blocks->guard(ResourceType::eHeap));
		HeapHandle produced = m_blocks->device().externalSharing->importHeap(m_impl, desc, &error);
		settle(produced, error);
		return produced;
	}

	Result<HeapHandle> Device::import_heap_with_result(const ExternalHeapImportDesc & desc) noexcept
	{
		Error error{};
		return as_result(import_heap(desc, error), error);
	}

	TextureHandle Device::import_texture(const ExternalTextureImportDesc & desc) noexcept
	{
		Error error{};
		return import_texture(desc, error);
	}

	TextureHandle Device::import_texture(const ExternalTextureImportDesc & desc, Error & error) noexcept
	{
		error = {};
		if (m_blocks->device().externalSharing == nullptr)
		{
			return decline<TextureHandle>(&error, kNoExternalSharing);
		}

		const std::scoped_lock guard(m_blocks->guard(ResourceType::eTexture));
		TextureHandle produced = m_blocks->device().externalSharing->importTexture(m_impl, desc, &error);
		settle(produced, error);
		return produced;
	}

	Result<TextureHandle> Device::import_texture_with_result(const ExternalTextureImportDesc & desc) noexcept
	{
		Error error{};
		return as_result(import_texture(desc, error), error);
	}

	TimelineHandle Device::import_timeline(const ExternalTimelineImportDesc & desc) noexcept
	{
		Error error{};
		return import_timeline(desc, error);
	}

	TimelineHandle Device::import_timeline(const ExternalTimelineImportDesc & desc, Error & error) noexcept
	{
		error = {};
		if (m_blocks->device().externalSharing == nullptr)
		{
			return decline<TimelineHandle>(&error, kNoExternalSharing);
		}

		const std::scoped_lock guard(m_blocks->guard(ResourceType::eTimeline));
		TimelineHandle produced = m_blocks->device().externalSharing->importTimeline(m_impl, desc, &error);
		settle(produced, error);
		return produced;
	}

	Result<TimelineHandle> Device::import_timeline_with_result(const ExternalTimelineImportDesc & desc) noexcept
	{
		Error error{};
		return as_result(import_timeline(desc, error), error);
	}

	BinarySemaphoreHandle Device::import_binary_semaphore(const ExternalBinarySemaphoreImportDesc & desc) noexcept
	{
		Error error{};
		return import_binary_semaphore(desc, error);
	}

	BinarySemaphoreHandle Device::import_binary_semaphore(const ExternalBinarySemaphoreImportDesc & desc, Error & error) noexcept
	{
		error = {};
		if (m_blocks->device().externalSharing == nullptr)
		{
			return decline<BinarySemaphoreHandle>(&error, kNoExternalSharing);
		}

		const std::scoped_lock guard(m_blocks->guard(ResourceType::eBinarySemaphore));
		BinarySemaphoreHandle produced = m_blocks->device().externalSharing->importBinarySemaphore(m_impl, desc, &error);
		settle(produced, error);
		return produced;
	}

	Result<BinarySemaphoreHandle> Device::import_binary_semaphore_with_result(const ExternalBinarySemaphoreImportDesc & desc) noexcept
	{
		Error error{};
		return as_result(import_binary_semaphore(desc, error), error);
	}

	bool Device::close_exported_handle(const ExternalHandle & handle) noexcept
	{
		Error error{};
		return close_exported_handle(handle, error);
	}

	bool Device::close_exported_handle(const ExternalHandle & handle, Error & error) noexcept
	{
		error = {};
		if (m_blocks->device().externalSharing == nullptr)
		{
			return decline<bool>(&error, kNoExternalSharing);
		}

		bool closed = m_blocks->device().externalSharing->closeExportedHandle(m_impl, handle, &error);
		settle(closed, error);
		return closed;
	}

	TextureViewHandle Device::AdoptTextureViewRaw(GraphicsApiId api, const void * nativeImport, const AdoptedTextureViewDesc & desc, Error * error) noexcept
	{
		if (m_blocks->device().adoption == nullptr)
		{
			return decline<TextureViewHandle>(error, kNoAdoption);
		}

		const std::scoped_lock guard(m_blocks->guard(ResourceType::eTextureView));
		return m_blocks->device().adoption->adoptTextureView(m_impl, api, nativeImport, desc, error);
	}

	SamplerHandle Device::AdoptSamplerRaw(GraphicsApiId api, const void * nativeImport, const AdoptedSamplerDesc & desc, Error * error) noexcept
	{
		if (m_blocks->device().adoption == nullptr)
		{
			return decline<SamplerHandle>(error, kNoAdoption);
		}

		const std::scoped_lock guard(m_blocks->guard(ResourceType::eSampler));
		return m_blocks->device().adoption->adoptSampler(m_impl, api, nativeImport, desc, error);
	}

	bool Device::GetNativeTextureViewRaw(GraphicsApiId api, TextureViewHandle view, void * outNativeImport, Error * error) noexcept
	{
		return m_blocks->device().adoption != nullptr ? m_blocks->device().adoption->getNativeTextureView(m_impl, api, view, outNativeImport, error)
													  : decline<bool>(error, kNoAdoption);
	}

	bool Device::GetNativeSamplerRaw(GraphicsApiId api, SamplerHandle sampler, void * outNativeImport, Error * error) noexcept
	{
		return m_blocks->device().adoption != nullptr ? m_blocks->device().adoption->getNativeSampler(m_impl, api, sampler, outNativeImport, error)
													  : decline<bool>(error, kNoAdoption);
	}

	TimelineHandle Device::AdoptTimelineRaw(GraphicsApiId api, const void * nativeImport, const AdoptedTimelineDesc & desc, Error * error) noexcept
	{
		if (m_blocks->device().adoption == nullptr)
		{
			return decline<TimelineHandle>(error, kNoAdoption);
		}

		const std::scoped_lock guard(m_blocks->guard(ResourceType::eTimeline));
		return m_blocks->device().adoption->adoptTimeline(m_impl, api, nativeImport, desc, error);
	}

	BinarySemaphoreHandle Device::AdoptBinarySemaphoreRaw(
		GraphicsApiId api,
		const void * nativeImport,
		const AdoptedBinarySemaphoreDesc & desc,
		Error * error
	) noexcept
	{
		if (m_blocks->device().adoption == nullptr)
		{
			return decline<BinarySemaphoreHandle>(error, kNoAdoption);
		}

		const std::scoped_lock guard(m_blocks->guard(ResourceType::eBinarySemaphore));
		return m_blocks->device().adoption->adoptBinarySemaphore(m_impl, api, nativeImport, desc, error);
	}

	bool Device::GetNativeTimelineRaw(GraphicsApiId api, TimelineHandle timeline, void * outNativeImport, Error * error) noexcept
	{
		return m_blocks->device().adoption != nullptr ? m_blocks->device().adoption->getNativeTimeline(m_impl, api, timeline, outNativeImport, error)
													  : decline<bool>(error, kNoAdoption);
	}

	bool Device::GetNativeBinarySemaphoreRaw(GraphicsApiId api, BinarySemaphoreHandle semaphore, void * outNativeImport, Error * error) noexcept
	{
		return m_blocks->device().adoption != nullptr ? m_blocks->device().adoption->getNativeBinarySemaphore(m_impl, api, semaphore, outNativeImport, error)
													  : decline<bool>(error, kNoAdoption);
	}

	QueueType Queue::get_type() const noexcept
	{
		return m_blocks->core->getType(m_impl);
	}

	bool Queue::submit(const SubmitDesc & desc) noexcept
	{
		return m_blocks->core->submit(m_impl, desc, nullptr);
	}

	bool Queue::submit(const SubmitDesc & desc, Error & error) noexcept
	{
		error = {};
		return m_blocks->core->submit(m_impl, desc, &error);
	}

	bool Queue::wait_idle() noexcept
	{
		return m_blocks->core->waitIdle(m_impl, nullptr);
	}

	bool Queue::wait_idle(Error & error) noexcept
	{
		error = {};
		return m_blocks->core->waitIdle(m_impl, &error);
	}

	bool Queue::wait(TimelineHandle timeline, std::uint64_t value, std::uint64_t timeoutNanoseconds) noexcept
	{
		return m_blocks->core->wait(m_impl, timeline, value, timeoutNanoseconds, nullptr);
	}

	bool Queue::wait(TimelineHandle timeline, std::uint64_t value, std::uint64_t timeoutNanoseconds, Error & error) noexcept
	{
		error = {};
		return m_blocks->core->wait(m_impl, timeline, value, timeoutNanoseconds, &error);
	}

	bool Queue::signal(TimelineHandle timeline, std::uint64_t value) noexcept
	{
		return m_blocks->core->signal(m_impl, timeline, value, nullptr);
	}

	bool Queue::signal(TimelineHandle timeline, std::uint64_t value, Error & error) noexcept
	{
		error = {};
		return m_blocks->core->signal(m_impl, timeline, value, &error);
	}

	bool Queue::bind_sparse(const SparseBindDesc & desc) noexcept
	{
		return m_blocks->sparse != nullptr ? m_blocks->sparse->bindSparse(m_impl, desc, nullptr) : decline<bool>(nullptr, kNoSparse);
	}

	bool Queue::bind_sparse(const SparseBindDesc & desc, Error & error) noexcept
	{
		error = {};
		return m_blocks->sparse != nullptr ? m_blocks->sparse->bindSparse(m_impl, desc, &error) : decline<bool>(&error, kNoSparse);
	}

	bool Queue::begin_debug_label(const char * name, std::uint32_t color) noexcept
	{
		return m_blocks->core->beginDebugLabel(m_impl, name, color, nullptr);
	}

	bool Queue::begin_debug_label(const char * name, std::uint32_t color, Error & error) noexcept
	{
		error = {};
		return m_blocks->core->beginDebugLabel(m_impl, name, color, &error);
	}

	bool Queue::end_debug_label() noexcept
	{
		return m_blocks->core->endDebugLabel(m_impl, nullptr);
	}

	bool Queue::end_debug_label(Error & error) noexcept
	{
		error = {};
		return m_blocks->core->endDebugLabel(m_impl, &error);
	}

	bool Queue::get_completed_value(TimelineHandle timeline, std::uint64_t & out) const noexcept
	{
		Error error{};
		return get_completed_value(timeline, out, error);
	}

	bool Queue::get_completed_value(TimelineHandle timeline, std::uint64_t & out, Error & error) const noexcept
	{
		out	  = 0;
		error = {};

		bool answered = m_blocks->core->getCompletedValue(m_impl, timeline, &out, &error);
		settle(answered, error);
		if (!answered)
		{
			out = {};
		}

		return answered;
	}

	Result<std::uint64_t> Queue::get_completed_value_with_result(TimelineHandle timeline) const noexcept
	{
		std::uint64_t out = 0;
		Error error{};
		static_cast<void>(get_completed_value(timeline, out, error));
		return as_result(out, error);
	}

	CommandList CommandPool::allocate(const char * debugName) noexcept
	{
		Error error{};
		return allocate(debugName, error);
	}

	CommandList CommandPool::allocate(const char * debugName, Error & error) noexcept
	{
		error = {};
		const std::scoped_lock guard(m_blocks->object_guard());
		void * impl						 = m_dispatch->allocate(m_impl, debugName, &error);
		const CommandListBlocks * blocks = detail::checked_child<RenderCommandApi>(impl, &error) ? m_blocks->command_list(impl) : nullptr;
		settle(blocks, error);

		return blocks != nullptr ? detail::FacadeBuilder::make_command_list(impl, blocks) : CommandList{};
	}

	Result<CommandList> CommandPool::allocate_with_result(const char * debugName) noexcept
	{
		Error error{};
		return as_result(allocate(debugName, error), error);
	}

	bool CommandPool::reset(RetirePoint safeAfter) noexcept
	{
		return m_dispatch->reset(m_impl, safeAfter, nullptr);
	}

	bool CommandPool::reset(RetirePoint safeAfter, Error & error) noexcept
	{
		error = {};
		return m_dispatch->reset(m_impl, safeAfter, &error);
	}

	bool CommandList::begin() noexcept
	{
		return m_blocks->render->begin(m_impl, nullptr);
	}

	bool CommandList::begin(Error & error) noexcept
	{
		error = {};
		return m_blocks->render->begin(m_impl, &error);
	}

	bool CommandList::end() noexcept
	{
		return m_blocks->render->end(m_impl, nullptr);
	}

	bool CommandList::end(Error & error) noexcept
	{
		error = {};
		return m_blocks->render->end(m_impl, &error);
	}

	bool CommandList::barriers(const BarrierBatch & barriers) noexcept
	{
		return m_blocks->render->barriers(m_impl, barriers, nullptr);
	}

	bool CommandList::barriers(const BarrierBatch & barriers, Error & error) noexcept
	{
		error = {};
		return m_blocks->render->barriers(m_impl, barriers, &error);
	}

	bool CommandList::transition(const TextureHandle texture, const Flags<ResourceUse> fromUse, const Flags<ResourceUse> toUse) noexcept
	{
		Error ignored{};
		return transition(texture, fromUse, toUse, ignored);
	}

	bool CommandList::transition(const TextureHandle texture, const Flags<ResourceUse> fromUse, const Flags<ResourceUse> toUse, Error & error) noexcept
	{
		const std::array textures{
			TextureBarrier{
				.texture = texture,
				.before	 = { .use = fromUse },
				.after	 = { .use = toUse },
				.range	 = { .aspects = kAllAspects, .mipCount = kAllMips, .layerCount = kAllLayers },
			},
		};
		return barriers(BarrierBatch{ .textures = textures }, error);
	}

	bool CommandList::transition(const BufferHandle buffer, const Flags<ResourceUse> fromUse, const Flags<ResourceUse> toUse) noexcept
	{
		Error ignored{};
		return transition(buffer, fromUse, toUse, ignored);
	}

	bool CommandList::transition(const BufferHandle buffer, const Flags<ResourceUse> fromUse, const Flags<ResourceUse> toUse, Error & error) noexcept
	{
		const std::array buffers{ BufferBarrier{ .buffer = buffer, .before = { .use = fromUse }, .after = { .use = toUse } } };
		return barriers(BarrierBatch{ .buffers = buffers }, error);
	}

	bool CommandList::alias_barriers(std::span<const AliasBarrier> barriers) noexcept
	{
		return m_blocks->aliasing != nullptr ? m_blocks->aliasing->aliasBarriers(m_impl, barriers, nullptr) : decline<bool>(nullptr, kNoAliasing);
	}

	bool CommandList::alias_barriers(std::span<const AliasBarrier> barriers, Error & error) noexcept
	{
		error = {};
		return m_blocks->aliasing != nullptr ? m_blocks->aliasing->aliasBarriers(m_impl, barriers, &error) : decline<bool>(&error, kNoAliasing);
	}

	bool CommandList::begin_rendering(const BeginRenderingDesc & desc) noexcept
	{
		return m_blocks->render->beginRendering(m_impl, desc, nullptr);
	}

	bool CommandList::begin_rendering(const BeginRenderingDesc & desc, Error & error) noexcept
	{
		error = {};
		return m_blocks->render->beginRendering(m_impl, desc, &error);
	}

	bool CommandList::end_rendering() noexcept
	{
		return m_blocks->render->endRendering(m_impl, nullptr);
	}

	bool CommandList::end_rendering(Error & error) noexcept
	{
		error = {};
		return m_blocks->render->endRendering(m_impl, &error);
	}

	bool CommandList::set_graphics_pipeline(GraphicsPipelineHandle pipeline) noexcept
	{
		return m_blocks->render->setGraphicsPipeline(m_impl, pipeline, nullptr);
	}

	bool CommandList::set_graphics_pipeline(GraphicsPipelineHandle pipeline, Error & error) noexcept
	{
		error = {};
		return m_blocks->render->setGraphicsPipeline(m_impl, pipeline, &error);
	}

	bool CommandList::set_compute_pipeline(ComputePipelineHandle pipeline) noexcept
	{
		return m_blocks->render->setComputePipeline(m_impl, pipeline, nullptr);
	}

	bool CommandList::set_compute_pipeline(ComputePipelineHandle pipeline, Error & error) noexcept
	{
		error = {};
		return m_blocks->render->setComputePipeline(m_impl, pipeline, &error);
	}

	bool CommandList::set_ray_tracing_pipeline(RayTracingPipelineHandle pipeline) noexcept
	{
		return m_blocks->rayTracing != nullptr ? m_blocks->rayTracing->setRayTracingPipeline(m_impl, pipeline, nullptr)
											   : decline<bool>(nullptr, kNoRayTracingCommand);
	}

	bool CommandList::set_ray_tracing_pipeline(RayTracingPipelineHandle pipeline, Error & error) noexcept
	{
		error = {};
		return m_blocks->rayTracing != nullptr ? m_blocks->rayTracing->setRayTracingPipeline(m_impl, pipeline, &error)
											   : decline<bool>(&error, kNoRayTracingCommand);
	}

	bool CommandList::bind_descriptor_set(
		PipelineLayoutHandle layout,
		std::uint32_t setIndex,
		DescriptorSetHandle set,
		std::span<const DynamicDescriptorOffset> dynamicOffsets
	) noexcept
	{
		return m_blocks->render->bindDescriptorSet(m_impl, layout, setIndex, set, dynamicOffsets, nullptr);
	}

	bool CommandList::bind_descriptor_set(
		PipelineLayoutHandle layout,
		std::uint32_t setIndex,
		DescriptorSetHandle set,
		std::span<const DynamicDescriptorOffset> dynamicOffsets,
		Error & error
	) noexcept
	{
		error = {};
		return m_blocks->render->bindDescriptorSet(m_impl, layout, setIndex, set, dynamicOffsets, &error);
	}

	bool CommandList::push_constants(
		PipelineLayoutHandle layout,
		Flags<ShaderStage> stages,
		std::uint32_t offset,
		std::uint32_t size,
		const void * data
	) noexcept
	{
		return m_blocks->render->pushConstants(m_impl, layout, stages, offset, size, data, nullptr);
	}

	bool CommandList::push_constants(
		PipelineLayoutHandle layout,
		Flags<ShaderStage> stages,
		std::uint32_t offset,
		std::uint32_t size,
		const void * data,
		Error & error
	) noexcept
	{
		error = {};
		return m_blocks->render->pushConstants(m_impl, layout, stages, offset, size, data, &error);
	}

	bool CommandList::set_viewport(const Viewport & viewport) noexcept
	{
		return m_blocks->render->setViewport(m_impl, viewport, nullptr);
	}

	bool CommandList::set_viewport(const Viewport & viewport, Error & error) noexcept
	{
		error = {};
		return m_blocks->render->setViewport(m_impl, viewport, &error);
	}

	bool CommandList::set_scissor(const Rect2D & scissor) noexcept
	{
		return m_blocks->render->setScissor(m_impl, scissor, nullptr);
	}

	bool CommandList::set_scissor(const Rect2D & scissor, Error & error) noexcept
	{
		error = {};
		return m_blocks->render->setScissor(m_impl, scissor, &error);
	}

	bool CommandList::set_blend_constants(float r, float g, float b, float a) noexcept
	{
		return m_blocks->render->setBlendConstants(m_impl, r, g, b, a, nullptr);
	}

	bool CommandList::set_blend_constants(float r, float g, float b, float a, Error & error) noexcept
	{
		error = {};
		return m_blocks->render->setBlendConstants(m_impl, r, g, b, a, &error);
	}

	bool CommandList::set_stencil_reference(std::uint32_t reference) noexcept
	{
		return m_blocks->render->setStencilReference(m_impl, reference, nullptr);
	}

	bool CommandList::set_stencil_reference(std::uint32_t reference, Error & error) noexcept
	{
		error = {};
		return m_blocks->render->setStencilReference(m_impl, reference, &error);
	}

	bool CommandList::set_depth_bias(float constantFactor, float clamp, float slopeFactor) noexcept
	{
		return m_blocks->render->setDepthBias(m_impl, constantFactor, clamp, slopeFactor, nullptr);
	}

	bool CommandList::set_depth_bias(float constantFactor, float clamp, float slopeFactor, Error & error) noexcept
	{
		error = {};
		return m_blocks->render->setDepthBias(m_impl, constantFactor, clamp, slopeFactor, &error);
	}

	bool CommandList::set_vertex_buffer(std::uint32_t slot, BufferHandle buffer, std::uint64_t offset) noexcept
	{
		return m_blocks->render->setVertexBuffer(m_impl, slot, buffer, offset, nullptr);
	}

	bool CommandList::set_vertex_buffer(std::uint32_t slot, BufferHandle buffer, std::uint64_t offset, Error & error) noexcept
	{
		error = {};
		return m_blocks->render->setVertexBuffer(m_impl, slot, buffer, offset, &error);
	}

	bool CommandList::set_index_buffer(BufferHandle buffer, std::uint64_t offset, bool index32) noexcept
	{
		return m_blocks->render->setIndexBuffer(m_impl, buffer, offset, index32, nullptr);
	}

	bool CommandList::set_index_buffer(BufferHandle buffer, std::uint64_t offset, bool index32, Error & error) noexcept
	{
		error = {};
		return m_blocks->render->setIndexBuffer(m_impl, buffer, offset, index32, &error);
	}

	bool CommandList::draw(std::uint32_t vertexCount, std::uint32_t instanceCount, std::uint32_t firstVertex, std::uint32_t firstInstance) noexcept
	{
		return m_blocks->render->draw(m_impl, vertexCount, instanceCount, firstVertex, firstInstance, nullptr);
	}

	bool CommandList::draw(
		std::uint32_t vertexCount,
		std::uint32_t instanceCount,
		std::uint32_t firstVertex,
		std::uint32_t firstInstance,
		Error & error
	) noexcept
	{
		error = {};
		return m_blocks->render->draw(m_impl, vertexCount, instanceCount, firstVertex, firstInstance, &error);
	}

	bool CommandList::draw_indexed(
		std::uint32_t indexCount,
		std::uint32_t instanceCount,
		std::uint32_t firstIndex,
		std::int32_t vertexOffset,
		std::uint32_t firstInstance
	) noexcept
	{
		return m_blocks->render->drawIndexed(m_impl, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance, nullptr);
	}

	bool CommandList::draw_indexed(
		std::uint32_t indexCount,
		std::uint32_t instanceCount,
		std::uint32_t firstIndex,
		std::int32_t vertexOffset,
		std::uint32_t firstInstance,
		Error & error
	) noexcept
	{
		error = {};
		return m_blocks->render->drawIndexed(m_impl, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance, &error);
	}

	bool CommandList::draw_indirect(BufferHandle args, std::uint64_t offset, std::uint32_t drawCount, std::uint32_t stride) noexcept
	{
		return m_blocks->indirect != nullptr ? m_blocks->indirect->drawIndirect(m_impl, args, offset, drawCount, stride, nullptr)
											 : decline<bool>(nullptr, kNoIndirect);
	}

	bool CommandList::draw_indirect(BufferHandle args, std::uint64_t offset, std::uint32_t drawCount, std::uint32_t stride, Error & error) noexcept
	{
		error = {};
		return m_blocks->indirect != nullptr ? m_blocks->indirect->drawIndirect(m_impl, args, offset, drawCount, stride, &error)
											 : decline<bool>(&error, kNoIndirect);
	}

	bool CommandList::draw_indexed_indirect(BufferHandle args, std::uint64_t offset, std::uint32_t drawCount, std::uint32_t stride) noexcept
	{
		return m_blocks->indirect != nullptr ? m_blocks->indirect->drawIndexedIndirect(m_impl, args, offset, drawCount, stride, nullptr)
											 : decline<bool>(nullptr, kNoIndirect);
	}

	bool CommandList::draw_indexed_indirect(BufferHandle args, std::uint64_t offset, std::uint32_t drawCount, std::uint32_t stride, Error & error) noexcept
	{
		error = {};
		return m_blocks->indirect != nullptr ? m_blocks->indirect->drawIndexedIndirect(m_impl, args, offset, drawCount, stride, &error)
											 : decline<bool>(&error, kNoIndirect);
	}

	bool CommandList::draw_indirect_count(
		BufferHandle args,
		std::uint64_t argsOffset,
		BufferHandle count,
		std::uint64_t countOffset,
		std::uint32_t maxDrawCount,
		std::uint32_t stride
	) noexcept
	{
		return m_blocks->indirectCount != nullptr
				   ? m_blocks->indirectCount->drawIndirectCount(m_impl, args, argsOffset, count, countOffset, maxDrawCount, stride, nullptr)
				   : decline<bool>(nullptr, kNoIndirectCount);
	}

	bool CommandList::draw_indirect_count(
		BufferHandle args,
		std::uint64_t argsOffset,
		BufferHandle count,
		std::uint64_t countOffset,
		std::uint32_t maxDrawCount,
		std::uint32_t stride,
		Error & error
	) noexcept
	{
		error = {};
		return m_blocks->indirectCount != nullptr
				   ? m_blocks->indirectCount->drawIndirectCount(m_impl, args, argsOffset, count, countOffset, maxDrawCount, stride, &error)
				   : decline<bool>(&error, kNoIndirectCount);
	}

	bool CommandList::draw_indexed_indirect_count(
		BufferHandle args,
		std::uint64_t argsOffset,
		BufferHandle count,
		std::uint64_t countOffset,
		std::uint32_t maxDrawCount,
		std::uint32_t stride
	) noexcept
	{
		return m_blocks->indirectCount != nullptr
				   ? m_blocks->indirectCount->drawIndexedIndirectCount(m_impl, args, argsOffset, count, countOffset, maxDrawCount, stride, nullptr)
				   : decline<bool>(nullptr, kNoIndirectCount);
	}

	bool CommandList::draw_indexed_indirect_count(
		BufferHandle args,
		std::uint64_t argsOffset,
		BufferHandle count,
		std::uint64_t countOffset,
		std::uint32_t maxDrawCount,
		std::uint32_t stride,
		Error & error
	) noexcept
	{
		error = {};
		return m_blocks->indirectCount != nullptr
				   ? m_blocks->indirectCount->drawIndexedIndirectCount(m_impl, args, argsOffset, count, countOffset, maxDrawCount, stride, &error)
				   : decline<bool>(&error, kNoIndirectCount);
	}

	bool CommandList::dispatch(std::uint32_t groupCountX, std::uint32_t groupCountY, std::uint32_t groupCountZ) noexcept
	{
		return m_blocks->render->dispatch(m_impl, groupCountX, groupCountY, groupCountZ, nullptr);
	}

	bool CommandList::dispatch(std::uint32_t groupCountX, std::uint32_t groupCountY, std::uint32_t groupCountZ, Error & error) noexcept
	{
		error = {};
		return m_blocks->render->dispatch(m_impl, groupCountX, groupCountY, groupCountZ, &error);
	}

	bool CommandList::dispatch_indirect(BufferHandle args, std::uint64_t offset) noexcept
	{
		return m_blocks->indirect != nullptr ? m_blocks->indirect->dispatchIndirect(m_impl, args, offset, nullptr) : decline<bool>(nullptr, kNoIndirect);
	}

	bool CommandList::dispatch_indirect(BufferHandle args, std::uint64_t offset, Error & error) noexcept
	{
		error = {};
		return m_blocks->indirect != nullptr ? m_blocks->indirect->dispatchIndirect(m_impl, args, offset, &error) : decline<bool>(&error, kNoIndirect);
	}

	bool CommandList::build_acceleration_structures(std::span<const AccelerationStructureBuildDesc> builds) noexcept
	{
		return m_blocks->rayTracing != nullptr ? m_blocks->rayTracing->buildAccelerationStructures(m_impl, builds, nullptr)
											   : decline<bool>(nullptr, kNoRayTracingCommand);
	}

	bool CommandList::build_acceleration_structures(std::span<const AccelerationStructureBuildDesc> builds, Error & error) noexcept
	{
		error = {};
		return m_blocks->rayTracing != nullptr ? m_blocks->rayTracing->buildAccelerationStructures(m_impl, builds, &error)
											   : decline<bool>(&error, kNoRayTracingCommand);
	}

	bool CommandList::copy_acceleration_structure(AccelerationStructureHandle dst, AccelerationStructureHandle src) noexcept
	{
		return m_blocks->rayTracing != nullptr ? m_blocks->rayTracing->copyAccelerationStructure(m_impl, dst, src, nullptr)
											   : decline<bool>(nullptr, kNoRayTracingCommand);
	}

	bool CommandList::copy_acceleration_structure(AccelerationStructureHandle dst, AccelerationStructureHandle src, Error & error) noexcept
	{
		error = {};
		return m_blocks->rayTracing != nullptr ? m_blocks->rayTracing->copyAccelerationStructure(m_impl, dst, src, &error)
											   : decline<bool>(&error, kNoRayTracingCommand);
	}

	bool CommandList::compact_acceleration_structure(AccelerationStructureHandle dst, AccelerationStructureHandle src) noexcept
	{
		return m_blocks->rayTracing != nullptr ? m_blocks->rayTracing->compactAccelerationStructure(m_impl, dst, src, nullptr)
											   : decline<bool>(nullptr, kNoRayTracingCommand);
	}

	bool CommandList::compact_acceleration_structure(AccelerationStructureHandle dst, AccelerationStructureHandle src, Error & error) noexcept
	{
		error = {};
		return m_blocks->rayTracing != nullptr ? m_blocks->rayTracing->compactAccelerationStructure(m_impl, dst, src, &error)
											   : decline<bool>(&error, kNoRayTracingCommand);
	}

	bool CommandList::trace_rays(const ShaderBindingTableDesc & sbt, std::uint32_t width, std::uint32_t height, std::uint32_t depth) noexcept
	{
		return m_blocks->rayTracing != nullptr ? m_blocks->rayTracing->traceRays(m_impl, sbt, width, height, depth, nullptr)
											   : decline<bool>(nullptr, kNoRayTracingCommand);
	}

	bool CommandList::trace_rays(const ShaderBindingTableDesc & sbt, std::uint32_t width, std::uint32_t height, std::uint32_t depth, Error & error) noexcept
	{
		error = {};
		return m_blocks->rayTracing != nullptr ? m_blocks->rayTracing->traceRays(m_impl, sbt, width, height, depth, &error)
											   : decline<bool>(&error, kNoRayTracingCommand);
	}

	bool CommandList::copy_buffer(BufferHandle dst, std::uint64_t dstOffset, BufferHandle src, std::uint64_t srcOffset, std::uint64_t size) noexcept
	{
		return m_blocks->render->copyBuffer(m_impl, dst, dstOffset, src, srcOffset, size, nullptr);
	}

	bool CommandList::copy_buffer(
		BufferHandle dst,
		std::uint64_t dstOffset,
		BufferHandle src,
		std::uint64_t srcOffset,
		std::uint64_t size,
		Error & error
	) noexcept
	{
		error = {};
		return m_blocks->render->copyBuffer(m_impl, dst, dstOffset, src, srcOffset, size, &error);
	}

	bool CommandList::copy_buffer_to_texture(TextureHandle dst, BufferHandle src, std::span<const BufferTextureCopy> regions) noexcept
	{
		return m_blocks->render->copyBufferToTexture(m_impl, dst, src, regions, nullptr);
	}

	bool CommandList::copy_buffer_to_texture(TextureHandle dst, BufferHandle src, std::span<const BufferTextureCopy> regions, Error & error) noexcept
	{
		error = {};
		return m_blocks->render->copyBufferToTexture(m_impl, dst, src, regions, &error);
	}

	bool CommandList::copy_texture_to_buffer(BufferHandle dst, TextureHandle src, std::span<const BufferTextureCopy> regions) noexcept
	{
		return m_blocks->render->copyTextureToBuffer(m_impl, dst, src, regions, nullptr);
	}

	bool CommandList::copy_texture_to_buffer(BufferHandle dst, TextureHandle src, std::span<const BufferTextureCopy> regions, Error & error) noexcept
	{
		error = {};
		return m_blocks->render->copyTextureToBuffer(m_impl, dst, src, regions, &error);
	}

	bool CommandList::copy_texture(TextureHandle dst, TextureHandle src, std::span<const TextureCopy> regions) noexcept
	{
		return m_blocks->render->copyTexture(m_impl, dst, src, regions, nullptr);
	}

	bool CommandList::copy_texture(TextureHandle dst, TextureHandle src, std::span<const TextureCopy> regions, Error & error) noexcept
	{
		error = {};
		return m_blocks->render->copyTexture(m_impl, dst, src, regions, &error);
	}

	bool CommandList::clear_buffer(BufferHandle buffer, std::uint64_t offset, std::uint64_t size, std::uint32_t value) noexcept
	{
		return m_blocks->render->clearBuffer(m_impl, buffer, offset, size, value, nullptr);
	}

	bool CommandList::clear_buffer(BufferHandle buffer, std::uint64_t offset, std::uint64_t size, std::uint32_t value, Error & error) noexcept
	{
		error = {};
		return m_blocks->render->clearBuffer(m_impl, buffer, offset, size, value, &error);
	}

	bool CommandList::clear_texture(TextureHandle texture, const ClearColor & color, std::span<const TextureSubresourceRange> ranges) noexcept
	{
		return m_blocks->render->clearTexture(m_impl, texture, color, ranges, nullptr);
	}

	bool CommandList::clear_texture(TextureHandle texture, const ClearColor & color, std::span<const TextureSubresourceRange> ranges, Error & error) noexcept
	{
		error = {};
		return m_blocks->render->clearTexture(m_impl, texture, color, ranges, &error);
	}

	bool CommandList::resolve_texture(TextureHandle dst, TextureHandle src, std::span<const TextureResolve> regions) noexcept
	{
		return m_blocks->render->resolveTexture(m_impl, dst, src, regions, nullptr);
	}

	bool CommandList::resolve_texture(TextureHandle dst, TextureHandle src, std::span<const TextureResolve> regions, Error & error) noexcept
	{
		error = {};
		return m_blocks->render->resolveTexture(m_impl, dst, src, regions, &error);
	}

	bool CommandList::blit(TextureHandle dst, TextureHandle src, std::span<const TextureBlit> regions, Filter filter) noexcept
	{
		return m_blocks->render->blit(m_impl, dst, src, regions, filter, nullptr);
	}

	bool CommandList::blit(TextureHandle dst, TextureHandle src, std::span<const TextureBlit> regions, Filter filter, Error & error) noexcept
	{
		error = {};
		return m_blocks->render->blit(m_impl, dst, src, regions, filter, &error);
	}

	bool CommandList::generate_mips(TextureHandle texture) noexcept
	{
		return m_blocks->render->generateMips(m_impl, texture, nullptr);
	}

	bool CommandList::generate_mips(TextureHandle texture, Error & error) noexcept
	{
		error = {};
		return m_blocks->render->generateMips(m_impl, texture, &error);
	}

	bool CommandList::reset_query_pool(QueryPoolHandle pool, std::uint32_t firstQuery, std::uint32_t queryCount) noexcept
	{
		return m_blocks->query != nullptr ? m_blocks->query->resetQueryPool(m_impl, pool, firstQuery, queryCount, nullptr)
										  : decline<bool>(nullptr, kNoQueryCommand);
	}

	bool CommandList::reset_query_pool(QueryPoolHandle pool, std::uint32_t firstQuery, std::uint32_t queryCount, Error & error) noexcept
	{
		error = {};
		return m_blocks->query != nullptr ? m_blocks->query->resetQueryPool(m_impl, pool, firstQuery, queryCount, &error)
										  : decline<bool>(&error, kNoQueryCommand);
	}

	bool CommandList::write_timestamp(QueryPoolHandle pool, std::uint32_t query, Flags<Stage> stage) noexcept
	{
		return m_blocks->query != nullptr ? m_blocks->query->writeTimestamp(m_impl, pool, query, stage, nullptr) : decline<bool>(nullptr, kNoQueryCommand);
	}

	bool CommandList::write_timestamp(QueryPoolHandle pool, std::uint32_t query, Flags<Stage> stage, Error & error) noexcept
	{
		error = {};
		return m_blocks->query != nullptr ? m_blocks->query->writeTimestamp(m_impl, pool, query, stage, &error) : decline<bool>(&error, kNoQueryCommand);
	}

	bool CommandList::begin_query(QueryPoolHandle pool, std::uint32_t query) noexcept
	{
		return m_blocks->query != nullptr ? m_blocks->query->beginQuery(m_impl, pool, query, nullptr) : decline<bool>(nullptr, kNoQueryCommand);
	}

	bool CommandList::begin_query(QueryPoolHandle pool, std::uint32_t query, Error & error) noexcept
	{
		error = {};
		return m_blocks->query != nullptr ? m_blocks->query->beginQuery(m_impl, pool, query, &error) : decline<bool>(&error, kNoQueryCommand);
	}

	bool CommandList::end_query(QueryPoolHandle pool, std::uint32_t query) noexcept
	{
		return m_blocks->query != nullptr ? m_blocks->query->endQuery(m_impl, pool, query, nullptr) : decline<bool>(nullptr, kNoQueryCommand);
	}

	bool CommandList::end_query(QueryPoolHandle pool, std::uint32_t query, Error & error) noexcept
	{
		error = {};
		return m_blocks->query != nullptr ? m_blocks->query->endQuery(m_impl, pool, query, &error) : decline<bool>(&error, kNoQueryCommand);
	}

	bool CommandList::resolve_query_data(
		QueryPoolHandle pool,
		std::uint32_t firstQuery,
		std::uint32_t queryCount,
		BufferHandle dst,
		std::uint64_t dstOffset
	) noexcept
	{
		return m_blocks->query != nullptr ? m_blocks->query->resolveQueryData(m_impl, pool, firstQuery, queryCount, dst, dstOffset, nullptr)
										  : decline<bool>(nullptr, kNoQueryCommand);
	}

	bool CommandList::resolve_query_data(
		QueryPoolHandle pool,
		std::uint32_t firstQuery,
		std::uint32_t queryCount,
		BufferHandle dst,
		std::uint64_t dstOffset,
		Error & error
	) noexcept
	{
		error = {};
		return m_blocks->query != nullptr ? m_blocks->query->resolveQueryData(m_impl, pool, firstQuery, queryCount, dst, dstOffset, &error)
										  : decline<bool>(&error, kNoQueryCommand);
	}

	bool CommandList::begin_debug_label(const char * name, std::uint32_t color) noexcept
	{
		return m_blocks->render->beginDebugLabel(m_impl, name, color, nullptr);
	}

	bool CommandList::begin_debug_label(const char * name, std::uint32_t color, Error & error) noexcept
	{
		error = {};
		return m_blocks->render->beginDebugLabel(m_impl, name, color, &error);
	}

	bool CommandList::end_debug_label() noexcept
	{
		return m_blocks->render->endDebugLabel(m_impl, nullptr);
	}

	bool CommandList::end_debug_label(Error & error) noexcept
	{
		error = {};
		return m_blocks->render->endDebugLabel(m_impl, &error);
	}

	bool CommandList::BeginNativeMutation(GraphicsApiId api, const NativeMutationDesc & desc, Error * error) noexcept
	{
		return m_blocks->nativeEscape != nullptr ? m_blocks->nativeEscape->beginNativeMutation(m_impl, api, desc, error)
												 : decline<bool>(error, kNoNativeEscape);
	}

	bool CommandList::EndNativeMutation(const NativeMutationDesc & desc, Error * error) noexcept
	{
		return m_blocks->nativeEscape != nullptr ? m_blocks->nativeEscape->endNativeMutation(m_impl, desc, error) : decline<bool>(error, kNoNativeEscape);
	}

	AcquireResult Swapchain::acquire_next_image(std::uint64_t timeoutNanoseconds) noexcept
	{
		Error error{};
		return acquire_next_image(timeoutNanoseconds, error);
	}

	AcquireResult Swapchain::acquire_next_image(std::uint64_t timeoutNanoseconds, Error & error) noexcept
	{
		error				   = {};
		AcquireResult produced = m_dispatch->acquireNextImage(m_impl, timeoutNanoseconds, &error);
		settle(produced, error);

		if (produced.status == SwapchainStatus::eOk || produced.status == SwapchainStatus::eSuboptimal)
		{
			produced.texture		= m_dispatch->getBackBuffer(m_impl, produced.imageIndex);
			produced.view			= m_dispatch->getBackBufferView(m_impl, produced.imageIndex);
			produced.renderFinished = m_dispatch->getPerImagePresentSemaphore(m_impl, produced.imageIndex);
		}

		return produced;
	}

	Result<AcquireResult> Swapchain::acquire_next_image_with_result(std::uint64_t timeoutNanoseconds) noexcept
	{
		Error error{};
		return as_result(acquire_next_image(timeoutNanoseconds, error), error);
	}

	PresentResult Swapchain::present(Queue & queue, std::uint32_t imageIndex, BinarySemaphoreHandle renderFinished) noexcept
	{
		Error error{};
		return present(queue, imageIndex, renderFinished, error);
	}

	PresentResult Swapchain::present(Queue & queue, std::uint32_t imageIndex, BinarySemaphoreHandle renderFinished, Error & error) noexcept
	{
		error				   = {};
		PresentResult produced = m_dispatch->present(m_impl, imageIndex, renderFinished, detail::FacadeBuilder::impl_of(queue), &error);
		settle(produced, error);
		return produced;
	}

	Result<PresentResult> Swapchain::present_with_result(Queue & queue, std::uint32_t imageIndex, BinarySemaphoreHandle renderFinished) noexcept
	{
		Error error{};
		return as_result(present(queue, imageIndex, renderFinished, error), error);
	}

	TextureHandle Swapchain::get_back_buffer(std::uint32_t imageIndex) const noexcept
	{
		return m_dispatch->getBackBuffer(m_impl, imageIndex);
	}

	TextureViewHandle Swapchain::get_back_buffer_view(std::uint32_t imageIndex) const noexcept
	{
		return m_dispatch->getBackBufferView(m_impl, imageIndex);
	}

	BinarySemaphoreHandle Swapchain::get_per_image_present_semaphore(std::uint32_t imageIndex) const noexcept
	{
		return m_dispatch->getPerImagePresentSemaphore(m_impl, imageIndex);
	}

	Format Swapchain::get_format() const noexcept
	{
		return m_dispatch->getFormat(m_impl);
	}

	bool Swapchain::supports_readback() const noexcept
	{
		return m_dispatch->supportsReadback(m_impl);
	}

	std::uint32_t Swapchain::get_image_count() const noexcept
	{
		return m_dispatch->getImageCount(m_impl);
	}

	std::uint32_t Swapchain::get_width() const noexcept
	{
		return m_dispatch->getWidth(m_impl);
	}

	std::uint32_t Swapchain::get_height() const noexcept
	{
		return m_dispatch->getHeight(m_impl);
	}

	bool Swapchain::resize(std::uint32_t width, std::uint32_t height) noexcept
	{
		return m_dispatch->resize(m_impl, width, height, nullptr);
	}

	bool Swapchain::resize(std::uint32_t width, std::uint32_t height, Error & error) noexcept
	{
		error = {};
		return m_dispatch->resize(m_impl, width, height, &error);
	}

	bool Swapchain::set_present_mode(PresentMode mode) noexcept
	{
		return m_dispatch->setPresentMode(m_impl, mode, nullptr);
	}

	PresentMode Swapchain::get_present_mode() const noexcept
	{
		return m_dispatch->getPresentMode(m_impl);
	}

	DescriptorSetHandle DescriptorArena::allocate(const DescriptorSetAllocDesc & desc) noexcept
	{
		Error error{};
		return allocate(desc, error);
	}

	DescriptorSetHandle DescriptorArena::allocate(const DescriptorSetAllocDesc & desc, Error & error) noexcept
	{
		error = {};

		const std::scoped_lock guard(m_blocks->guard(ResourceType::eDescriptorSet));
		DescriptorSetHandle produced = m_dispatch->allocate(m_impl, desc, &error);
		settle(produced, error);
		return produced;
	}

	Result<DescriptorSetHandle> DescriptorArena::allocate_with_result(const DescriptorSetAllocDesc & desc) noexcept
	{
		Error error{};
		return as_result(allocate(desc, error), error);
	}

	bool DescriptorArena::reset(RetirePoint safeAfter) noexcept
	{
		const std::scoped_lock guard(m_blocks->guard(ResourceType::eDescriptorSet));
		return m_dispatch->reset(m_impl, safeAfter, nullptr);
	}

	bool DescriptorArena::reset(RetirePoint safeAfter, Error & error) noexcept
	{
		error = {};
		const std::scoped_lock guard(m_blocks->guard(ResourceType::eDescriptorSet));
		return m_dispatch->reset(m_impl, safeAfter, &error);
	}

	void UniqueDevice::Reset() noexcept
	{
		if (m_impl != nullptr && m_blocks != nullptr)
		{
			{
				const detail::LifetimeLock lifetime;
				m_blocks->device().core->destroyDevice(m_impl);
			}

			m_blocks->tracker().forget();

			detail::release_device_blocks(m_blocks);
		}

		m_impl	 = nullptr;
		m_blocks = nullptr;
	}

	namespace
	{

		[[nodiscard]] void * create_backend_instance(
			GraphicsApiRegistry & registry,
			std::span<const GraphicsApiId> preferredApis,
			const InstanceDesc & desc,
			Error & error
		)
		{
			for (GraphicsApiId id : preferredApis)
			{
				const BackendCreateInfo * backend = detail::RegistryAccess::find(registry, id);
				if (backend == nullptr || backend->createInstance == nullptr)
				{
					continue;
				}

				void * instanceImpl = nullptr;
				{
					const detail::LifetimeLock lifetime;
					instanceImpl = backend->createInstance(&desc, &error);
				}

				if (instanceImpl == nullptr && error.code == ErrorCode::eOk)
				{
					error = Error{
						.code	 = ErrorCode::eUnknown,
						.message = "backend returned a null instance",
					};
				}

				return instanceImpl;
			}

			error = Error{
				.code	 = ErrorCode::eUnsupportedApi,
				.message = "none of the preferred graphics API backends are registered",
			};
			return nullptr;
		}

		[[nodiscard]] const InstanceApi * block_or_release(void * instanceImpl, Error & error)
		{
			const auto * block = detail::checked_block<InstanceApi>(instanceImpl, &error);
			if (block != nullptr)
			{
				return block;
			}

			if (const auto * partial = detail::query_block<InstanceApi>(instanceImpl); partial != nullptr && partial->destroyInstance != nullptr)
			{
				const detail::LifetimeLock lifetime;
				partial->destroyInstance(instanceImpl);
			}

			return nullptr;
		}

		[[nodiscard]] Result<UniqueDevice> create_device_on(const BackendCreateInfo & backend, const InstanceDesc & instanceDesc, const DeviceDesc & desc)
		{
			Error error{};

			void * instanceImpl = nullptr;
			{
				const detail::LifetimeLock lifetime;
				instanceImpl = backend.createInstance(&instanceDesc, &error);
			}

			if (instanceImpl == nullptr)
			{
				return error.code == ErrorCode::eOk ? Error{ .code = ErrorCode::eUnknown, .message = "backend returned a null instance" } : error;
			}

			const InstanceApi * dispatch = block_or_release(instanceImpl, error);
			if (dispatch == nullptr)
			{
				return error;
			}

			void * deviceImpl = nullptr;
			{
				const detail::LifetimeLock lifetime;
				deviceImpl = dispatch->createDevice(instanceImpl, desc, &error);
			}

			if (deviceImpl == nullptr)
			{
				const detail::LifetimeLock lifetime;
				dispatch->destroyInstance(instanceImpl);
				return error.code == ErrorCode::eOk ? Error{ .code = ErrorCode::eUnknown, .message = "backend returned a null device", } : error;
			}

			BackendBlockSet * blocks = detail::resolve_device_blocks(deviceImpl, desc, &error);
			if (blocks == nullptr)
			{
				detail::release_undrivable_device(deviceImpl);

				const detail::LifetimeLock lifetime;
				dispatch->destroyInstance(instanceImpl);
				return error;
			}

			return detail::FacadeBuilder::make_unique_device(deviceImpl, blocks);
		}

	}

	Result<UniqueInstance> create_instance(GraphicsApiRegistry & registry, std::span<const GraphicsApiId> preferredApis, const InstanceDesc & desc)
	{
		Error error{};
		void * instanceImpl = create_backend_instance(registry, preferredApis, desc, error);
		if (instanceImpl == nullptr)
		{
			return error;
		}

		const InstanceApi * dispatch = block_or_release(instanceImpl, error);
		if (dispatch == nullptr)
		{
			return error;
		}

		return detail::FacadeBuilder::make_unique_instance(instanceImpl, dispatch);
	}

	Result<UniqueDevice> create_device(GraphicsApiRegistry & registry, std::span<const GraphicsApiId> preferredApis, const DeviceDesc & desc)
	{
		if (const Result<void> checked = detail::check_device_desc(desc); !checked)
		{
			return checked.get_error();
		}

		const InstanceDesc instanceDesc = instance_desc_for_device(desc);

		Error refusal{};
		bool anyTried = false;

		for (const GraphicsApiId id : preferredApis)
		{
			const BackendCreateInfo * backend = detail::RegistryAccess::find(registry, id);
			if (backend == nullptr || backend->createInstance == nullptr)
			{
				continue;
			}

			Result<UniqueDevice> device = create_device_on(*backend, instanceDesc, desc);
			if (device)
			{
				return device;
			}

			if (!anyTried)
			{
				refusal	 = device.get_error();
				anyTried = true;
			}
		}

		if (!anyTried)
		{
			return Error{
				.code	 = ErrorCode::eUnsupportedApi,
				.message = "none of the preferred graphics API backends are registered",
			};
		}

		return refusal;
	}

}

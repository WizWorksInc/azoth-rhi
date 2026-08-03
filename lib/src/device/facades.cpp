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
#include "azoth/rhi/backend/table_validation.hpp"
#include "azoth/rhi/native/native_access.hpp"

#include <cstdint>
#include <mutex>
#include <span>
#include <string_view>

namespace azo::rhi
{

	namespace
	{

		void Fail(Error * error, const ErrorCode code, const char * message) noexcept
		{
			if (error != nullptr)
			{
				*error = Error{
					.code	 = code,
					.message = message,
				};
			}
		}

		/*
		 * What a caller is told when the backend did not publish the block an operation lives in.
		 *
		 * Naming the capability and not the block, since a caller reaching CreateSwapchain wants to know this backend cannot present, not which struct that answer
		 * came out of.
		 */
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

		/*
		 * What a declined block answers with.
		 *
		 * A backend says it does not implement a capability by not publishing its block so a null block pointer is an answer and not a fault. Every operation behind
		 * that block reports eUnsupportedFeature and hands back whatever its own return type calls nothing.
		 */
		template <class ValueT>
		[[nodiscard]] ValueT Decline(Error * error, const char * what) noexcept
		{
			Fail(error, ErrorCode::eUnsupportedFeature, what);
			return ValueT{};
		}

		/*
		 * What a call produced, read off the sentinel its own return type carries.
		 *
		 * One of the two things a verdict is built from. The other is the Error and the point of naming this is that the two are read in one place, not each form of
		 * an operation picking one.
		 */
		template <class Tag>
		[[nodiscard]] bool Produced(const Handle<Tag> & handle) noexcept
		{
			return handle.IsValid();
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

		// What each return type looks like once a call has been settled as a failure. Everything but the two swapchain answers is its own default and theirs defaults
		// to success.
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

		/*
		 * The verdict on one call, decided once and written into both things it answered with. A tripled operation reports what happened three ways and each public
		 * form carries only part: the plain form the return value, the out-Error form both, the Result form the Error. Deciding separately in each is how they come
		 * to disagree.
		 *
		 * A backend handing back a resource beside a populated Error has broken its contract. That counts as a failure and not a success.
		 */
		template <class ValueT>
		void Settle(ValueT & value, Error & error) noexcept
		{
			if (Produced(value) && error.code == ErrorCode::eOk)
			{
				return;
			}

			Discard(value);

			// A failure with no code reads as success to anyone checking the code and not the value, which is the disagreement in the other direction.
			if (error.code == ErrorCode::eOk)
			{
				error = Error{
					.code	 = ErrorCode::eUnknown,
					.message = "the backend produced nothing and reported no reason",
				};
			}
		}

		/*
		 * The Result form of a settled verdict.
		 *
		 * It reads the Error and nothing else. Settle has already made the value agree with it so there is no second predicate here to drift from the first.
		 */
		template <class ValueT>
		[[nodiscard]] Result<ValueT> AsResult(ValueT value, const Error & error) noexcept
		{
			if (error.code != ErrorCode::eOk)
			{
				return error;
			}

			return value;
		}

		/*
		 * Asks the allocator for room and records what it granted.
		 *
		 * The backend already reports the footprint a resource needs and already knows how to place one at an offset in a heap so an allocator only has to answer
		 * with somewhere to put it. That makes this the whole of the routing.
		 */
		[[nodiscard]] bool ReserveSpan(Device device, DeviceMemoryAllocator * allocator, const MemoryInfo & info, const HeapType heapType, const bool forBuffer,
			CString debugName, MemorySpan & out, Error * error)
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

			// The allocator is handed a Device and makes heaps through it so it is one of the two places the RHI calls out to something that can call back in.
			detail::CheckNoGuardHeld();

			if (!allocator->Allocate(device, request, out) || !out.IsValid())
			{
				Fail(error, ErrorCode::eOutOfDeviceMemory, "the installed device memory allocator refused the request");
				return false;
			}

			// Checked here, not left to the placed create, which would reject it as whatever the native API calls a bad offset. An allocator that suballocates is the
			// one likely to get this wrong and it is the one that benefits most from being told which promise it broke.
			if (info.alignment != 0 && out.offset % info.alignment != 0)
			{
				detail::CheckNoGuardHeld();
				allocator->Free(device, out);
				out = {};
				Fail(error, ErrorCode::eInvalidArgument, "the device memory allocator returned an offset that does not satisfy the requested alignment");
				return false;
			}

			if (out.size < info.size)
			{
				detail::CheckNoGuardHeld();
				allocator->Free(device, out);
				out = {};
				Fail(error, ErrorCode::eOutOfDeviceMemory, "the device memory allocator returned a span smaller than the resource needs");
				return false;
			}

			return true;
		}

		void RecordSpan(BackendBlockSet & blocks, const ResourceType type, const std::uint32_t index, const std::uint32_t generation, const MemorySpan & span)
		{
			blocks.Tracker().Record(type,
				RawHandle{
					.index		= index,
					.generation = generation,
				},
				span);
		}

		/*
		 * Takes back the span a destroyed resource held, when its destroy said the GPU was already done with it.
		 *
		 * Called under the guard for the kind being destroyed, since the tracker is partitioned by kind and that guard is its whole synchronization. What it does not
		 * do is hand the span to the allocator: that is a call out to a host interface holding an RHI object and it waits until the guard is gone.
		 */
		[[nodiscard]] bool TakeRetiredSpan(BackendBlockSet & blocks, const ResourceType type, const std::uint32_t index, const std::uint32_t generation,
			const DestroyDesc & desc, MemorySpan & out)
		{
			if (blocks.Allocator() == nullptr)
			{
				return false;
			}

			return blocks.Tracker().Retire(type,
				RawHandle{
					.index		= index,
					.generation = generation,
				},
				desc,
				out);
		}

		/*
		 * Runs a collect over every kind, each under that kind's guard. A destroy queues its resource while holding the guard for the kind it destroyed so the
		 * collect draining that queue holds the same one. Sixteen guards at once would mean holding one while taking another, which the cooperative rules forbid.
		 *
		 * Every kind is visited even after one fails so one failing kind cannot strand what the other fifteen queued. The error reported is the first.
		 */
		template <class Collect>
		[[nodiscard]] bool CollectEveryKind(BackendBlockSet & blocks, Error * error, Collect collect) noexcept
		{
			bool collected = true;
			for (std::size_t kind = 0; kind < kResourceTypeCount; ++kind)
			{
				const auto type = static_cast<ResourceType>(kind);

				Error kindError{};
				const std::scoped_lock guard(blocks.Guard(type));
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

		/*
		 * Hands a collect's spans back to the allocator, once every guard the sweep took has been released.
		 *
		 * Free is host code that may create or destroy heaps of its own so calling it under a device guard would be the RHI holding a lock across a call it does not
		 * control, which is the first of the cooperative rules.
		 */
		void FreeSpans(Device device, DeviceMemoryAllocator * allocator, const detail::HostVector<MemorySpan> & spans) noexcept
		{
			if (allocator == nullptr)
			{
				return;
			}

			detail::CheckNoGuardHeld();

			for (const MemorySpan & span : spans)
			{
				allocator->Free(device, span);
			}
		}

	} // namespace

	// Instance.

	// UniqueInstance is the sole owner of backend instance teardown.

	void UniqueInstance::Reset() noexcept
	{
		if (m_impl != nullptr)
		{
			const detail::LifetimeLock lifetime;
			m_dispatch->destroyInstance(m_impl);
		}

		m_impl = nullptr;
	}

	GraphicsApiId Instance::GetGraphicsApiId() const noexcept
	{
		return m_dispatch->getGraphicsApiId(m_impl);
	}

	bool Instance::EnumerateAdapters(std::span<AdapterInfo> adapters, std::uint32_t & out) const noexcept
	{
		Error error{};
		return EnumerateAdapters(adapters, out, error);
	}

	bool Instance::EnumerateAdapters(std::span<AdapterInfo> adapters, std::uint32_t & out, Error & error) const noexcept
	{
		out	  = 0;
		error = {};

		bool answered = m_dispatch->enumerateAdapters(m_impl, adapters, &out, &error);
		Settle(answered, error);
		if (!answered)
		{
			out = {};
		}

		return answered;
	}

	Result<std::uint32_t> Instance::EnumerateAdaptersWithResult(std::span<AdapterInfo> adapters) const noexcept
	{
		std::uint32_t out = 0;
		Error error{};
		static_cast<void>(EnumerateAdapters(adapters, out, error));
		return AsResult(out, error);
	}

	bool Instance::QueryExternalHandleSupport(const ExternalHandleSupportDesc & desc, ExternalHandleSupport & out) const noexcept
	{
		Error error{};
		return QueryExternalHandleSupport(desc, out, error);
	}

	bool Instance::QueryExternalHandleSupport(const ExternalHandleSupportDesc & desc, ExternalHandleSupport & out, Error & error) const noexcept
	{
		out	  = {};
		error = {};

		/*
		 * A backend that declines the block shares nothing, and that is an answer and not a failure.
		 *
		 * Reporting it as a cleared result keeps the question askable on every backend, which is what this query replaced two capability flags to achieve. A caller
		 * sweeping handle types gets no everywhere instead of having to tell a refusal apart from an absence.
		 */
		const ExternalCapabilityApi * block = detail::QueryBlock<ExternalCapabilityApi>(m_impl);
		if (block == nullptr || block->queryExternalHandleSupport == nullptr)
		{
			return true;
		}

		bool answered = block->queryExternalHandleSupport(m_impl, desc, &out, &error);
		Settle(answered, error);

		// A refusal leaves nothing behind and not a partly filled answer, so a caller that ignored the return reads no support, not whatever the backend got to
		// before it stopped.
		if (!answered)
		{
			out = {};
		}

		return answered;
	}

	Result<ExternalHandleSupport> Instance::QueryExternalHandleSupportWithResult(const ExternalHandleSupportDesc & desc) const noexcept
	{
		ExternalHandleSupport out{};
		Error error{};
		static_cast<void>(QueryExternalHandleSupport(desc, out, error));
		return AsResult(out, error);
	}

	// Device queries.

	GraphicsApiId Device::GetGraphicsApiId() const noexcept
	{
		return m_blocks->Device().core->getGraphicsApiId(m_impl);
	}

	std::string_view Device::GetGraphicsApiName() const noexcept
	{
		return m_blocks->Device().core->getGraphicsApiName(m_impl);
	}

	// Device resource and object creation.

	BufferHandle Device::CreateBuffer(const BufferDesc & desc) noexcept
	{
		Error error{};
		return CreateBuffer(desc, error);
	}

	BufferHandle Device::CreateBuffer(const BufferDesc & desc, Error & error) noexcept
	{
		error = {};

		BufferHandle produced = CreateBufferRouted(desc, &error);
		Settle(produced, error);
		return produced;
	}

	// Both public forms come through here so an installed allocator is not something one overload honors and another walks past.
	BufferHandle Device::CreateBufferRouted(const BufferDesc & desc, Error * error) noexcept
	{
		// Settled when the device was created so this is a load and a branch, not two questions asked per create.
		if (!m_blocks->AllocatesPlaced())
		{
			const std::scoped_lock guard(m_blocks->Guard(ResourceType::eBuffer));
			return m_blocks->Device().core->createBuffer(m_impl, desc, error);
		}

		/*
		 * Each step that reaches the backend takes the guard on its own and not one held across all three. The middle step calls into host allocator code that may
		 * create heaps of its own. Holding a guard across it would lock across a call the RHI does not control while the heap create underneath takes a second.
		 *
		 * Splitting costs an atomicity the sequence never needed. A footprint is a function of the desc, not device state.
		 */
		MemoryInfo info{};
		{
			const std::scoped_lock guard(m_blocks->Guard(ResourceType::eBuffer));
			if (!m_blocks->Device().placedMemory->getBufferMemoryInfo(m_impl, desc, &info, error))
			{
				return {};
			}
		}

		MemorySpan span{};
		if (!ReserveSpan(*this, m_blocks->Allocator(), info, HeapTypeForUsage(desc.memory), true, desc.debugName, span, error))
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
			const std::scoped_lock guard(m_blocks->Guard(ResourceType::eBuffer));
			handle = m_blocks->Device().placedMemory->createPlacedBuffer(m_impl, placed, error);
			if (handle.IsValid())
			{
				// Under the guard with the create, since the tracker is partitioned by kind and that is its whole synchronization.
				RecordSpan(*m_blocks, ResourceType::eBuffer, handle.index, handle.generation, span);
			}
		}

		if (!handle.IsValid())
		{
			// Outside the guard, for the same reason the reservation was taken outside one.
			detail::CheckNoGuardHeld();
			m_blocks->Allocator()->Free(*this, span);
		}

		return handle;
	}

	Result<BufferHandle> Device::CreateBufferWithResult(const BufferDesc & desc) noexcept
	{
		Error error{};
		return AsResult(CreateBuffer(desc, error), error);
	}

	TextureHandle Device::CreateTexture(const TextureDesc & desc) noexcept
	{
		Error error{};
		return CreateTexture(desc, error);
	}

	TextureHandle Device::CreateTexture(const TextureDesc & desc, Error & error) noexcept
	{
		error = {};

		TextureHandle produced = CreateTextureRouted(desc, &error);
		Settle(produced, error);
		return produced;
	}

	TextureHandle Device::CreateTextureRouted(const TextureDesc & desc, Error * error) noexcept
	{
		// Settled when the device was created so this is a load and a branch, not two questions asked per create.
		if (!m_blocks->AllocatesPlaced())
		{
			const std::scoped_lock guard(m_blocks->Guard(ResourceType::eTexture));
			return m_blocks->Device().core->createTexture(m_impl, desc, error);
		}

		/*
		 * Each step that reaches the backend takes the guard on its own and not one held across all three. The middle step calls into host allocator code that may
		 * create heaps of its own. Holding a guard across it would lock across a call the RHI does not control while the heap create underneath takes a second.
		 *
		 * Splitting costs an atomicity the sequence never needed. A footprint is a function of the desc, not device state.
		 */
		MemoryInfo info{};
		{
			const std::scoped_lock guard(m_blocks->Guard(ResourceType::eTexture));
			if (!m_blocks->Device().placedMemory->getTextureMemoryInfo(m_impl, desc, &info, error))
			{
				return {};
			}
		}

		/*
		 * Through the same mapping a buffer create uses so the heap a routed texture lands in is the one an unrouted create would have picked. Forcing device-local
		 * here would quietly ignore a TextureDesc::memory the backends do read. Where a backend cannot place a texture in that class of heap (Direct3D 12 bars UPLOAD
		 * and READBACK) the placed create below reports it.
		 */
		MemorySpan span{};
		if (!ReserveSpan(*this, m_blocks->Allocator(), info, HeapTypeForUsage(desc.memory), false, desc.debugName, span, error))
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
			const std::scoped_lock guard(m_blocks->Guard(ResourceType::eTexture));
			handle = m_blocks->Device().placedMemory->createPlacedTexture(m_impl, placed, error);
			if (handle.IsValid())
			{
				RecordSpan(*m_blocks, ResourceType::eTexture, handle.index, handle.generation, span);
			}
		}

		if (!handle.IsValid())
		{
			detail::CheckNoGuardHeld();
			m_blocks->Allocator()->Free(*this, span);
		}

		return handle;
	}

	Result<TextureHandle> Device::CreateTextureWithResult(const TextureDesc & desc) noexcept
	{
		Error error{};
		return AsResult(CreateTexture(desc, error), error);
	}

	TextureViewHandle Device::CreateTextureView(TextureHandle texture, const TextureViewDesc & desc) noexcept
	{
		Error error{};
		return CreateTextureView(texture, desc, error);
	}

	TextureViewHandle Device::CreateTextureView(TextureHandle texture, const TextureViewDesc & desc, Error & error) noexcept
	{
		error = {};
		const std::scoped_lock guard(m_blocks->Guard(ResourceType::eTextureView));
		TextureViewHandle produced = m_blocks->Device().core->createTextureView(m_impl, texture, desc, &error);
		Settle(produced, error);
		return produced;
	}

	Result<TextureViewHandle> Device::CreateTextureViewWithResult(TextureHandle texture, const TextureViewDesc & desc) noexcept
	{
		Error error{};
		return AsResult(CreateTextureView(texture, desc, error), error);
	}

	SamplerHandle Device::CreateSampler(const SamplerDesc & desc) noexcept
	{
		Error error{};
		return CreateSampler(desc, error);
	}

	SamplerHandle Device::CreateSampler(const SamplerDesc & desc, Error & error) noexcept
	{
		error = {};
		const std::scoped_lock guard(m_blocks->Guard(ResourceType::eSampler));
		SamplerHandle produced = m_blocks->Device().core->createSampler(m_impl, desc, &error);
		Settle(produced, error);
		return produced;
	}

	Result<SamplerHandle> Device::CreateSamplerWithResult(const SamplerDesc & desc) noexcept
	{
		Error error{};
		return AsResult(CreateSampler(desc, error), error);
	}

	HeapHandle Device::CreateHeap(const HeapDesc & desc) noexcept
	{
		Error error{};
		return CreateHeap(desc, error);
	}

	HeapHandle Device::CreateHeap(const HeapDesc & desc, Error & error) noexcept
	{
		error = {};
		if (m_blocks->Device().placedMemory == nullptr)
		{
			return Decline<HeapHandle>(&error, kNoPlacedMemory);
		}

		const std::scoped_lock guard(m_blocks->Guard(ResourceType::eHeap));
		HeapHandle produced = m_blocks->Device().placedMemory->createHeap(m_impl, desc, &error);
		Settle(produced, error);
		return produced;
	}

	Result<HeapHandle> Device::CreateHeapWithResult(const HeapDesc & desc) noexcept
	{
		Error error{};
		return AsResult(CreateHeap(desc, error), error);
	}

	BufferHandle Device::CreatePlacedBuffer(const PlacedBufferDesc & desc) noexcept
	{
		Error error{};
		return CreatePlacedBuffer(desc, error);
	}

	BufferHandle Device::CreatePlacedBuffer(const PlacedBufferDesc & desc, Error & error) noexcept
	{
		error = {};
		if (m_blocks->Device().placedMemory == nullptr)
		{
			return Decline<BufferHandle>(&error, kNoPlacedMemory);
		}

		const std::scoped_lock guard(m_blocks->Guard(ResourceType::eBuffer));
		BufferHandle produced = m_blocks->Device().placedMemory->createPlacedBuffer(m_impl, desc, &error);
		Settle(produced, error);
		return produced;
	}

	Result<BufferHandle> Device::CreatePlacedBufferWithResult(const PlacedBufferDesc & desc) noexcept
	{
		Error error{};
		return AsResult(CreatePlacedBuffer(desc, error), error);
	}

	TextureHandle Device::CreatePlacedTexture(const PlacedTextureDesc & desc) noexcept
	{
		Error error{};
		return CreatePlacedTexture(desc, error);
	}

	TextureHandle Device::CreatePlacedTexture(const PlacedTextureDesc & desc, Error & error) noexcept
	{
		error = {};
		if (m_blocks->Device().placedMemory == nullptr)
		{
			return Decline<TextureHandle>(&error, kNoPlacedMemory);
		}

		const std::scoped_lock guard(m_blocks->Guard(ResourceType::eTexture));
		TextureHandle produced = m_blocks->Device().placedMemory->createPlacedTexture(m_impl, desc, &error);
		Settle(produced, error);
		return produced;
	}

	Result<TextureHandle> Device::CreatePlacedTextureWithResult(const PlacedTextureDesc & desc) noexcept
	{
		Error error{};
		return AsResult(CreatePlacedTexture(desc, error), error);
	}

	bool Device::GetTextureMemoryInfo(const TextureDesc & desc, MemoryInfo & out) const noexcept
	{
		Error error{};
		return GetTextureMemoryInfo(desc, out, error);
	}

	bool Device::GetTextureMemoryInfo(const TextureDesc & desc, MemoryInfo & out, Error & error) const noexcept
	{
		out	  = {};
		error = {};

		if (m_blocks->Device().placedMemory == nullptr)
		{
			return Decline<bool>(&error, kNoPlacedMemory);
		}

		bool answered = m_blocks->Device().placedMemory->getTextureMemoryInfo(m_impl, desc, &out, &error);
		Settle(answered, error);
		if (!answered)
		{
			out = {};
		}

		return answered;
	}

	Result<MemoryInfo> Device::GetTextureMemoryInfoWithResult(const TextureDesc & desc) const noexcept
	{
		MemoryInfo out{};
		Error error{};
		static_cast<void>(GetTextureMemoryInfo(desc, out, error));
		return AsResult(out, error);
	}

	bool Device::GetBufferMemoryInfo(const BufferDesc & desc, MemoryInfo & out) const noexcept
	{
		Error error{};
		return GetBufferMemoryInfo(desc, out, error);
	}

	bool Device::GetBufferMemoryInfo(const BufferDesc & desc, MemoryInfo & out, Error & error) const noexcept
	{
		out	  = {};
		error = {};

		if (m_blocks->Device().placedMemory == nullptr)
		{
			return Decline<bool>(&error, kNoPlacedMemory);
		}

		bool answered = m_blocks->Device().placedMemory->getBufferMemoryInfo(m_impl, desc, &out, &error);
		Settle(answered, error);
		if (!answered)
		{
			out = {};
		}

		return answered;
	}

	Result<MemoryInfo> Device::GetBufferMemoryInfoWithResult(const BufferDesc & desc) const noexcept
	{
		MemoryInfo out{};
		Error error{};
		static_cast<void>(GetBufferMemoryInfo(desc, out, error));
		return AsResult(out, error);
	}

	DescriptorSetLayoutHandle Device::CreateDescriptorSetLayout(const DescriptorSetLayoutDesc & desc) noexcept
	{
		Error error{};
		return CreateDescriptorSetLayout(desc, error);
	}

	DescriptorSetLayoutHandle Device::CreateDescriptorSetLayout(const DescriptorSetLayoutDesc & desc, Error & error) noexcept
	{
		error = {};
		const std::scoped_lock guard(m_blocks->Guard(ResourceType::eDescriptorSetLayout));
		DescriptorSetLayoutHandle produced = m_blocks->Device().core->createDescriptorSetLayout(m_impl, desc, &error);
		Settle(produced, error);
		return produced;
	}

	Result<DescriptorSetLayoutHandle> Device::CreateDescriptorSetLayoutWithResult(const DescriptorSetLayoutDesc & desc) noexcept
	{
		Error error{};
		return AsResult(CreateDescriptorSetLayout(desc, error), error);
	}

	PipelineLayoutHandle Device::CreatePipelineLayout(const PipelineLayoutDesc & desc) noexcept
	{
		Error error{};
		return CreatePipelineLayout(desc, error);
	}

	PipelineLayoutHandle Device::CreatePipelineLayout(const PipelineLayoutDesc & desc, Error & error) noexcept
	{
		error = {};
		const std::scoped_lock guard(m_blocks->Guard(ResourceType::ePipelineLayout));
		PipelineLayoutHandle produced = m_blocks->Device().core->createPipelineLayout(m_impl, desc, &error);
		Settle(produced, error);
		return produced;
	}

	Result<PipelineLayoutHandle> Device::CreatePipelineLayoutWithResult(const PipelineLayoutDesc & desc) noexcept
	{
		Error error{};
		return AsResult(CreatePipelineLayout(desc, error), error);
	}

	GraphicsPipelineHandle Device::CreateGraphicsPipeline(const GraphicsPipelineDesc & desc) noexcept
	{
		Error error{};
		return CreateGraphicsPipeline(desc, error);
	}

	GraphicsPipelineHandle Device::CreateGraphicsPipeline(const GraphicsPipelineDesc & desc, Error & error) noexcept
	{
		error = {};
		const std::scoped_lock guard(m_blocks->Guard(ResourceType::eGraphicsPipeline));
		GraphicsPipelineHandle produced = m_blocks->Device().core->createGraphicsPipeline(m_impl, desc, &error);
		Settle(produced, error);
		return produced;
	}

	Result<GraphicsPipelineHandle> Device::CreateGraphicsPipelineWithResult(const GraphicsPipelineDesc & desc) noexcept
	{
		Error error{};
		return AsResult(CreateGraphicsPipeline(desc, error), error);
	}

	ComputePipelineHandle Device::CreateComputePipeline(const ComputePipelineDesc & desc) noexcept
	{
		Error error{};
		return CreateComputePipeline(desc, error);
	}

	ComputePipelineHandle Device::CreateComputePipeline(const ComputePipelineDesc & desc, Error & error) noexcept
	{
		error = {};
		const std::scoped_lock guard(m_blocks->Guard(ResourceType::eComputePipeline));
		ComputePipelineHandle produced = m_blocks->Device().core->createComputePipeline(m_impl, desc, &error);
		Settle(produced, error);
		return produced;
	}

	Result<ComputePipelineHandle> Device::CreateComputePipelineWithResult(const ComputePipelineDesc & desc) noexcept
	{
		Error error{};
		return AsResult(CreateComputePipeline(desc, error), error);
	}

	RayTracingPipelineHandle Device::CreateRayTracingPipeline(const RayTracingPipelineDesc & desc) noexcept
	{
		Error error{};
		return CreateRayTracingPipeline(desc, error);
	}

	RayTracingPipelineHandle Device::CreateRayTracingPipeline(const RayTracingPipelineDesc & desc, Error & error) noexcept
	{
		error = {};
		if (m_blocks->Device().rayTracing == nullptr)
		{
			return Decline<RayTracingPipelineHandle>(&error, kNoRayTracing);
		}

		const std::scoped_lock guard(m_blocks->Guard(ResourceType::eRayTracingPipeline));
		RayTracingPipelineHandle produced = m_blocks->Device().rayTracing->createRayTracingPipeline(m_impl, desc, &error);
		Settle(produced, error);
		return produced;
	}

	Result<RayTracingPipelineHandle> Device::CreateRayTracingPipelineWithResult(const RayTracingPipelineDesc & desc) noexcept
	{
		Error error{};
		return AsResult(CreateRayTracingPipeline(desc, error), error);
	}

	PipelineCacheHandle Device::CreatePipelineCache(const PipelineCacheDesc & desc) noexcept
	{
		Error error{};
		return CreatePipelineCache(desc, error);
	}

	PipelineCacheHandle Device::CreatePipelineCache(const PipelineCacheDesc & desc, Error & error) noexcept
	{
		error = {};
		if (m_blocks->Device().pipelineCache == nullptr)
		{
			return Decline<PipelineCacheHandle>(&error, kNoPipelineCache);
		}

		const std::scoped_lock guard(m_blocks->Guard(ResourceType::ePipelineCache));
		PipelineCacheHandle produced = m_blocks->Device().pipelineCache->createPipelineCache(m_impl, desc, &error);
		Settle(produced, error);
		return produced;
	}

	Result<PipelineCacheHandle> Device::CreatePipelineCacheWithResult(const PipelineCacheDesc & desc) noexcept
	{
		Error error{};
		return AsResult(CreatePipelineCache(desc, error), error);
	}

	bool Device::GetPipelineCacheData(PipelineCacheHandle cache, PipelineCacheData & out) noexcept
	{
		Error error{};
		return GetPipelineCacheData(cache, out, error);
	}

	bool Device::GetPipelineCacheData(PipelineCacheHandle cache, PipelineCacheData & out, Error & error) noexcept
	{
		out	  = {};
		error = {};

		if (m_blocks->Device().pipelineCache == nullptr)
		{
			return Decline<bool>(&error, kNoPipelineCache);
		}

		bool answered = m_blocks->Device().pipelineCache->getPipelineCacheData(m_impl, cache, &out, &error);
		Settle(answered, error);
		if (!answered)
		{
			out = {};
		}

		return answered;
	}

	Result<PipelineCacheData> Device::GetPipelineCacheDataWithResult(PipelineCacheHandle cache) noexcept
	{
		PipelineCacheData out{};
		Error error{};
		static_cast<void>(GetPipelineCacheData(cache, out, error));
		return AsResult(out, error);
	}

	AccelerationStructureHandle Device::CreateAccelerationStructure(const AccelerationStructureDesc & desc) noexcept
	{
		Error error{};
		return CreateAccelerationStructure(desc, error);
	}

	AccelerationStructureHandle Device::CreateAccelerationStructure(const AccelerationStructureDesc & desc, Error & error) noexcept
	{
		error = {};
		if (m_blocks->Device().rayTracing == nullptr)
		{
			return Decline<AccelerationStructureHandle>(&error, kNoRayTracing);
		}

		const std::scoped_lock guard(m_blocks->Guard(ResourceType::eAccelerationStructure));
		AccelerationStructureHandle produced = m_blocks->Device().rayTracing->createAccelerationStructure(m_impl, desc, &error);
		Settle(produced, error);
		return produced;
	}

	Result<AccelerationStructureHandle> Device::CreateAccelerationStructureWithResult(const AccelerationStructureDesc & desc) noexcept
	{
		Error error{};
		return AsResult(CreateAccelerationStructure(desc, error), error);
	}

	QueryPoolHandle Device::CreateQueryPool(const QueryPoolDesc & desc) noexcept
	{
		Error error{};
		return CreateQueryPool(desc, error);
	}

	QueryPoolHandle Device::CreateQueryPool(const QueryPoolDesc & desc, Error & error) noexcept
	{
		error = {};
		if (m_blocks->Device().query == nullptr)
		{
			return Decline<QueryPoolHandle>(&error, kNoQuery);
		}

		const std::scoped_lock guard(m_blocks->Guard(ResourceType::eQueryPool));
		QueryPoolHandle produced = m_blocks->Device().query->createQueryPool(m_impl, desc, &error);
		Settle(produced, error);
		return produced;
	}

	Result<QueryPoolHandle> Device::CreateQueryPoolWithResult(const QueryPoolDesc & desc) noexcept
	{
		Error error{};
		return AsResult(CreateQueryPool(desc, error), error);
	}

	TimelineHandle Device::CreateTimeline(const TimelineDesc & desc) noexcept
	{
		Error error{};
		return CreateTimeline(desc, error);
	}

	TimelineHandle Device::CreateTimeline(const TimelineDesc & desc, Error & error) noexcept
	{
		error = {};
		const std::scoped_lock guard(m_blocks->Guard(ResourceType::eTimeline));
		TimelineHandle produced = m_blocks->Device().core->createTimeline(m_impl, desc, &error);
		Settle(produced, error);
		return produced;
	}

	Result<TimelineHandle> Device::CreateTimelineWithResult(const TimelineDesc & desc) noexcept
	{
		Error error{};
		return AsResult(CreateTimeline(desc, error), error);
	}

	BinarySemaphoreHandle Device::CreateBinarySemaphore(const BinarySemaphoreDesc & desc) noexcept
	{
		Error error{};
		return CreateBinarySemaphore(desc, error);
	}

	BinarySemaphoreHandle Device::CreateBinarySemaphore(const BinarySemaphoreDesc & desc, Error & error) noexcept
	{
		error = {};
		const std::scoped_lock guard(m_blocks->Guard(ResourceType::eBinarySemaphore));
		BinarySemaphoreHandle produced = m_blocks->Device().core->createBinarySemaphore(m_impl, desc, &error);
		Settle(produced, error);
		return produced;
	}

	Result<BinarySemaphoreHandle> Device::CreateBinarySemaphoreWithResult(const BinarySemaphoreDesc & desc) noexcept
	{
		Error error{};
		return AsResult(CreateBinarySemaphore(desc, error), error);
	}

	DescriptorArena Device::CreateDescriptorArena(const DescriptorArenaDesc & desc) noexcept
	{
		Error error{};
		return CreateDescriptorArena(desc, error);
	}

	DescriptorArena Device::CreateDescriptorArena(const DescriptorArenaDesc & desc, Error & error) noexcept
	{
		error = {};
		const std::scoped_lock guard(m_blocks->ObjectGuard());
		void * impl						 = m_blocks->Device().core->createDescriptorArena(m_impl, desc, &error);
		const DescriptorArenaApi * block = detail::CheckedBlock<DescriptorArenaApi>(impl, &error);
		Settle(block, error);

		return block != nullptr ? detail::FacadeBuilder::MakeDescriptorArena(impl, block, m_blocks) : DescriptorArena{};
	}

	Result<DescriptorArena> Device::CreateDescriptorArenaWithResult(const DescriptorArenaDesc & desc) noexcept
	{
		Error error{};
		return AsResult(CreateDescriptorArena(desc, error), error);
	}

	CommandPool Device::CreateCommandPool(const CommandPoolDesc & desc) noexcept
	{
		Error error{};
		return CreateCommandPool(desc, error);
	}

	CommandPool Device::CreateCommandPool(const CommandPoolDesc & desc, Error & error) noexcept
	{
		error = {};
		const std::scoped_lock guard(m_blocks->ObjectGuard());
		void * impl		   = m_blocks->Device().core->createCommandPool(m_impl, desc, &error);
		const auto * block = detail::CheckedBlock<CommandPoolApi>(impl, &error);
		Settle(block, error);

		return block != nullptr ? detail::FacadeBuilder::MakeCommandPool(impl, block, m_blocks) : CommandPool{};
	}

	Result<CommandPool> Device::CreateCommandPoolWithResult(const CommandPoolDesc & desc) noexcept
	{
		Error error{};
		return AsResult(CreateCommandPool(desc, error), error);
	}

	Swapchain Device::CreateSwapchain(const SwapchainDesc & desc) noexcept
	{
		Error error{};
		return CreateSwapchain(desc, error);
	}

	Swapchain Device::CreateSwapchain(const SwapchainDesc & desc, Error & error) noexcept
	{
		error = {};
		if (m_blocks->Device().present == nullptr)
		{
			return Decline<Swapchain>(&error, kNoPresent);
		}

		const std::scoped_lock guard(m_blocks->ObjectGuard());
		void * impl				   = m_blocks->Device().present->createSwapchain(m_impl, desc, &error);
		const SwapchainApi * block = detail::CheckedBlock<SwapchainApi>(impl, &error);
		Settle(block, error);

		return block != nullptr ? detail::FacadeBuilder::MakeSwapchain(impl, block) : Swapchain{};
	}

	Result<Swapchain> Device::CreateSwapchainWithResult(const SwapchainDesc & desc) noexcept
	{
		Error error{};
		return AsResult(CreateSwapchain(desc, error), error);
	}

	Queue Device::GetQueue(QueueType type, std::uint32_t index) noexcept
	{
		Error error{};
		return GetQueue(type, index, error);
	}

	Queue Device::GetQueue(QueueType type, std::uint32_t index, Error & error) noexcept
	{
		error = {};
		const std::scoped_lock guard(m_blocks->ObjectGuard());
		void * impl				   = m_blocks->Device().core->getQueue(m_impl, type, index, &error);
		const QueueBlocks * blocks = detail::CheckedChild<QueueApi>(impl, &error) ? m_blocks->Queue(impl) : nullptr;
		Settle(blocks, error);

		return blocks != nullptr ? detail::FacadeBuilder::MakeQueue(impl, blocks) : Queue{};
	}

	Result<Queue> Device::GetQueueWithResult(QueueType type, std::uint32_t index) noexcept
	{
		Error error{};
		return AsResult(GetQueue(type, index, error), error);
	}

	std::uint32_t Device::GetQueueCount(QueueType type) const noexcept
	{
		const DeviceCaps & caps = m_blocks->Device().core->getCaps(m_impl);
		switch (type)
		{
		case QueueType::eGraphics: return caps.graphicsQueueCount;
		case QueueType::eCompute:  return caps.computeQueueCount;
		case QueueType::eCopy:	   return caps.copyQueueCount;
		}

		return 0;
	}

	// Device mapping.

	MappedMemory Device::Map(BufferHandle buffer, const MapDesc & desc) noexcept
	{
		Error error{};
		return Map(buffer, desc, error);
	}

	MappedMemory Device::Map(BufferHandle buffer, const MapDesc & desc, Error & error) noexcept
	{
		error				  = {};
		MappedMemory produced = m_blocks->Device().core->map(m_impl, buffer, desc, &error);
		Settle(produced, error);
		return produced;
	}

	Result<MappedMemory> Device::MapWithResult(BufferHandle buffer, const MapDesc & desc) noexcept
	{
		Error error{};
		return AsResult(Map(buffer, desc, error), error);
	}

	bool Device::Unmap(BufferHandle buffer) noexcept
	{
		return m_blocks->Device().core->unmap(m_impl, buffer, nullptr);
	}

	bool Device::Unmap(BufferHandle buffer, Error & error) noexcept
	{
		error = {};
		return m_blocks->Device().core->unmap(m_impl, buffer, &error);
	}

	bool Device::FlushMappedRange(BufferHandle buffer, std::uint64_t offset, std::uint64_t size) noexcept
	{
		return m_blocks->Device().core->flushMappedRange(m_impl, buffer, offset, size, nullptr);
	}

	bool Device::FlushMappedRange(BufferHandle buffer, std::uint64_t offset, std::uint64_t size, Error & error) noexcept
	{
		error = {};
		return m_blocks->Device().core->flushMappedRange(m_impl, buffer, offset, size, &error);
	}

	bool Device::InvalidateMappedRange(BufferHandle buffer, std::uint64_t offset, std::uint64_t size) noexcept
	{
		return m_blocks->Device().core->invalidateMappedRange(m_impl, buffer, offset, size, nullptr);
	}

	bool Device::InvalidateMappedRange(BufferHandle buffer, std::uint64_t offset, std::uint64_t size, Error & error) noexcept
	{
		error = {};
		return m_blocks->Device().core->invalidateMappedRange(m_impl, buffer, offset, size, &error);
	}

	// Descriptor updates. Guarded, not left to the backend: on MoltenVK a texture write is where the image's MTLTexture gets lazily materialized and that is not
	// thread-safe so concurrent writes from several recording threads need serializing somewhere.

	bool Device::UpdateDescriptors(std::span<const DescriptorWriteBuffer> writes) noexcept
	{
		const std::scoped_lock guard(m_blocks->Guard(ResourceType::eDescriptorSet));
		return m_blocks->Device().core->updateDescriptorsBuffer(m_impl, writes, nullptr);
	}

	bool Device::UpdateDescriptors(std::span<const DescriptorWriteBuffer> writes, Error & error) noexcept
	{
		error = {};
		const std::scoped_lock guard(m_blocks->Guard(ResourceType::eDescriptorSet));
		return m_blocks->Device().core->updateDescriptorsBuffer(m_impl, writes, &error);
	}

	bool Device::UpdateDescriptors(std::span<const DescriptorWriteTexture> writes) noexcept
	{
		const std::scoped_lock guard(m_blocks->Guard(ResourceType::eDescriptorSet));
		return m_blocks->Device().core->updateDescriptorsTexture(m_impl, writes, nullptr);
	}

	bool Device::UpdateDescriptors(std::span<const DescriptorWriteTexture> writes, Error & error) noexcept
	{
		error = {};
		const std::scoped_lock guard(m_blocks->Guard(ResourceType::eDescriptorSet));
		return m_blocks->Device().core->updateDescriptorsTexture(m_impl, writes, &error);
	}

	bool Device::UpdateDescriptors(std::span<const DescriptorWriteSampler> writes) noexcept
	{
		const std::scoped_lock guard(m_blocks->Guard(ResourceType::eDescriptorSet));
		return m_blocks->Device().core->updateDescriptorsSampler(m_impl, writes, nullptr);
	}

	bool Device::UpdateDescriptors(std::span<const DescriptorWriteSampler> writes, Error & error) noexcept
	{
		error = {};
		const std::scoped_lock guard(m_blocks->Guard(ResourceType::eDescriptorSet));
		return m_blocks->Device().core->updateDescriptorsSampler(m_impl, writes, &error);
	}

	bool Device::UpdateDescriptors(std::span<const DescriptorWriteAccelerationStructure> writes) noexcept
	{
		return m_blocks->Device().rayTracing != nullptr ? m_blocks->Device().rayTracing->updateDescriptorsAccelerationStructure(m_impl, writes, nullptr)
														: Decline<bool>(nullptr, kNoRayTracing);
	}

	bool Device::UpdateDescriptors(std::span<const DescriptorWriteAccelerationStructure> writes, Error & error) noexcept
	{
		error = {};
		return m_blocks->Device().rayTracing != nullptr ? m_blocks->Device().rayTracing->updateDescriptorsAccelerationStructure(m_impl, writes, &error)
														: Decline<bool>(&error, kNoRayTracing);
	}

	// Device memory, residency, profiling and capabilities.

	bool Device::QueryMemoryBudget(HeapType heap, MemoryBudgetInfo & out) const noexcept
	{
		Error error{};
		return QueryMemoryBudget(heap, out, error);
	}

	bool Device::QueryMemoryBudget(HeapType heap, MemoryBudgetInfo & out, Error & error) const noexcept
	{
		out	  = {};
		error = {};

		if (m_blocks->Device().residency == nullptr)
		{
			return Decline<bool>(&error, kNoResidency);
		}

		bool answered = m_blocks->Device().residency->queryMemoryBudget(m_impl, heap, &out, &error);
		Settle(answered, error);
		if (!answered)
		{
			out = {};
		}

		return answered;
	}

	Result<MemoryBudgetInfo> Device::QueryMemoryBudgetWithResult(HeapType heap) const noexcept
	{
		MemoryBudgetInfo out{};
		Error error{};
		static_cast<void>(QueryMemoryBudget(heap, out, error));
		return AsResult(out, error);
	}

	bool Device::SetResidencyPriority(std::span<const ResidencyPriorityDesc> priorities) noexcept
	{
		return m_blocks->Device().residency != nullptr ? m_blocks->Device().residency->setResidencyPriority(m_impl, priorities, nullptr)
													   : Decline<bool>(nullptr, kNoResidency);
	}

	bool Device::SetResidencyPriority(std::span<const ResidencyPriorityDesc> priorities, Error & error) noexcept
	{
		error = {};
		return m_blocks->Device().residency != nullptr ? m_blocks->Device().residency->setResidencyPriority(m_impl, priorities, &error)
													   : Decline<bool>(&error, kNoResidency);
	}

	bool Device::CalibrateTimestamp(QueueType queueType, TimestampCalibration & out) const noexcept
	{
		Error error{};
		return CalibrateTimestamp(queueType, out, error);
	}

	bool Device::CalibrateTimestamp(QueueType queueType, TimestampCalibration & out, Error & error) const noexcept
	{
		out	  = {};
		error = {};

		if (m_blocks->Device().query == nullptr)
		{
			return Decline<bool>(&error, kNoQuery);
		}

		bool answered = m_blocks->Device().query->calibrateTimestamp(m_impl, queueType, &out, &error);
		Settle(answered, error);
		if (!answered)
		{
			out = {};
		}

		return answered;
	}

	Result<TimestampCalibration> Device::CalibrateTimestampWithResult(QueueType queueType) const noexcept
	{
		TimestampCalibration out{};
		Error error{};
		static_cast<void>(CalibrateTimestamp(queueType, out, error));
		return AsResult(out, error);
	}

	const DeviceCaps & Device::GetCaps() const noexcept
	{
		/*
		 * The copy the RHI derived, not the one the backend filled. Limits and scalar features are the backend's own answers and came across untouched. The
		 * categorical ones were taken off the blocks it published, which is what leaves a caller one thing to read, not two that can disagree.
		 */
		return m_blocks->Caps();
	}

	FormatSupport Device::GetFormatSupport(Format format) const noexcept
	{
		FormatSupport support = m_blocks->Device().core->getFormatSupport(m_impl, format);

		// Whether the device has a scaled blit at all is one answer, so it is given once, not repeated per format. A backend without one only has to leave
		// supportsScaledBlit false and every format follows, which is what stops the two disagreeing.
		if (!m_blocks->Caps().supportsScaledBlit)
		{
			support.blitSrc = false;
			support.blitDst = false;
		}

		return support;
	}

	const AdapterInfo & Device::GetAdapterInfo() const noexcept
	{
		return m_blocks->Device().core->getAdapterInfo(m_impl);
	}

	bool Device::GetTextureInfo(const TextureHandle texture, TextureInfo & out) const noexcept
	{
		Error error{};
		return GetTextureInfo(texture, out, error);
	}

	bool Device::GetTextureInfo(const TextureHandle texture, TextureInfo & out, Error & error) const noexcept
	{
		out	  = {};
		error = {};

		if (m_blocks->Device().introspection == nullptr)
		{
			return Decline<bool>(&error, kNoIntrospection);
		}

		bool answered = m_blocks->Device().introspection->getTextureInfo(m_impl, texture, &out, &error);
		Settle(answered, error);
		if (!answered)
		{
			out = {};
		}

		return answered;
	}

	Result<TextureInfo> Device::GetTextureInfoWithResult(const TextureHandle texture) const noexcept
	{
		TextureInfo out{};
		Error error{};
		static_cast<void>(GetTextureInfo(texture, out, error));
		return AsResult(out, error);
	}

	bool Device::GetBufferInfo(const BufferHandle buffer, BufferInfo & out) const noexcept
	{
		Error error{};
		return GetBufferInfo(buffer, out, error);
	}

	bool Device::GetBufferInfo(const BufferHandle buffer, BufferInfo & out, Error & error) const noexcept
	{
		out	  = {};
		error = {};

		if (m_blocks->Device().introspection == nullptr)
		{
			return Decline<bool>(&error, kNoIntrospection);
		}

		bool answered = m_blocks->Device().introspection->getBufferInfo(m_impl, buffer, &out, &error);
		Settle(answered, error);
		if (!answered)
		{
			out = {};
		}

		return answered;
	}

	Result<BufferInfo> Device::GetBufferInfoWithResult(const BufferHandle buffer) const noexcept
	{
		BufferInfo out{};
		Error error{};
		static_cast<void>(GetBufferInfo(buffer, out, error));
		return AsResult(out, error);
	}

	ValidationMessageCounts Device::GetValidationMessageCounts() const noexcept
	{
		// Every backend fills this entry. One with no validation channel to read reports zeros and says so through caps, which is what tells a caller that a zero
		// here is not evidence of a clean run.
		return m_blocks->Device().core->getValidationMessageCounts(m_impl);
	}

	// All typed destroy overloads route through the shared backend destroy entry.

	bool Device::Destroy(BufferHandle handle, const DestroyDesc & desc) noexcept
	{
		MemorySpan released{};
		bool hasSpan = false;

		bool destroyed = false;
		{
			const std::scoped_lock guard(m_blocks->Guard(ResourceType::eBuffer));
			destroyed = m_blocks->Device().core->destroy(m_impl,
				ResourceType::eBuffer,
				{
					.index		= handle.index,
					.generation = handle.generation,
				},
				desc,
				nullptr);
			if (destroyed)
			{
				hasSpan = TakeRetiredSpan(*m_blocks, ResourceType::eBuffer, handle.index, handle.generation, desc, released);
			}
		}

		// Outside the guard: the allocator is handed this device and frees a heap through it, which is another create.
		if (hasSpan)
		{
			detail::CheckNoGuardHeld();
			m_blocks->Allocator()->Free(*this, released);
		}

		return destroyed;
	}

	bool Device::Destroy(BufferHandle handle, const DestroyDesc & desc, Error & error) noexcept
	{
		error = {};
		MemorySpan released{};
		bool hasSpan = false;

		bool destroyed = false;
		{
			const std::scoped_lock guard(m_blocks->Guard(ResourceType::eBuffer));
			destroyed = m_blocks->Device().core->destroy(m_impl,
				ResourceType::eBuffer,
				{
					.index		= handle.index,
					.generation = handle.generation,
				},
				desc,
				&error);
			if (destroyed)
			{
				hasSpan = TakeRetiredSpan(*m_blocks, ResourceType::eBuffer, handle.index, handle.generation, desc, released);
			}
		}

		// Outside the guard: the allocator is handed this device and frees a heap through it, which is another create.
		if (hasSpan)
		{
			detail::CheckNoGuardHeld();
			m_blocks->Allocator()->Free(*this, released);
		}

		return destroyed;
	}

	bool Device::Destroy(TextureHandle handle, const DestroyDesc & desc) noexcept
	{
		MemorySpan released{};
		bool hasSpan = false;

		bool destroyed = false;
		{
			const std::scoped_lock guard(m_blocks->Guard(ResourceType::eTexture));
			destroyed = m_blocks->Device().core->destroy(m_impl,
				ResourceType::eTexture,
				{
					.index		= handle.index,
					.generation = handle.generation,
				},
				desc,
				nullptr);
			if (destroyed)
			{
				hasSpan = TakeRetiredSpan(*m_blocks, ResourceType::eTexture, handle.index, handle.generation, desc, released);
			}
		}

		// Outside the guard: the allocator is handed this device and frees a heap through it, which is another create.
		if (hasSpan)
		{
			detail::CheckNoGuardHeld();
			m_blocks->Allocator()->Free(*this, released);
		}

		return destroyed;
	}

	bool Device::Destroy(TextureHandle handle, const DestroyDesc & desc, Error & error) noexcept
	{
		error = {};
		MemorySpan released{};
		bool hasSpan = false;

		bool destroyed = false;
		{
			const std::scoped_lock guard(m_blocks->Guard(ResourceType::eTexture));
			destroyed = m_blocks->Device().core->destroy(m_impl,
				ResourceType::eTexture,
				{
					.index		= handle.index,
					.generation = handle.generation,
				},
				desc,
				&error);
			if (destroyed)
			{
				hasSpan = TakeRetiredSpan(*m_blocks, ResourceType::eTexture, handle.index, handle.generation, desc, released);
			}
		}

		// Outside the guard: the allocator is handed this device and frees a heap through it, which is another create.
		if (hasSpan)
		{
			detail::CheckNoGuardHeld();
			m_blocks->Allocator()->Free(*this, released);
		}

		return destroyed;
	}

	bool Device::Destroy(TextureViewHandle handle, const DestroyDesc & desc) noexcept
	{
		const std::scoped_lock guard(m_blocks->Guard(ResourceType::eTextureView));
		return m_blocks->Device().core->destroy(m_impl,
			ResourceType::eTextureView,
			{
				.index		= handle.index,
				.generation = handle.generation,
			},
			desc,
			nullptr);
	}

	bool Device::Destroy(TextureViewHandle handle, const DestroyDesc & desc, Error & error) noexcept
	{
		error = {};
		const std::scoped_lock guard(m_blocks->Guard(ResourceType::eTextureView));
		return m_blocks->Device().core->destroy(m_impl,
			ResourceType::eTextureView,
			{
				.index		= handle.index,
				.generation = handle.generation,
			},
			desc,
			&error);
	}

	bool Device::Destroy(SamplerHandle handle, const DestroyDesc & desc) noexcept
	{
		const std::scoped_lock guard(m_blocks->Guard(ResourceType::eSampler));
		return m_blocks->Device().core->destroy(m_impl,
			ResourceType::eSampler,
			{
				.index		= handle.index,
				.generation = handle.generation,
			},
			desc,
			nullptr);
	}

	bool Device::Destroy(SamplerHandle handle, const DestroyDesc & desc, Error & error) noexcept
	{
		error = {};
		const std::scoped_lock guard(m_blocks->Guard(ResourceType::eSampler));
		return m_blocks->Device().core->destroy(m_impl,
			ResourceType::eSampler,
			{
				.index		= handle.index,
				.generation = handle.generation,
			},
			desc,
			&error);
	}

	bool Device::Destroy(HeapHandle handle, const DestroyDesc & desc) noexcept
	{
		const std::scoped_lock guard(m_blocks->Guard(ResourceType::eHeap));
		return m_blocks->Device().core->destroy(m_impl,
			ResourceType::eHeap,
			{
				.index		= handle.index,
				.generation = handle.generation,
			},
			desc,
			nullptr);
	}

	bool Device::Destroy(HeapHandle handle, const DestroyDesc & desc, Error & error) noexcept
	{
		error = {};
		const std::scoped_lock guard(m_blocks->Guard(ResourceType::eHeap));
		return m_blocks->Device().core->destroy(m_impl,
			ResourceType::eHeap,
			{
				.index		= handle.index,
				.generation = handle.generation,
			},
			desc,
			&error);
	}

	bool Device::Destroy(DescriptorSetLayoutHandle handle, const DestroyDesc & desc) noexcept
	{
		const std::scoped_lock guard(m_blocks->Guard(ResourceType::eDescriptorSetLayout));
		return m_blocks->Device().core->destroy(m_impl,
			ResourceType::eDescriptorSetLayout,
			{
				.index		= handle.index,
				.generation = handle.generation,
			},
			desc,
			nullptr);
	}

	bool Device::Destroy(DescriptorSetLayoutHandle handle, const DestroyDesc & desc, Error & error) noexcept
	{
		error = {};
		const std::scoped_lock guard(m_blocks->Guard(ResourceType::eDescriptorSetLayout));
		return m_blocks->Device().core->destroy(m_impl,
			ResourceType::eDescriptorSetLayout,
			{
				.index		= handle.index,
				.generation = handle.generation,
			},
			desc,
			&error);
	}

	bool Device::Destroy(DescriptorSetHandle handle, const DestroyDesc & desc) noexcept
	{
		const std::scoped_lock guard(m_blocks->Guard(ResourceType::eDescriptorSet));
		return m_blocks->Device().core->destroy(m_impl,
			ResourceType::eDescriptorSet,
			{
				.index		= handle.index,
				.generation = handle.generation,
			},
			desc,
			nullptr);
	}

	bool Device::Destroy(DescriptorSetHandle handle, const DestroyDesc & desc, Error & error) noexcept
	{
		error = {};
		const std::scoped_lock guard(m_blocks->Guard(ResourceType::eDescriptorSet));
		return m_blocks->Device().core->destroy(m_impl,
			ResourceType::eDescriptorSet,
			{
				.index		= handle.index,
				.generation = handle.generation,
			},
			desc,
			&error);
	}

	bool Device::Destroy(PipelineLayoutHandle handle, const DestroyDesc & desc) noexcept
	{
		const std::scoped_lock guard(m_blocks->Guard(ResourceType::ePipelineLayout));
		return m_blocks->Device().core->destroy(m_impl,
			ResourceType::ePipelineLayout,
			{
				.index		= handle.index,
				.generation = handle.generation,
			},
			desc,
			nullptr);
	}

	bool Device::Destroy(PipelineLayoutHandle handle, const DestroyDesc & desc, Error & error) noexcept
	{
		error = {};
		const std::scoped_lock guard(m_blocks->Guard(ResourceType::ePipelineLayout));
		return m_blocks->Device().core->destroy(m_impl,
			ResourceType::ePipelineLayout,
			{
				.index		= handle.index,
				.generation = handle.generation,
			},
			desc,
			&error);
	}

	bool Device::Destroy(GraphicsPipelineHandle handle, const DestroyDesc & desc) noexcept
	{
		const std::scoped_lock guard(m_blocks->Guard(ResourceType::eGraphicsPipeline));
		return m_blocks->Device().core->destroy(m_impl,
			ResourceType::eGraphicsPipeline,
			{
				.index		= handle.index,
				.generation = handle.generation,
			},
			desc,
			nullptr);
	}

	bool Device::Destroy(GraphicsPipelineHandle handle, const DestroyDesc & desc, Error & error) noexcept
	{
		error = {};
		const std::scoped_lock guard(m_blocks->Guard(ResourceType::eGraphicsPipeline));
		return m_blocks->Device().core->destroy(m_impl,
			ResourceType::eGraphicsPipeline,
			{
				.index		= handle.index,
				.generation = handle.generation,
			},
			desc,
			&error);
	}

	bool Device::Destroy(ComputePipelineHandle handle, const DestroyDesc & desc) noexcept
	{
		const std::scoped_lock guard(m_blocks->Guard(ResourceType::eComputePipeline));
		return m_blocks->Device().core->destroy(m_impl,
			ResourceType::eComputePipeline,
			{
				.index		= handle.index,
				.generation = handle.generation,
			},
			desc,
			nullptr);
	}

	bool Device::Destroy(ComputePipelineHandle handle, const DestroyDesc & desc, Error & error) noexcept
	{
		error = {};
		const std::scoped_lock guard(m_blocks->Guard(ResourceType::eComputePipeline));
		return m_blocks->Device().core->destroy(m_impl,
			ResourceType::eComputePipeline,
			{
				.index		= handle.index,
				.generation = handle.generation,
			},
			desc,
			&error);
	}

	bool Device::Destroy(RayTracingPipelineHandle handle, const DestroyDesc & desc) noexcept
	{
		const std::scoped_lock guard(m_blocks->Guard(ResourceType::eRayTracingPipeline));
		return m_blocks->Device().core->destroy(m_impl,
			ResourceType::eRayTracingPipeline,
			{
				.index		= handle.index,
				.generation = handle.generation,
			},
			desc,
			nullptr);
	}

	bool Device::Destroy(RayTracingPipelineHandle handle, const DestroyDesc & desc, Error & error) noexcept
	{
		error = {};
		const std::scoped_lock guard(m_blocks->Guard(ResourceType::eRayTracingPipeline));
		return m_blocks->Device().core->destroy(m_impl,
			ResourceType::eRayTracingPipeline,
			{
				.index		= handle.index,
				.generation = handle.generation,
			},
			desc,
			&error);
	}

	bool Device::Destroy(PipelineCacheHandle handle, const DestroyDesc & desc) noexcept
	{
		const std::scoped_lock guard(m_blocks->Guard(ResourceType::ePipelineCache));
		return m_blocks->Device().core->destroy(m_impl,
			ResourceType::ePipelineCache,
			{
				.index		= handle.index,
				.generation = handle.generation,
			},
			desc,
			nullptr);
	}

	bool Device::Destroy(PipelineCacheHandle handle, const DestroyDesc & desc, Error & error) noexcept
	{
		error = {};
		const std::scoped_lock guard(m_blocks->Guard(ResourceType::ePipelineCache));
		return m_blocks->Device().core->destroy(m_impl,
			ResourceType::ePipelineCache,
			{
				.index		= handle.index,
				.generation = handle.generation,
			},
			desc,
			&error);
	}

	bool Device::Destroy(AccelerationStructureHandle handle, const DestroyDesc & desc) noexcept
	{
		const std::scoped_lock guard(m_blocks->Guard(ResourceType::eAccelerationStructure));
		return m_blocks->Device().core->destroy(m_impl,
			ResourceType::eAccelerationStructure,
			{
				.index		= handle.index,
				.generation = handle.generation,
			},
			desc,
			nullptr);
	}

	bool Device::Destroy(AccelerationStructureHandle handle, const DestroyDesc & desc, Error & error) noexcept
	{
		error = {};
		const std::scoped_lock guard(m_blocks->Guard(ResourceType::eAccelerationStructure));
		return m_blocks->Device().core->destroy(m_impl,
			ResourceType::eAccelerationStructure,
			{
				.index		= handle.index,
				.generation = handle.generation,
			},
			desc,
			&error);
	}

	bool Device::Destroy(QueryPoolHandle handle, const DestroyDesc & desc) noexcept
	{
		const std::scoped_lock guard(m_blocks->Guard(ResourceType::eQueryPool));
		return m_blocks->Device().core->destroy(m_impl,
			ResourceType::eQueryPool,
			{
				.index		= handle.index,
				.generation = handle.generation,
			},
			desc,
			nullptr);
	}

	bool Device::Destroy(QueryPoolHandle handle, const DestroyDesc & desc, Error & error) noexcept
	{
		error = {};
		const std::scoped_lock guard(m_blocks->Guard(ResourceType::eQueryPool));
		return m_blocks->Device().core->destroy(m_impl,
			ResourceType::eQueryPool,
			{
				.index		= handle.index,
				.generation = handle.generation,
			},
			desc,
			&error);
	}

	bool Device::Destroy(TimelineHandle handle, const DestroyDesc & desc) noexcept
	{
		const std::scoped_lock guard(m_blocks->Guard(ResourceType::eTimeline));
		return m_blocks->Device().core->destroy(m_impl,
			ResourceType::eTimeline,
			{
				.index		= handle.index,
				.generation = handle.generation,
			},
			desc,
			nullptr);
	}

	bool Device::Destroy(TimelineHandle handle, const DestroyDesc & desc, Error & error) noexcept
	{
		error = {};
		const std::scoped_lock guard(m_blocks->Guard(ResourceType::eTimeline));
		return m_blocks->Device().core->destroy(m_impl,
			ResourceType::eTimeline,
			{
				.index		= handle.index,
				.generation = handle.generation,
			},
			desc,
			&error);
	}

	bool Device::Destroy(BinarySemaphoreHandle handle, const DestroyDesc & desc) noexcept
	{
		const std::scoped_lock guard(m_blocks->Guard(ResourceType::eBinarySemaphore));
		return m_blocks->Device().core->destroy(m_impl,
			ResourceType::eBinarySemaphore,
			{
				.index		= handle.index,
				.generation = handle.generation,
			},
			desc,
			nullptr);
	}

	bool Device::Destroy(BinarySemaphoreHandle handle, const DestroyDesc & desc, Error & error) noexcept
	{
		error = {};
		const std::scoped_lock guard(m_blocks->Guard(ResourceType::eBinarySemaphore));
		return m_blocks->Device().core->destroy(m_impl,
			ResourceType::eBinarySemaphore,
			{
				.index		= handle.index,
				.generation = handle.generation,
			},
			desc,
			&error);
	}

	// A collect is what retires the native resource so it is also what releases the heap range underneath it.

	bool Device::CollectGarbage() noexcept
	{
		const CoreDeviceApi * core = m_blocks->Device().core;

		detail::HostVector<MemorySpan> released;
		const bool collected = CollectEveryKind(*m_blocks,
			nullptr,
			[&](const ResourceType type, Error * kindError) noexcept
			{
				// Under this kind's guard, which is the one the destroy that queued these held.
				m_blocks->Tracker().TakeAll(type, released);
				return core->collectGarbage(m_impl, type, kindError);
			});

		FreeSpans(*this, m_blocks->Allocator(), released);
		return collected;
	}

	bool Device::CollectGarbage(Error & error) noexcept
	{
		error					   = {};
		const CoreDeviceApi * core = m_blocks->Device().core;

		detail::HostVector<MemorySpan> released;
		const bool collected = CollectEveryKind(*m_blocks,
			&error,
			[&](const ResourceType type, Error * kindError) noexcept
			{
				// Under this kind's guard, which is the one the destroy that queued these held.
				m_blocks->Tracker().TakeAll(type, released);
				return core->collectGarbage(m_impl, type, kindError);
			});

		FreeSpans(*this, m_blocks->Allocator(), released);
		return collected;
	}

	bool Device::CollectGarbage(TimelineHandle timeline, std::uint64_t completedValue) noexcept
	{
		const CoreDeviceApi * core = m_blocks->Device().core;

		detail::HostVector<MemorySpan> released;
		const bool collected = CollectEveryKind(*m_blocks,
			nullptr,
			[&](const ResourceType type, Error * kindError) noexcept
			{
				// Under this kind's guard, which is the one the destroy that queued these held.
				m_blocks->Tracker().TakeReleasable(type, timeline, completedValue, released);
				return core->collectGarbageTimeline(m_impl, type, timeline, completedValue, kindError);
			});

		FreeSpans(*this, m_blocks->Allocator(), released);
		return collected;
	}

	bool Device::CollectGarbage(TimelineHandle timeline, std::uint64_t completedValue, Error & error) noexcept
	{
		error					   = {};
		const CoreDeviceApi * core = m_blocks->Device().core;

		detail::HostVector<MemorySpan> released;
		const bool collected = CollectEveryKind(*m_blocks,
			&error,
			[&](const ResourceType type, Error * kindError) noexcept
			{
				// Under this kind's guard, which is the one the destroy that queued these held.
				m_blocks->Tracker().TakeReleasable(type, timeline, completedValue, released);
				return core->collectGarbageTimeline(m_impl, type, timeline, completedValue, kindError);
			});

		FreeSpans(*this, m_blocks->Allocator(), released);
		return collected;
	}

	// Adoption of native objects made on this same device.

	BufferHandle Device::AdoptBufferRaw(GraphicsApiId api, const void * nativeImport, const AdoptedBufferDesc & desc, Error * error) noexcept
	{
		return m_blocks->Device().adoption != nullptr ? m_blocks->Device().adoption->adoptBuffer(m_impl, api, nativeImport, desc, error)
													  : Decline<BufferHandle>(error, kNoAdoption);
	}

	TextureHandle Device::AdoptTextureRaw(GraphicsApiId api, const void * nativeImport, const AdoptedTextureDesc & desc, Error * error) noexcept
	{
		return m_blocks->Device().adoption != nullptr ? m_blocks->Device().adoption->adoptTexture(m_impl, api, nativeImport, desc, error)
													  : Decline<TextureHandle>(error, kNoAdoption);
	}

	bool Device::GetNativeBufferRaw(GraphicsApiId api, BufferHandle buffer, void * outNativeImport, Error * error) noexcept
	{
		return m_blocks->Device().adoption != nullptr ? m_blocks->Device().adoption->getNativeBuffer(m_impl, api, buffer, outNativeImport, error)
													  : Decline<bool>(error, kNoAdoption);
	}

	bool Device::GetNativeTextureRaw(GraphicsApiId api, TextureHandle texture, void * outNativeImport, Error * error) noexcept
	{
		return m_blocks->Device().adoption != nullptr ? m_blocks->Device().adoption->getNativeTexture(m_impl, api, texture, outNativeImport, error)
													  : Decline<bool>(error, kNoAdoption);
	}

	// External sharing.

	bool Device::ExportBuffer(BufferHandle buffer, ExternalHandleType type, ExternalHandle & out) noexcept
	{
		Error error{};
		return ExportBuffer(buffer, type, out, error);
	}

	bool Device::ExportBuffer(BufferHandle buffer, ExternalHandleType type, ExternalHandle & out, Error & error) noexcept
	{
		out	  = {};
		error = {};
		if (m_blocks->Device().externalSharing == nullptr)
		{
			return Decline<bool>(&error, kNoExternalSharing);
		}

		const std::scoped_lock guard(m_blocks->Guard(ResourceType::eBuffer));
		bool exported = m_blocks->Device().externalSharing->exportBuffer(m_impl, buffer, type, &out, &error);
		Settle(exported, error);

		// A refusal leaves nothing behind, so a caller that ignored the return closes an empty handle, not whatever the backend got to.
		if (!exported)
		{
			out = {};
		}

		return exported;
	}

	Result<ExternalHandle> Device::ExportBufferWithResult(BufferHandle buffer, ExternalHandleType type) noexcept
	{
		ExternalHandle out{};
		Error error{};
		static_cast<void>(ExportBuffer(buffer, type, out, error));
		return AsResult(out, error);
	}

	bool Device::ExportHeap(HeapHandle heap, ExternalHandleType type, ExternalHandle & out) noexcept
	{
		Error error{};
		return ExportHeap(heap, type, out, error);
	}

	bool Device::ExportHeap(HeapHandle heap, ExternalHandleType type, ExternalHandle & out, Error & error) noexcept
	{
		out	  = {};
		error = {};
		if (m_blocks->Device().externalSharing == nullptr)
		{
			return Decline<bool>(&error, kNoExternalSharing);
		}

		const std::scoped_lock guard(m_blocks->Guard(ResourceType::eHeap));
		bool exported = m_blocks->Device().externalSharing->exportHeap(m_impl, heap, type, &out, &error);
		Settle(exported, error);

		// A refusal leaves nothing behind, so a caller that ignored the return closes an empty handle, not whatever the backend got to.
		if (!exported)
		{
			out = {};
		}

		return exported;
	}

	Result<ExternalHandle> Device::ExportHeapWithResult(HeapHandle heap, ExternalHandleType type) noexcept
	{
		ExternalHandle out{};
		Error error{};
		static_cast<void>(ExportHeap(heap, type, out, error));
		return AsResult(out, error);
	}

	bool Device::ExportTexture(TextureHandle texture, ExternalHandleType type, ExternalHandle & out) noexcept
	{
		Error error{};
		return ExportTexture(texture, type, out, error);
	}

	bool Device::ExportTexture(TextureHandle texture, ExternalHandleType type, ExternalHandle & out, Error & error) noexcept
	{
		out	  = {};
		error = {};
		if (m_blocks->Device().externalSharing == nullptr)
		{
			return Decline<bool>(&error, kNoExternalSharing);
		}

		const std::scoped_lock guard(m_blocks->Guard(ResourceType::eTexture));
		bool exported = m_blocks->Device().externalSharing->exportTexture(m_impl, texture, type, &out, &error);
		Settle(exported, error);

		// A refusal leaves nothing behind, so a caller that ignored the return closes an empty handle, not whatever the backend got to.
		if (!exported)
		{
			out = {};
		}

		return exported;
	}

	Result<ExternalHandle> Device::ExportTextureWithResult(TextureHandle texture, ExternalHandleType type) noexcept
	{
		ExternalHandle out{};
		Error error{};
		static_cast<void>(ExportTexture(texture, type, out, error));
		return AsResult(out, error);
	}

	bool Device::ExportTimeline(TimelineHandle timeline, ExternalHandleType type, ExternalHandle & out) noexcept
	{
		Error error{};
		return ExportTimeline(timeline, type, out, error);
	}

	bool Device::ExportTimeline(TimelineHandle timeline, ExternalHandleType type, ExternalHandle & out, Error & error) noexcept
	{
		out	  = {};
		error = {};
		if (m_blocks->Device().externalSharing == nullptr)
		{
			return Decline<bool>(&error, kNoExternalSharing);
		}

		const std::scoped_lock guard(m_blocks->Guard(ResourceType::eTimeline));
		bool exported = m_blocks->Device().externalSharing->exportTimeline(m_impl, timeline, type, &out, &error);
		Settle(exported, error);

		// A refusal leaves nothing behind, so a caller that ignored the return closes an empty handle, not whatever the backend got to.
		if (!exported)
		{
			out = {};
		}

		return exported;
	}

	Result<ExternalHandle> Device::ExportTimelineWithResult(TimelineHandle timeline, ExternalHandleType type) noexcept
	{
		ExternalHandle out{};
		Error error{};
		static_cast<void>(ExportTimeline(timeline, type, out, error));
		return AsResult(out, error);
	}

	bool Device::ExportBinarySemaphore(BinarySemaphoreHandle semaphore, ExternalHandleType type, ExternalHandle & out) noexcept
	{
		Error error{};
		return ExportBinarySemaphore(semaphore, type, out, error);
	}

	bool Device::ExportBinarySemaphore(BinarySemaphoreHandle semaphore, ExternalHandleType type, ExternalHandle & out, Error & error) noexcept
	{
		out	  = {};
		error = {};
		if (m_blocks->Device().externalSharing == nullptr)
		{
			return Decline<bool>(&error, kNoExternalSharing);
		}

		const std::scoped_lock guard(m_blocks->Guard(ResourceType::eBinarySemaphore));
		bool exported = m_blocks->Device().externalSharing->exportBinarySemaphore(m_impl, semaphore, type, &out, &error);
		Settle(exported, error);

		// A refusal leaves nothing behind, so a caller that ignored the return closes an empty handle, not whatever the backend got to.
		if (!exported)
		{
			out = {};
		}

		return exported;
	}

	Result<ExternalHandle> Device::ExportBinarySemaphoreWithResult(BinarySemaphoreHandle semaphore, ExternalHandleType type) noexcept
	{
		ExternalHandle out{};
		Error error{};
		static_cast<void>(ExportBinarySemaphore(semaphore, type, out, error));
		return AsResult(out, error);
	}

	BufferHandle Device::ImportBuffer(const ExternalBufferImportDesc & desc) noexcept
	{
		Error error{};
		return ImportBuffer(desc, error);
	}

	BufferHandle Device::ImportBuffer(const ExternalBufferImportDesc & desc, Error & error) noexcept
	{
		error = {};
		if (m_blocks->Device().externalSharing == nullptr)
		{
			return Decline<BufferHandle>(&error, kNoExternalSharing);
		}

		const std::scoped_lock guard(m_blocks->Guard(ResourceType::eBuffer));
		BufferHandle produced = m_blocks->Device().externalSharing->importBuffer(m_impl, desc, &error);
		Settle(produced, error);
		return produced;
	}

	Result<BufferHandle> Device::ImportBufferWithResult(const ExternalBufferImportDesc & desc) noexcept
	{
		Error error{};
		return AsResult(ImportBuffer(desc, error), error);
	}

	HeapHandle Device::ImportHeap(const ExternalHeapImportDesc & desc) noexcept
	{
		Error error{};
		return ImportHeap(desc, error);
	}

	HeapHandle Device::ImportHeap(const ExternalHeapImportDesc & desc, Error & error) noexcept
	{
		error = {};
		if (m_blocks->Device().externalSharing == nullptr)
		{
			return Decline<HeapHandle>(&error, kNoExternalSharing);
		}

		const std::scoped_lock guard(m_blocks->Guard(ResourceType::eHeap));
		HeapHandle produced = m_blocks->Device().externalSharing->importHeap(m_impl, desc, &error);
		Settle(produced, error);
		return produced;
	}

	Result<HeapHandle> Device::ImportHeapWithResult(const ExternalHeapImportDesc & desc) noexcept
	{
		Error error{};
		return AsResult(ImportHeap(desc, error), error);
	}

	TextureHandle Device::ImportTexture(const ExternalTextureImportDesc & desc) noexcept
	{
		Error error{};
		return ImportTexture(desc, error);
	}

	TextureHandle Device::ImportTexture(const ExternalTextureImportDesc & desc, Error & error) noexcept
	{
		error = {};
		if (m_blocks->Device().externalSharing == nullptr)
		{
			return Decline<TextureHandle>(&error, kNoExternalSharing);
		}

		const std::scoped_lock guard(m_blocks->Guard(ResourceType::eTexture));
		TextureHandle produced = m_blocks->Device().externalSharing->importTexture(m_impl, desc, &error);
		Settle(produced, error);
		return produced;
	}

	Result<TextureHandle> Device::ImportTextureWithResult(const ExternalTextureImportDesc & desc) noexcept
	{
		Error error{};
		return AsResult(ImportTexture(desc, error), error);
	}

	TimelineHandle Device::ImportTimeline(const ExternalTimelineImportDesc & desc) noexcept
	{
		Error error{};
		return ImportTimeline(desc, error);
	}

	TimelineHandle Device::ImportTimeline(const ExternalTimelineImportDesc & desc, Error & error) noexcept
	{
		error = {};
		if (m_blocks->Device().externalSharing == nullptr)
		{
			return Decline<TimelineHandle>(&error, kNoExternalSharing);
		}

		const std::scoped_lock guard(m_blocks->Guard(ResourceType::eTimeline));
		TimelineHandle produced = m_blocks->Device().externalSharing->importTimeline(m_impl, desc, &error);
		Settle(produced, error);
		return produced;
	}

	Result<TimelineHandle> Device::ImportTimelineWithResult(const ExternalTimelineImportDesc & desc) noexcept
	{
		Error error{};
		return AsResult(ImportTimeline(desc, error), error);
	}

	BinarySemaphoreHandle Device::ImportBinarySemaphore(const ExternalBinarySemaphoreImportDesc & desc) noexcept
	{
		Error error{};
		return ImportBinarySemaphore(desc, error);
	}

	BinarySemaphoreHandle Device::ImportBinarySemaphore(const ExternalBinarySemaphoreImportDesc & desc, Error & error) noexcept
	{
		error = {};
		if (m_blocks->Device().externalSharing == nullptr)
		{
			return Decline<BinarySemaphoreHandle>(&error, kNoExternalSharing);
		}

		const std::scoped_lock guard(m_blocks->Guard(ResourceType::eBinarySemaphore));
		BinarySemaphoreHandle produced = m_blocks->Device().externalSharing->importBinarySemaphore(m_impl, desc, &error);
		Settle(produced, error);
		return produced;
	}

	Result<BinarySemaphoreHandle> Device::ImportBinarySemaphoreWithResult(const ExternalBinarySemaphoreImportDesc & desc) noexcept
	{
		Error error{};
		return AsResult(ImportBinarySemaphore(desc, error), error);
	}

	bool Device::CloseExportedHandle(const ExternalHandle & handle) noexcept
	{
		Error error{};
		return CloseExportedHandle(handle, error);
	}

	bool Device::CloseExportedHandle(const ExternalHandle & handle, Error & error) noexcept
	{
		error = {};
		if (m_blocks->Device().externalSharing == nullptr)
		{
			return Decline<bool>(&error, kNoExternalSharing);
		}

		// No guard: a handle names no slot in this device's registries, so nothing here races a create or a destroy.
		bool closed = m_blocks->Device().externalSharing->closeExportedHandle(m_impl, handle, &error);
		Settle(closed, error);
		return closed;
	}

	TextureViewHandle Device::AdoptTextureViewRaw(GraphicsApiId api, const void * nativeImport, const AdoptedTextureViewDesc & desc, Error * error) noexcept
	{
		return m_blocks->Device().adoption != nullptr ? m_blocks->Device().adoption->adoptTextureView(m_impl, api, nativeImport, desc, error)
													  : Decline<TextureViewHandle>(error, kNoAdoption);
	}

	SamplerHandle Device::AdoptSamplerRaw(GraphicsApiId api, const void * nativeImport, const AdoptedSamplerDesc & desc, Error * error) noexcept
	{
		return m_blocks->Device().adoption != nullptr ? m_blocks->Device().adoption->adoptSampler(m_impl, api, nativeImport, desc, error)
													  : Decline<SamplerHandle>(error, kNoAdoption);
	}

	bool Device::GetNativeTextureViewRaw(GraphicsApiId api, TextureViewHandle view, void * outNativeImport, Error * error) noexcept
	{
		return m_blocks->Device().adoption != nullptr ? m_blocks->Device().adoption->getNativeTextureView(m_impl, api, view, outNativeImport, error)
													  : Decline<bool>(error, kNoAdoption);
	}

	bool Device::GetNativeSamplerRaw(GraphicsApiId api, SamplerHandle sampler, void * outNativeImport, Error * error) noexcept
	{
		return m_blocks->Device().adoption != nullptr ? m_blocks->Device().adoption->getNativeSampler(m_impl, api, sampler, outNativeImport, error)
													  : Decline<bool>(error, kNoAdoption);
	}

	TimelineHandle Device::AdoptTimelineRaw(GraphicsApiId api, const void * nativeImport, const AdoptedTimelineDesc & desc, Error * error) noexcept
	{
		return m_blocks->Device().adoption != nullptr ? m_blocks->Device().adoption->adoptTimeline(m_impl, api, nativeImport, desc, error)
													  : Decline<TimelineHandle>(error, kNoAdoption);
	}

	BinarySemaphoreHandle Device::AdoptBinarySemaphoreRaw(
		GraphicsApiId api, const void * nativeImport, const AdoptedBinarySemaphoreDesc & desc, Error * error) noexcept
	{
		return m_blocks->Device().adoption != nullptr ? m_blocks->Device().adoption->adoptBinarySemaphore(m_impl, api, nativeImport, desc, error)
													  : Decline<BinarySemaphoreHandle>(error, kNoAdoption);
	}

	bool Device::GetNativeTimelineRaw(GraphicsApiId api, TimelineHandle timeline, void * outNativeImport, Error * error) noexcept
	{
		return m_blocks->Device().adoption != nullptr ? m_blocks->Device().adoption->getNativeTimeline(m_impl, api, timeline, outNativeImport, error)
													  : Decline<bool>(error, kNoAdoption);
	}

	bool Device::GetNativeBinarySemaphoreRaw(GraphicsApiId api, BinarySemaphoreHandle semaphore, void * outNativeImport, Error * error) noexcept
	{
		return m_blocks->Device().adoption != nullptr ? m_blocks->Device().adoption->getNativeBinarySemaphore(m_impl, api, semaphore, outNativeImport, error)
													  : Decline<bool>(error, kNoAdoption);
	}

	// Queue.

	QueueType Queue::GetType() const noexcept
	{
		return m_blocks->core->getType(m_impl);
	}

	std::uint32_t Queue::GetFamilyIndex() const noexcept
	{
		return m_blocks->core->getFamilyIndex(m_impl);
	}

	bool Queue::Submit(const SubmitDesc & desc) noexcept
	{
		return m_blocks->core->submit(m_impl, desc, nullptr);
	}

	bool Queue::Submit(const SubmitDesc & desc, Error & error) noexcept
	{
		error = {};
		return m_blocks->core->submit(m_impl, desc, &error);
	}

	bool Queue::WaitIdle() noexcept
	{
		return m_blocks->core->waitIdle(m_impl, nullptr);
	}

	bool Queue::WaitIdle(Error & error) noexcept
	{
		error = {};
		return m_blocks->core->waitIdle(m_impl, &error);
	}

	bool Queue::Wait(TimelineHandle timeline, std::uint64_t value, std::uint64_t timeoutNanoseconds) noexcept
	{
		return m_blocks->core->wait(m_impl, timeline, value, timeoutNanoseconds, nullptr);
	}

	bool Queue::Wait(TimelineHandle timeline, std::uint64_t value, std::uint64_t timeoutNanoseconds, Error & error) noexcept
	{
		error = {};
		return m_blocks->core->wait(m_impl, timeline, value, timeoutNanoseconds, &error);
	}

	bool Queue::Signal(TimelineHandle timeline, std::uint64_t value) noexcept
	{
		return m_blocks->core->signal(m_impl, timeline, value, nullptr);
	}

	bool Queue::Signal(TimelineHandle timeline, std::uint64_t value, Error & error) noexcept
	{
		error = {};
		return m_blocks->core->signal(m_impl, timeline, value, &error);
	}

	bool Queue::BindSparse(const SparseBindDesc & desc) noexcept
	{
		return m_blocks->sparse != nullptr ? m_blocks->sparse->bindSparse(m_impl, desc, nullptr) : Decline<bool>(nullptr, kNoSparse);
	}

	bool Queue::BindSparse(const SparseBindDesc & desc, Error & error) noexcept
	{
		error = {};
		return m_blocks->sparse != nullptr ? m_blocks->sparse->bindSparse(m_impl, desc, &error) : Decline<bool>(&error, kNoSparse);
	}

	bool Queue::BeginDebugLabel(const char * name, std::uint32_t color) noexcept
	{
		return m_blocks->core->beginDebugLabel(m_impl, name, color, nullptr);
	}

	bool Queue::BeginDebugLabel(const char * name, std::uint32_t color, Error & error) noexcept
	{
		error = {};
		return m_blocks->core->beginDebugLabel(m_impl, name, color, &error);
	}

	bool Queue::EndDebugLabel() noexcept
	{
		return m_blocks->core->endDebugLabel(m_impl, nullptr);
	}

	bool Queue::EndDebugLabel(Error & error) noexcept
	{
		error = {};
		return m_blocks->core->endDebugLabel(m_impl, &error);
	}

	bool Queue::GetCompletedValue(TimelineHandle timeline, std::uint64_t & out) const noexcept
	{
		Error error{};
		return GetCompletedValue(timeline, out, error);
	}

	bool Queue::GetCompletedValue(TimelineHandle timeline, std::uint64_t & out, Error & error) const noexcept
	{
		out	  = 0;
		error = {};

		bool answered = m_blocks->core->getCompletedValue(m_impl, timeline, &out, &error);
		Settle(answered, error);
		if (!answered)
		{
			out = {};
		}

		return answered;
	}

	Result<std::uint64_t> Queue::GetCompletedValueWithResult(TimelineHandle timeline) const noexcept
	{
		std::uint64_t out = 0;
		Error error{};
		static_cast<void>(GetCompletedValue(timeline, out, error));
		return AsResult(out, error);
	}

	// Command pool.

	CommandList CommandPool::Allocate(const char * debugName) noexcept
	{
		Error error{};
		return Allocate(debugName, error);
	}

	CommandList CommandPool::Allocate(const char * debugName, Error & error) noexcept
	{
		error = {};
		const std::scoped_lock guard(m_blocks->ObjectGuard());
		void * impl						 = m_dispatch->allocate(m_impl, debugName, &error);
		const CommandListBlocks * blocks = detail::CheckedChild<RenderCommandApi>(impl, &error) ? m_blocks->CommandList(impl) : nullptr;
		Settle(blocks, error);

		return blocks != nullptr ? detail::FacadeBuilder::MakeCommandList(impl, blocks) : CommandList{};
	}

	Result<CommandList> CommandPool::AllocateWithResult(const char * debugName) noexcept
	{
		Error error{};
		return AsResult(Allocate(debugName, error), error);
	}

	bool CommandPool::Reset(RetirePoint safeAfter) noexcept
	{
		return m_dispatch->reset(m_impl, safeAfter, nullptr);
	}

	bool CommandPool::Reset(RetirePoint safeAfter, Error & error) noexcept
	{
		error = {};
		return m_dispatch->reset(m_impl, safeAfter, &error);
	}

	// Command list recording.

	bool CommandList::Begin() noexcept
	{
		return m_blocks->render->begin(m_impl, nullptr);
	}

	bool CommandList::Begin(Error & error) noexcept
	{
		error = {};
		return m_blocks->render->begin(m_impl, &error);
	}

	bool CommandList::End() noexcept
	{
		return m_blocks->render->end(m_impl, nullptr);
	}

	bool CommandList::End(Error & error) noexcept
	{
		error = {};
		return m_blocks->render->end(m_impl, &error);
	}

	bool CommandList::Barriers(const BarrierBatch & barriers) noexcept
	{
		return m_blocks->render->barriers(m_impl, barriers, nullptr);
	}

	bool CommandList::Barriers(const BarrierBatch & barriers, Error & error) noexcept
	{
		error = {};
		return m_blocks->render->barriers(m_impl, barriers, &error);
	}

	bool CommandList::AliasBarriers(std::span<const AliasBarrier> barriers) noexcept
	{
		return m_blocks->aliasing != nullptr ? m_blocks->aliasing->aliasBarriers(m_impl, barriers, nullptr) : Decline<bool>(nullptr, kNoAliasing);
	}

	bool CommandList::AliasBarriers(std::span<const AliasBarrier> barriers, Error & error) noexcept
	{
		error = {};
		return m_blocks->aliasing != nullptr ? m_blocks->aliasing->aliasBarriers(m_impl, barriers, &error) : Decline<bool>(&error, kNoAliasing);
	}

	bool CommandList::BeginRendering(const BeginRenderingDesc & desc) noexcept
	{
		return m_blocks->render->beginRendering(m_impl, desc, nullptr);
	}

	bool CommandList::BeginRendering(const BeginRenderingDesc & desc, Error & error) noexcept
	{
		error = {};
		return m_blocks->render->beginRendering(m_impl, desc, &error);
	}

	bool CommandList::EndRendering() noexcept
	{
		return m_blocks->render->endRendering(m_impl, nullptr);
	}

	bool CommandList::EndRendering(Error & error) noexcept
	{
		error = {};
		return m_blocks->render->endRendering(m_impl, &error);
	}

	bool CommandList::SetGraphicsPipeline(GraphicsPipelineHandle pipeline) noexcept
	{
		return m_blocks->render->setGraphicsPipeline(m_impl, pipeline, nullptr);
	}

	bool CommandList::SetGraphicsPipeline(GraphicsPipelineHandle pipeline, Error & error) noexcept
	{
		error = {};
		return m_blocks->render->setGraphicsPipeline(m_impl, pipeline, &error);
	}

	bool CommandList::SetComputePipeline(ComputePipelineHandle pipeline) noexcept
	{
		return m_blocks->render->setComputePipeline(m_impl, pipeline, nullptr);
	}

	bool CommandList::SetComputePipeline(ComputePipelineHandle pipeline, Error & error) noexcept
	{
		error = {};
		return m_blocks->render->setComputePipeline(m_impl, pipeline, &error);
	}

	bool CommandList::SetRayTracingPipeline(RayTracingPipelineHandle pipeline) noexcept
	{
		return m_blocks->rayTracing != nullptr ? m_blocks->rayTracing->setRayTracingPipeline(m_impl, pipeline, nullptr)
											   : Decline<bool>(nullptr, kNoRayTracingCommand);
	}

	bool CommandList::SetRayTracingPipeline(RayTracingPipelineHandle pipeline, Error & error) noexcept
	{
		error = {};
		return m_blocks->rayTracing != nullptr ? m_blocks->rayTracing->setRayTracingPipeline(m_impl, pipeline, &error)
											   : Decline<bool>(&error, kNoRayTracingCommand);
	}

	bool CommandList::BindDescriptorSet(
		PipelineLayoutHandle layout, std::uint32_t setIndex, DescriptorSetHandle set, std::span<const DynamicDescriptorOffset> dynamicOffsets) noexcept
	{
		return m_blocks->render->bindDescriptorSet(m_impl, layout, setIndex, set, dynamicOffsets, nullptr);
	}

	bool CommandList::BindDescriptorSet(PipelineLayoutHandle layout, std::uint32_t setIndex, DescriptorSetHandle set,
		std::span<const DynamicDescriptorOffset> dynamicOffsets, Error & error) noexcept
	{
		error = {};
		return m_blocks->render->bindDescriptorSet(m_impl, layout, setIndex, set, dynamicOffsets, &error);
	}

	bool CommandList::PushConstants(
		PipelineLayoutHandle layout, Flags<ShaderStage> stages, std::uint32_t offset, std::uint32_t size, const void * data) noexcept
	{
		return m_blocks->render->pushConstants(m_impl, layout, stages, offset, size, data, nullptr);
	}

	bool CommandList::PushConstants(
		PipelineLayoutHandle layout, Flags<ShaderStage> stages, std::uint32_t offset, std::uint32_t size, const void * data, Error & error) noexcept
	{
		error = {};
		return m_blocks->render->pushConstants(m_impl, layout, stages, offset, size, data, &error);
	}

	bool CommandList::SetViewport(const Viewport & viewport) noexcept
	{
		return m_blocks->render->setViewport(m_impl, viewport, nullptr);
	}

	bool CommandList::SetViewport(const Viewport & viewport, Error & error) noexcept
	{
		error = {};
		return m_blocks->render->setViewport(m_impl, viewport, &error);
	}

	bool CommandList::SetScissor(const Rect2D & scissor) noexcept
	{
		return m_blocks->render->setScissor(m_impl, scissor, nullptr);
	}

	bool CommandList::SetScissor(const Rect2D & scissor, Error & error) noexcept
	{
		error = {};
		return m_blocks->render->setScissor(m_impl, scissor, &error);
	}

	bool CommandList::SetBlendConstants(float r, float g, float b, float a) noexcept
	{
		return m_blocks->render->setBlendConstants(m_impl, r, g, b, a, nullptr);
	}

	bool CommandList::SetBlendConstants(float r, float g, float b, float a, Error & error) noexcept
	{
		error = {};
		return m_blocks->render->setBlendConstants(m_impl, r, g, b, a, &error);
	}

	bool CommandList::SetStencilReference(std::uint32_t reference) noexcept
	{
		return m_blocks->render->setStencilReference(m_impl, reference, nullptr);
	}

	bool CommandList::SetStencilReference(std::uint32_t reference, Error & error) noexcept
	{
		error = {};
		return m_blocks->render->setStencilReference(m_impl, reference, &error);
	}

	bool CommandList::SetDepthBias(float constantFactor, float clamp, float slopeFactor) noexcept
	{
		return m_blocks->render->setDepthBias(m_impl, constantFactor, clamp, slopeFactor, nullptr);
	}

	bool CommandList::SetDepthBias(float constantFactor, float clamp, float slopeFactor, Error & error) noexcept
	{
		error = {};
		return m_blocks->render->setDepthBias(m_impl, constantFactor, clamp, slopeFactor, &error);
	}

	bool CommandList::SetVertexBuffer(std::uint32_t slot, BufferHandle buffer, std::uint64_t offset) noexcept
	{
		return m_blocks->render->setVertexBuffer(m_impl, slot, buffer, offset, nullptr);
	}

	bool CommandList::SetVertexBuffer(std::uint32_t slot, BufferHandle buffer, std::uint64_t offset, Error & error) noexcept
	{
		error = {};
		return m_blocks->render->setVertexBuffer(m_impl, slot, buffer, offset, &error);
	}

	bool CommandList::SetIndexBuffer(BufferHandle buffer, std::uint64_t offset, bool index32) noexcept
	{
		return m_blocks->render->setIndexBuffer(m_impl, buffer, offset, index32, nullptr);
	}

	bool CommandList::SetIndexBuffer(BufferHandle buffer, std::uint64_t offset, bool index32, Error & error) noexcept
	{
		error = {};
		return m_blocks->render->setIndexBuffer(m_impl, buffer, offset, index32, &error);
	}

	bool CommandList::Draw(std::uint32_t vertexCount, std::uint32_t instanceCount, std::uint32_t firstVertex, std::uint32_t firstInstance) noexcept
	{
		return m_blocks->render->draw(m_impl, vertexCount, instanceCount, firstVertex, firstInstance, nullptr);
	}

	bool CommandList::Draw(
		std::uint32_t vertexCount, std::uint32_t instanceCount, std::uint32_t firstVertex, std::uint32_t firstInstance, Error & error) noexcept
	{
		error = {};
		return m_blocks->render->draw(m_impl, vertexCount, instanceCount, firstVertex, firstInstance, &error);
	}

	bool CommandList::DrawIndexed(
		std::uint32_t indexCount, std::uint32_t instanceCount, std::uint32_t firstIndex, std::int32_t vertexOffset, std::uint32_t firstInstance) noexcept
	{
		return m_blocks->render->drawIndexed(m_impl, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance, nullptr);
	}

	bool CommandList::DrawIndexed(std::uint32_t indexCount, std::uint32_t instanceCount, std::uint32_t firstIndex, std::int32_t vertexOffset,
		std::uint32_t firstInstance, Error & error) noexcept
	{
		error = {};
		return m_blocks->render->drawIndexed(m_impl, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance, &error);
	}

	bool CommandList::DrawIndirect(BufferHandle args, std::uint64_t offset, std::uint32_t drawCount, std::uint32_t stride) noexcept
	{
		return m_blocks->indirect != nullptr ? m_blocks->indirect->drawIndirect(m_impl, args, offset, drawCount, stride, nullptr)
											 : Decline<bool>(nullptr, kNoIndirect);
	}

	bool CommandList::DrawIndirect(BufferHandle args, std::uint64_t offset, std::uint32_t drawCount, std::uint32_t stride, Error & error) noexcept
	{
		error = {};
		return m_blocks->indirect != nullptr ? m_blocks->indirect->drawIndirect(m_impl, args, offset, drawCount, stride, &error)
											 : Decline<bool>(&error, kNoIndirect);
	}

	bool CommandList::DrawIndexedIndirect(BufferHandle args, std::uint64_t offset, std::uint32_t drawCount, std::uint32_t stride) noexcept
	{
		return m_blocks->indirect != nullptr ? m_blocks->indirect->drawIndexedIndirect(m_impl, args, offset, drawCount, stride, nullptr)
											 : Decline<bool>(nullptr, kNoIndirect);
	}

	bool CommandList::DrawIndexedIndirect(BufferHandle args, std::uint64_t offset, std::uint32_t drawCount, std::uint32_t stride, Error & error) noexcept
	{
		error = {};
		return m_blocks->indirect != nullptr ? m_blocks->indirect->drawIndexedIndirect(m_impl, args, offset, drawCount, stride, &error)
											 : Decline<bool>(&error, kNoIndirect);
	}

	bool CommandList::DrawIndirectCount(
		BufferHandle args, std::uint64_t argsOffset, BufferHandle count, std::uint64_t countOffset, std::uint32_t maxDrawCount, std::uint32_t stride) noexcept
	{
		return m_blocks->indirectCount != nullptr
				   ? m_blocks->indirectCount->drawIndirectCount(m_impl, args, argsOffset, count, countOffset, maxDrawCount, stride, nullptr)
				   : Decline<bool>(nullptr, kNoIndirectCount);
	}

	bool CommandList::DrawIndirectCount(BufferHandle args, std::uint64_t argsOffset, BufferHandle count, std::uint64_t countOffset, std::uint32_t maxDrawCount,
		std::uint32_t stride, Error & error) noexcept
	{
		error = {};
		return m_blocks->indirectCount != nullptr
				   ? m_blocks->indirectCount->drawIndirectCount(m_impl, args, argsOffset, count, countOffset, maxDrawCount, stride, &error)
				   : Decline<bool>(&error, kNoIndirectCount);
	}

	bool CommandList::DrawIndexedIndirectCount(
		BufferHandle args, std::uint64_t argsOffset, BufferHandle count, std::uint64_t countOffset, std::uint32_t maxDrawCount, std::uint32_t stride) noexcept
	{
		return m_blocks->indirectCount != nullptr
				   ? m_blocks->indirectCount->drawIndexedIndirectCount(m_impl, args, argsOffset, count, countOffset, maxDrawCount, stride, nullptr)
				   : Decline<bool>(nullptr, kNoIndirectCount);
	}

	bool CommandList::DrawIndexedIndirectCount(BufferHandle args, std::uint64_t argsOffset, BufferHandle count, std::uint64_t countOffset,
		std::uint32_t maxDrawCount, std::uint32_t stride, Error & error) noexcept
	{
		error = {};
		return m_blocks->indirectCount != nullptr
				   ? m_blocks->indirectCount->drawIndexedIndirectCount(m_impl, args, argsOffset, count, countOffset, maxDrawCount, stride, &error)
				   : Decline<bool>(&error, kNoIndirectCount);
	}

	bool CommandList::Dispatch(std::uint32_t groupCountX, std::uint32_t groupCountY, std::uint32_t groupCountZ) noexcept
	{
		return m_blocks->render->dispatch(m_impl, groupCountX, groupCountY, groupCountZ, nullptr);
	}

	bool CommandList::Dispatch(std::uint32_t groupCountX, std::uint32_t groupCountY, std::uint32_t groupCountZ, Error & error) noexcept
	{
		error = {};
		return m_blocks->render->dispatch(m_impl, groupCountX, groupCountY, groupCountZ, &error);
	}

	bool CommandList::DispatchIndirect(BufferHandle args, std::uint64_t offset) noexcept
	{
		return m_blocks->indirect != nullptr ? m_blocks->indirect->dispatchIndirect(m_impl, args, offset, nullptr) : Decline<bool>(nullptr, kNoIndirect);
	}

	bool CommandList::DispatchIndirect(BufferHandle args, std::uint64_t offset, Error & error) noexcept
	{
		error = {};
		return m_blocks->indirect != nullptr ? m_blocks->indirect->dispatchIndirect(m_impl, args, offset, &error) : Decline<bool>(&error, kNoIndirect);
	}

	bool CommandList::BuildAccelerationStructures(std::span<const AccelerationStructureBuildDesc> builds) noexcept
	{
		return m_blocks->rayTracing != nullptr ? m_blocks->rayTracing->buildAccelerationStructures(m_impl, builds, nullptr)
											   : Decline<bool>(nullptr, kNoRayTracingCommand);
	}

	bool CommandList::BuildAccelerationStructures(std::span<const AccelerationStructureBuildDesc> builds, Error & error) noexcept
	{
		error = {};
		return m_blocks->rayTracing != nullptr ? m_blocks->rayTracing->buildAccelerationStructures(m_impl, builds, &error)
											   : Decline<bool>(&error, kNoRayTracingCommand);
	}

	bool CommandList::CopyAccelerationStructure(AccelerationStructureHandle dst, AccelerationStructureHandle src) noexcept
	{
		return m_blocks->rayTracing != nullptr ? m_blocks->rayTracing->copyAccelerationStructure(m_impl, dst, src, nullptr)
											   : Decline<bool>(nullptr, kNoRayTracingCommand);
	}

	bool CommandList::CopyAccelerationStructure(AccelerationStructureHandle dst, AccelerationStructureHandle src, Error & error) noexcept
	{
		error = {};
		return m_blocks->rayTracing != nullptr ? m_blocks->rayTracing->copyAccelerationStructure(m_impl, dst, src, &error)
											   : Decline<bool>(&error, kNoRayTracingCommand);
	}

	bool CommandList::CompactAccelerationStructure(AccelerationStructureHandle dst, AccelerationStructureHandle src) noexcept
	{
		return m_blocks->rayTracing != nullptr ? m_blocks->rayTracing->compactAccelerationStructure(m_impl, dst, src, nullptr)
											   : Decline<bool>(nullptr, kNoRayTracingCommand);
	}

	bool CommandList::CompactAccelerationStructure(AccelerationStructureHandle dst, AccelerationStructureHandle src, Error & error) noexcept
	{
		error = {};
		return m_blocks->rayTracing != nullptr ? m_blocks->rayTracing->compactAccelerationStructure(m_impl, dst, src, &error)
											   : Decline<bool>(&error, kNoRayTracingCommand);
	}

	bool CommandList::TraceRays(const ShaderBindingTableDesc & sbt, std::uint32_t width, std::uint32_t height, std::uint32_t depth) noexcept
	{
		return m_blocks->rayTracing != nullptr ? m_blocks->rayTracing->traceRays(m_impl, sbt, width, height, depth, nullptr)
											   : Decline<bool>(nullptr, kNoRayTracingCommand);
	}

	bool CommandList::TraceRays(const ShaderBindingTableDesc & sbt, std::uint32_t width, std::uint32_t height, std::uint32_t depth, Error & error) noexcept
	{
		error = {};
		return m_blocks->rayTracing != nullptr ? m_blocks->rayTracing->traceRays(m_impl, sbt, width, height, depth, &error)
											   : Decline<bool>(&error, kNoRayTracingCommand);
	}

	bool CommandList::CopyBuffer(BufferHandle dst, std::uint64_t dstOffset, BufferHandle src, std::uint64_t srcOffset, std::uint64_t size) noexcept
	{
		return m_blocks->render->copyBuffer(m_impl, dst, dstOffset, src, srcOffset, size, nullptr);
	}

	bool CommandList::CopyBuffer(
		BufferHandle dst, std::uint64_t dstOffset, BufferHandle src, std::uint64_t srcOffset, std::uint64_t size, Error & error) noexcept
	{
		error = {};
		return m_blocks->render->copyBuffer(m_impl, dst, dstOffset, src, srcOffset, size, &error);
	}

	bool CommandList::CopyBufferToTexture(TextureHandle dst, BufferHandle src, std::span<const BufferTextureCopy> regions) noexcept
	{
		return m_blocks->render->copyBufferToTexture(m_impl, dst, src, regions, nullptr);
	}

	bool CommandList::CopyBufferToTexture(TextureHandle dst, BufferHandle src, std::span<const BufferTextureCopy> regions, Error & error) noexcept
	{
		error = {};
		return m_blocks->render->copyBufferToTexture(m_impl, dst, src, regions, &error);
	}

	bool CommandList::CopyTextureToBuffer(BufferHandle dst, TextureHandle src, std::span<const BufferTextureCopy> regions) noexcept
	{
		return m_blocks->render->copyTextureToBuffer(m_impl, dst, src, regions, nullptr);
	}

	bool CommandList::CopyTextureToBuffer(BufferHandle dst, TextureHandle src, std::span<const BufferTextureCopy> regions, Error & error) noexcept
	{
		error = {};
		return m_blocks->render->copyTextureToBuffer(m_impl, dst, src, regions, &error);
	}

	bool CommandList::CopyTexture(TextureHandle dst, TextureHandle src, std::span<const TextureCopy> regions) noexcept
	{
		return m_blocks->render->copyTexture(m_impl, dst, src, regions, nullptr);
	}

	bool CommandList::CopyTexture(TextureHandle dst, TextureHandle src, std::span<const TextureCopy> regions, Error & error) noexcept
	{
		error = {};
		return m_blocks->render->copyTexture(m_impl, dst, src, regions, &error);
	}

	bool CommandList::ClearBuffer(BufferHandle buffer, std::uint64_t offset, std::uint64_t size, std::uint32_t value) noexcept
	{
		return m_blocks->render->clearBuffer(m_impl, buffer, offset, size, value, nullptr);
	}

	bool CommandList::ClearBuffer(BufferHandle buffer, std::uint64_t offset, std::uint64_t size, std::uint32_t value, Error & error) noexcept
	{
		error = {};
		return m_blocks->render->clearBuffer(m_impl, buffer, offset, size, value, &error);
	}

	bool CommandList::ClearTexture(TextureHandle texture, const ClearColor & color, std::span<const TextureSubresourceRange> ranges) noexcept
	{
		return m_blocks->render->clearTexture(m_impl, texture, color, ranges, nullptr);
	}

	bool CommandList::ClearTexture(TextureHandle texture, const ClearColor & color, std::span<const TextureSubresourceRange> ranges, Error & error) noexcept
	{
		error = {};
		return m_blocks->render->clearTexture(m_impl, texture, color, ranges, &error);
	}

	bool CommandList::ResolveTexture(TextureHandle dst, TextureHandle src, std::span<const TextureResolve> regions) noexcept
	{
		return m_blocks->render->resolveTexture(m_impl, dst, src, regions, nullptr);
	}

	bool CommandList::ResolveTexture(TextureHandle dst, TextureHandle src, std::span<const TextureResolve> regions, Error & error) noexcept
	{
		error = {};
		return m_blocks->render->resolveTexture(m_impl, dst, src, regions, &error);
	}

	bool CommandList::Blit(TextureHandle dst, TextureHandle src, std::span<const TextureBlit> regions, Filter filter) noexcept
	{
		return m_blocks->render->blit(m_impl, dst, src, regions, filter, nullptr);
	}

	bool CommandList::Blit(TextureHandle dst, TextureHandle src, std::span<const TextureBlit> regions, Filter filter, Error & error) noexcept
	{
		error = {};
		return m_blocks->render->blit(m_impl, dst, src, regions, filter, &error);
	}

	bool CommandList::GenerateMips(TextureHandle texture) noexcept
	{
		return m_blocks->render->generateMips(m_impl, texture, nullptr);
	}

	bool CommandList::GenerateMips(TextureHandle texture, Error & error) noexcept
	{
		error = {};
		return m_blocks->render->generateMips(m_impl, texture, &error);
	}

	bool CommandList::ResetQueryPool(QueryPoolHandle pool, std::uint32_t firstQuery, std::uint32_t queryCount) noexcept
	{
		return m_blocks->query != nullptr ? m_blocks->query->resetQueryPool(m_impl, pool, firstQuery, queryCount, nullptr)
										  : Decline<bool>(nullptr, kNoQueryCommand);
	}

	bool CommandList::ResetQueryPool(QueryPoolHandle pool, std::uint32_t firstQuery, std::uint32_t queryCount, Error & error) noexcept
	{
		error = {};
		return m_blocks->query != nullptr ? m_blocks->query->resetQueryPool(m_impl, pool, firstQuery, queryCount, &error)
										  : Decline<bool>(&error, kNoQueryCommand);
	}

	bool CommandList::WriteTimestamp(QueryPoolHandle pool, std::uint32_t query, Flags<PipelineStage> stage) noexcept
	{
		return m_blocks->query != nullptr ? m_blocks->query->writeTimestamp(m_impl, pool, query, stage, nullptr) : Decline<bool>(nullptr, kNoQueryCommand);
	}

	bool CommandList::WriteTimestamp(QueryPoolHandle pool, std::uint32_t query, Flags<PipelineStage> stage, Error & error) noexcept
	{
		error = {};
		return m_blocks->query != nullptr ? m_blocks->query->writeTimestamp(m_impl, pool, query, stage, &error) : Decline<bool>(&error, kNoQueryCommand);
	}

	bool CommandList::BeginQuery(QueryPoolHandle pool, std::uint32_t query) noexcept
	{
		return m_blocks->query != nullptr ? m_blocks->query->beginQuery(m_impl, pool, query, nullptr) : Decline<bool>(nullptr, kNoQueryCommand);
	}

	bool CommandList::BeginQuery(QueryPoolHandle pool, std::uint32_t query, Error & error) noexcept
	{
		error = {};
		return m_blocks->query != nullptr ? m_blocks->query->beginQuery(m_impl, pool, query, &error) : Decline<bool>(&error, kNoQueryCommand);
	}

	bool CommandList::EndQuery(QueryPoolHandle pool, std::uint32_t query) noexcept
	{
		return m_blocks->query != nullptr ? m_blocks->query->endQuery(m_impl, pool, query, nullptr) : Decline<bool>(nullptr, kNoQueryCommand);
	}

	bool CommandList::EndQuery(QueryPoolHandle pool, std::uint32_t query, Error & error) noexcept
	{
		error = {};
		return m_blocks->query != nullptr ? m_blocks->query->endQuery(m_impl, pool, query, &error) : Decline<bool>(&error, kNoQueryCommand);
	}

	bool CommandList::ResolveQueryData(
		QueryPoolHandle pool, std::uint32_t firstQuery, std::uint32_t queryCount, BufferHandle dst, std::uint64_t dstOffset) noexcept
	{
		return m_blocks->query != nullptr ? m_blocks->query->resolveQueryData(m_impl, pool, firstQuery, queryCount, dst, dstOffset, nullptr)
										  : Decline<bool>(nullptr, kNoQueryCommand);
	}

	bool CommandList::ResolveQueryData(
		QueryPoolHandle pool, std::uint32_t firstQuery, std::uint32_t queryCount, BufferHandle dst, std::uint64_t dstOffset, Error & error) noexcept
	{
		error = {};
		return m_blocks->query != nullptr ? m_blocks->query->resolveQueryData(m_impl, pool, firstQuery, queryCount, dst, dstOffset, &error)
										  : Decline<bool>(&error, kNoQueryCommand);
	}

	bool CommandList::BeginDebugLabel(const char * name, std::uint32_t color) noexcept
	{
		return m_blocks->render->beginDebugLabel(m_impl, name, color, nullptr);
	}

	bool CommandList::BeginDebugLabel(const char * name, std::uint32_t color, Error & error) noexcept
	{
		error = {};
		return m_blocks->render->beginDebugLabel(m_impl, name, color, &error);
	}

	bool CommandList::EndDebugLabel() noexcept
	{
		return m_blocks->render->endDebugLabel(m_impl, nullptr);
	}

	bool CommandList::EndDebugLabel(Error & error) noexcept
	{
		error = {};
		return m_blocks->render->endDebugLabel(m_impl, &error);
	}

	bool CommandList::BeginNativeMutation(GraphicsApiId api, const NativeMutationDesc & desc, Error * error) noexcept
	{
		return m_blocks->nativeEscape != nullptr ? m_blocks->nativeEscape->beginNativeMutation(m_impl, api, desc, error)
												 : Decline<bool>(error, kNoNativeEscape);
	}

	bool CommandList::EndNativeMutation(const NativeMutationDesc & desc, Error * error) noexcept
	{
		return m_blocks->nativeEscape != nullptr ? m_blocks->nativeEscape->endNativeMutation(m_impl, desc, error) : Decline<bool>(error, kNoNativeEscape);
	}

	// Swapchain.

	AcquireResult Swapchain::AcquireNextImage(std::uint64_t timeoutNanoseconds) noexcept
	{
		Error error{};
		return AcquireNextImage(timeoutNanoseconds, error);
	}

	AcquireResult Swapchain::AcquireNextImage(std::uint64_t timeoutNanoseconds, Error & error) noexcept
	{
		error				   = {};
		AcquireResult produced = m_dispatch->acquireNextImage(m_impl, timeoutNanoseconds, &error);
		Settle(produced, error);

		// A backend answers with a status, an index and the acquire semaphore. Resolving what that index names happens here and not in each backend, since the three
		// accessors it goes through are the same ones a caller would have had to call itself.
		if (produced.status == SwapchainStatus::eOk || produced.status == SwapchainStatus::eSuboptimal)
		{
			produced.texture		= m_dispatch->getBackBuffer(m_impl, produced.imageIndex);
			produced.view			= m_dispatch->getBackBufferView(m_impl, produced.imageIndex);
			produced.renderFinished = m_dispatch->getPerImagePresentSemaphore(m_impl, produced.imageIndex);
		}

		return produced;
	}

	Result<AcquireResult> Swapchain::AcquireNextImageWithResult(std::uint64_t timeoutNanoseconds) noexcept
	{
		Error error{};
		return AsResult(AcquireNextImage(timeoutNanoseconds, error), error);
	}

	PresentResult Swapchain::Present(Queue & queue, std::uint32_t imageIndex, BinarySemaphoreHandle renderFinished) noexcept
	{
		Error error{};
		return Present(queue, imageIndex, renderFinished, error);
	}

	PresentResult Swapchain::Present(Queue & queue, std::uint32_t imageIndex, BinarySemaphoreHandle renderFinished, Error & error) noexcept
	{
		error				   = {};
		PresentResult produced = m_dispatch->present(m_impl, imageIndex, renderFinished, detail::FacadeBuilder::ImplOf(queue), &error);
		Settle(produced, error);
		return produced;
	}

	Result<PresentResult> Swapchain::PresentWithResult(Queue & queue, std::uint32_t imageIndex, BinarySemaphoreHandle renderFinished) noexcept
	{
		Error error{};
		return AsResult(Present(queue, imageIndex, renderFinished, error), error);
	}

	TextureHandle Swapchain::GetBackBuffer(std::uint32_t imageIndex) const noexcept
	{
		return m_dispatch->getBackBuffer(m_impl, imageIndex);
	}

	TextureViewHandle Swapchain::GetBackBufferView(std::uint32_t imageIndex) const noexcept
	{
		return m_dispatch->getBackBufferView(m_impl, imageIndex);
	}

	BinarySemaphoreHandle Swapchain::GetPerImagePresentSemaphore(std::uint32_t imageIndex) const noexcept
	{
		return m_dispatch->getPerImagePresentSemaphore(m_impl, imageIndex);
	}

	Format Swapchain::GetFormat() const noexcept
	{
		return m_dispatch->getFormat(m_impl);
	}

	bool Swapchain::SupportsReadback() const noexcept
	{
		return m_dispatch->supportsReadback(m_impl);
	}

	std::uint32_t Swapchain::GetImageCount() const noexcept
	{
		return m_dispatch->getImageCount(m_impl);
	}

	std::uint32_t Swapchain::GetWidth() const noexcept
	{
		return m_dispatch->getWidth(m_impl);
	}

	std::uint32_t Swapchain::GetHeight() const noexcept
	{
		return m_dispatch->getHeight(m_impl);
	}

	bool Swapchain::Resize(std::uint32_t width, std::uint32_t height) noexcept
	{
		return m_dispatch->resize(m_impl, width, height, nullptr);
	}

	bool Swapchain::Resize(std::uint32_t width, std::uint32_t height, Error & error) noexcept
	{
		error = {};
		return m_dispatch->resize(m_impl, width, height, &error);
	}

	bool Swapchain::SetPresentMode(PresentMode mode) noexcept
	{
		return m_dispatch->setPresentMode(m_impl, mode, nullptr);
	}

	PresentMode Swapchain::GetPresentMode() const noexcept
	{
		return m_dispatch->getPresentMode(m_impl);
	}

	// Descriptor arena.

	DescriptorSetHandle DescriptorArena::Allocate(const DescriptorSetAllocDesc & desc) noexcept
	{
		Error error{};
		return Allocate(desc, error);
	}

	DescriptorSetHandle DescriptorArena::Allocate(const DescriptorSetAllocDesc & desc, Error & error) noexcept
	{
		error = {};

		/*
		 * The same guard a destroy of a set takes, because this reaches the same table. Every backend stores the set it made in the device's descriptor set table so
		 * two arenas allocating on two threads are two writers to one structure and D24's promise that a backend may assume its entries are serialized for that kind
		 * covers this one like every other.
		 */
		const std::scoped_lock guard(m_blocks->Guard(ResourceType::eDescriptorSet));
		DescriptorSetHandle produced = m_dispatch->allocate(m_impl, desc, &error);
		Settle(produced, error);
		return produced;
	}

	Result<DescriptorSetHandle> DescriptorArena::AllocateWithResult(const DescriptorSetAllocDesc & desc) noexcept
	{
		Error error{};
		return AsResult(Allocate(desc, error), error);
	}

	bool DescriptorArena::Reset(RetirePoint safeAfter) noexcept
	{
		// Retiring the sets this arena handed out walks the same table allocating them filled so it takes the same guard. Still one acquisition for the whole
		// reclaim, which is the property D27 chose a bulk reset for.
		const std::scoped_lock guard(m_blocks->Guard(ResourceType::eDescriptorSet));
		return m_dispatch->reset(m_impl, safeAfter, nullptr);
	}

	bool DescriptorArena::Reset(RetirePoint safeAfter, Error & error) noexcept
	{
		error = {};
		const std::scoped_lock guard(m_blocks->Guard(ResourceType::eDescriptorSet));
		return m_dispatch->reset(m_impl, safeAfter, &error);
	}

	// UniqueDevice is the sole owner of backend device teardown.

	void UniqueDevice::Reset() noexcept
	{
		if (m_impl != nullptr && m_blocks != nullptr)
		{
			{
				const detail::LifetimeLock lifetime;
				m_blocks->Device().core->destroyDevice(m_impl);
			}

			// Dropped, not freed. The backend released its heaps wholesale as it tore down so handing each span back would be returning ranges of memory that no longer
			// exists.
			m_blocks->Tracker().Forget();

			// The resolved blocks go with the device that published them. Every facade derived from it pointed here and none of those outlive the owner.
			detail::ReleaseDeviceBlocks(m_blocks);
		}

		m_impl	 = nullptr;
		m_blocks = nullptr;
	}

	// Dynamic creation.

	namespace
	{

		// Brings up an instance on the first preferred API the registry has an entry point for. Null with error set otherwise. Shared by both dynamic creates, since
		// only one of the two hands the result to a UniqueInstance.
		[[nodiscard]] void * CreateBackendInstance(
			GraphicsApiRegistry & registry, std::span<const GraphicsApiId> preferredApis, const InstanceDesc & desc, Error & error)
		{
			for (GraphicsApiId id : preferredApis)
			{
				const BackendCreateInfo * backend = detail::RegistryAccess::Find(registry, id);
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

		/*
		 * The instance's block when it published a whole one. Fills error and releases the instance otherwise.
		 *
		 * An instance nothing can drive still has to go back and destroyInstance is the only entry that can send it. A backend that published no block at all or left
		 * even that one entry unset, leaves nothing to call so the object is abandoned, not reached through a null pointer.
		 */
		[[nodiscard]] const InstanceApi * BlockOrRelease(void * instanceImpl, Error & error)
		{
			const InstanceApi * block = detail::CheckedBlock<InstanceApi>(instanceImpl, &error);
			if (block != nullptr)
			{
				return block;
			}

			if (const InstanceApi * partial = detail::QueryBlock<InstanceApi>(instanceImpl); partial != nullptr && partial->destroyInstance != nullptr)
			{
				const detail::LifetimeLock lifetime;
				partial->destroyInstance(instanceImpl);
			}

			return nullptr;
		}

	} // namespace

	Result<UniqueInstance> CreateInstance(GraphicsApiRegistry & registry, std::span<const GraphicsApiId> preferredApis, const InstanceDesc & desc)
	{
		Error error{};
		void * instanceImpl = CreateBackendInstance(registry, preferredApis, desc, error);
		if (instanceImpl == nullptr)
		{
			return error;
		}

		const InstanceApi * dispatch = BlockOrRelease(instanceImpl, error);
		if (dispatch == nullptr)
		{
			return error;
		}

		return detail::FacadeBuilder::MakeUniqueInstance(instanceImpl, dispatch);
	}

	Result<UniqueDevice> CreateDevice(GraphicsApiRegistry & registry, std::span<const GraphicsApiId> preferredApis, const DeviceDesc & desc)
	{
		if (const Result<void> threading = detail::CheckThreading(desc); !threading)
		{
			return threading.GetError();
		}

		const InstanceDesc instanceDesc = InstanceDescForDevice(desc);

		Error error{};
		void * instanceImpl = CreateBackendInstance(registry, preferredApis, instanceDesc, error);
		if (instanceImpl == nullptr)
		{
			return error;
		}

		// Checked before createDevice is reached through it, since that entry is one of the ones that could be missing.
		const InstanceApi * dispatch = BlockOrRelease(instanceImpl, error);
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
			// On success the instance stays with the backend, which holds it for the devices created from it. A device that never came up leaves nobody to do that so it
			// is released here, not left for the static teardown at process exit.
			const detail::LifetimeLock lifetime;
			dispatch->destroyInstance(instanceImpl);
			return error.code == ErrorCode::eOk ? Error{ .code = ErrorCode::eUnknown, .message = "backend returned a null device", } : error;
		}

		BackendBlockSet * blocks = detail::ResolveDeviceBlocks(deviceImpl, desc, &error);
		if (blocks == nullptr)
		{
			// The device came up but cannot be driven. Unwind it the same way the null-device path above does so a rejected backend does not strand a device and an
			// instance for the life of the process.
			detail::ReleaseUndrivableDevice(deviceImpl);

			const detail::LifetimeLock lifetime;
			dispatch->destroyInstance(instanceImpl);
			return error;
		}

		return detail::FacadeBuilder::MakeUniqueDevice(deviceImpl, blocks);
	}

} // namespace azo::rhi

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

#include "azoth/rhi/backend/dispatch.hpp"
#include "azoth/rhi/backend/resource_tables.hpp"
#include "azoth/rhi/backend/support/host_containers.hpp"
#include "azoth/rhi/backend/support/spin_lock.hpp"
#include "azoth/rhi/core/api.hpp"
#include "azoth/rhi/validation/registry.hpp"

#include <atomic>
#include <cstdint>
#include <span>
#include <thread>
#include <type_traits>

/*
 * The validation decorator is a backend object that stands in front of another one. A wrapper publishes blocks through queryInterface the way any backend
 * object does so the facades resolve it with the same code that resolves a real backend. That is what applies the checks to a backend from outside this
 * repository.
 *
 * Installed only when ValidationMode is not eOff. Off, nothing is allocated and the facades hold the backend's own block pointers.
 */
namespace azo::rhi::validation
{

	class DeviceValidator;

	/*
	 * What every wrapper has: the object it publishes, the object it stands in front of and the device's validator.
	 *
	 * The BackendObject pointer is first because that is the impl-header convention every backend object follows and it is how the RHI finds the blocks behind any
	 * impl pointer.
	 */
	struct WrappedDevice;

	struct WrappedObject
	{
		const BackendObject * object = nullptr;
		void * inner				 = nullptr;

		// The device this belongs to and its validator. Both point at the device wrapper, which points at itself so a check reaches the registry the same way from
		// any object without walking back up through the one that made it.
		WrappedDevice * device		= nullptr;
		DeviceValidator * validator = nullptr;
	};

	/*
	 * The registry for one device and the reporting the checks fail through.
	 *
	 * Owned by the device wrapper, not shared with the backend. A backend that kept its own would be a backend that could disagree and the point is that every
	 * backend is checked by the same code.
	 */
	class DeviceValidator final
	{
	public:
		explicit DeviceValidator(const ValidationMode mode) noexcept : m_mode(mode) {}

		// Holds the registry by value and the wrappers in front of a device point at it so it stays where the device built it.
		DeviceValidator(const DeviceValidator &)			 = delete;
		DeviceValidator & operator=(const DeviceValidator &) = delete;
		DeviceValidator(DeviceValidator &&)					 = delete;
		DeviceValidator & operator=(DeviceValidator &&)		 = delete;
		~DeviceValidator()									 = default;

		[[nodiscard]] HandleRegistry & Handles() noexcept
		{
			return m_handles;
		}

		[[nodiscard]] ValidationMode Mode() const noexcept
		{
			return m_mode;
		}

		// Set once, while the device is coming up and before anything can reach it. Here and not in the constructor because the wrapper holding this is built into
		// storage the host allocator handed back, which default constructs it first.
		void SetMode(const ValidationMode mode) noexcept
		{
			m_mode = mode;
		}

		/*
		 * Whether the deeper checks run: resource state, queue ownership and descriptor compatibility.
		 *
		 * Handle liveness runs in every mode that is not off, since it is a load and two compares and it is what turns a use after free into a refusal and not a
		 * crash somewhere else.
		 */
		[[nodiscard]] bool ChecksState() const noexcept
		{
			return m_mode == ValidationMode::eDeveloper || m_mode == ValidationMode::eCapture;
		}

		/*
		 * Reports a broken rule through the Error the entry was given and counts it.
		 *
		 * Returns false so a check can be the whole of an entry's early return.
		 */
		bool Fail(Error * error, const char * message) noexcept
		{
			m_failures.fetch_add(1, std::memory_order_relaxed);
			if (error != nullptr)
			{
				*error = Error{
					.code	 = ErrorCode::eValidationFailed,
					.message = message,
				};
			}

			return false;
		}

		/*
		 * The same report for an entry that answers with a value and not a bool.
		 */
		template <class T>
		[[nodiscard]] T FailValue(Error * error, const char * message) noexcept
		{
			static_cast<void>(Fail(error, message));
			return T{};
		}

		/*
		 * How many rules this device has seen broken. What a test reads instead of parsing messages.
		 */
		[[nodiscard]] std::uint64_t Failures() const noexcept
		{
			return m_failures.load(std::memory_order_relaxed);
		}

	private:
		HandleRegistry m_handles;
		std::atomic<std::uint64_t> m_failures{ 0 };
		ValidationMode m_mode = ValidationMode::eOff;
	};

	/*
	 * A wrapped device, which owns the validator and every child wrapper it handed out.
	 *
	 * Children are freed with the device because that is when the backend frees the objects behind them. A command pool, an arena, a swapchain and the lists a
	 * pool allocates all live until the device goes so a wrapper that outlived its inner object or died before it would be the only new lifetime here and there is
	 * none.
	 */
	struct WrappedDevice final : WrappedObject
	{
		DeviceBlocks blocks{};

		// The one validator for this device. The base's pointer names this and every child wrapper's names it too.
		DeviceValidator ownedValidator{ ValidationMode::eOff };

		// A child and the entry that frees it, since they are held type erased and each kind has its own size.
		struct Child final
		{
			WrappedObject * wrapper					  = nullptr;
			void (*release)(WrappedObject *) noexcept = nullptr;
		};

		// Guarded because a create can arrive on any thread and it is the only guard this layer takes. Nothing on a read path touches it.
		SpinLock childLock;
		detail::HostVector<Child> children;

		// Names the next arena this device vends. Starts at one, since zero is what a kind with no pool carries in its record.
		std::atomic<std::uint64_t> nextArenaId{ 1 };
	};

	struct WrappedQueue final : WrappedObject
	{
		QueueBlocks blocks{};

		// Asked once, when the queue is handed out so a submit can compare it against what the lists were recorded for.
		QueueType type = QueueType::eGraphics;
	};

	struct WrappedCommandList;

	struct WrappedCommandPool final : WrappedObject
	{
		const CommandPoolApi * blocks = nullptr;

		// The type every list this pool allocates is recorded for, which is the pool's own and not something a list can be asked.
		QueueType queueType = QueueType::eGraphics;

		/*
		 * The wrapper standing in front of each list this pool has handed out, found by the inner list it wraps.
		 *
		 * A pool takes its lists back at Reset and hands the same ones out again, so an allocate that wrapped the returned pointer every time would leave one
		 * inner list behind a new wrapper each frame. Bounded by the pool's high water mark. Unguarded, since a command pool is host-thread owned.
		 */
		detail::HostMap<void *, WrappedCommandList *> lists;
	};

	/*
	 * A wrapped command list, which is also where recording scope is tracked.
	 *
	 * Scope is per list and not in the registry because it is not a property of a resource. A list belongs to one thread for its whole lifetime, which is what
	 * lets these be plain members, not atomics.
	 */
	struct WrappedCommandList final : WrappedObject
	{
		/*
		 * Whether this recording holds itself to one thread, settled at Begin. False until Begin claims the list so a command arriving before one is refused for what
		 * it actually did wrong and not for a thread nothing has recorded yet. Declared ahead of the blocks on purpose: an entry that reads this reads the block
		 * pointer it calls through next so the two belong in one cache line.
		 */
		bool checksThread = false;

		/*
		 * The thread that called Begin and the one every command until End has to arrive on.
		 *
		 * A list belongs to one thread for its whole lifetime so a command arriving on another means either two threads are recording it or a recording job yielded
		 * and its fiber came back on a different worker. Neither is legal and neither shows up as anything but corruption later.
		 */
		std::thread::id recordingThread;

		CommandListBlocks blocks{};

		bool recording	   = false;
		bool rendering	   = false;
		bool graphicsBound = false;
		bool computeBound  = false;

		// The pool's type, carried down so a submit can refuse a list recorded for a queue it does not belong to.
		QueueType queueType = QueueType::eGraphics;

		/*
		 * What each resource this recording transitioned was last left in, keyed by kind, index and generation. Per recording, not per resource, since a barrier's
		 * before-state describes where this list left the resource and not where the device did. A pool reuses its lists so this is cleared at Begin. Otherwise the
		 * second frame's first barrier would disagree with the first frame's last. It needs no lock because a list belongs to one thread.
		 */
		detail::HostMap<std::uint64_t, std::uint32_t> recordedStates;
	};

	struct WrappedDescriptorArena final : WrappedObject
	{
		const DescriptorArenaApi * blocks = nullptr;

		// What the sets this arena hands out are stamped with so a reset can find them all. Never zero and never reused within a device.
		std::uint64_t id = 0;
	};

	struct WrappedSwapchain final : WrappedObject
	{
		const SwapchainApi * blocks = nullptr;
	};

	/*
	 * The inner block an entry of this type forwards through, found from the wrapper it was called on.
	 *
	 * Specialized per block, not deduced because the block type is what says which wrapper the impl pointer is and only an entry reached through that wrapper can
	 * name the block.
	 */
	template <class Block>
	[[nodiscard]] const Block * InnerBlock(WrappedObject * self) noexcept;

	/*
	 * A pass-through entry, generated from the block's own declaration.
	 *
	 * Every entry with nothing to check is one of these so no signature in this layer is retyped and an entry that gains an argument does not need a thunk
	 * rewritten to match.
	 */
	template <auto Member>
	struct Forward;

	// Whether an entry's return type is a handle, which is what decides whether passing it straight through is a shape or a defect.
	template <class T>
	inline constexpr bool kIsHandle = false;

	template <class Tag>
	inline constexpr bool kIsHandle<Handle<Tag>> = true;

	template <class Block, class R, class... Args, R (*Block::*Member)(void *, Args...) noexcept>
	struct Forward<Member>
	{
		/*
		 * A handle handed straight out is one the validation registry never heard of. The first use is refused as stale and the failure names the caller, not
		 * the entry that skipped registration. Every swapchain back buffer reached a facade this way and nothing failed until something tried to barrier one. The
		 * return type is part of the block declaration so this is decidable here and not by a reviewer noticing. Vending is the shape that registers.
		 */
		static_assert(!kIsHandle<R>,
			"an entry returning a Handle must use Vending, which records it with the validation registry. "
			"Forward passes the handle straight through, so nothing has heard of it and the first use is "
			"refused as stale.");

		static R Call(void * impl, Args... args) noexcept
		{
			auto * self = static_cast<WrappedObject *>(impl);
			return (InnerBlock<Block>(self)->*Member)(self->inner, args...);
		}
	};

	/*
	 * Picks the Error out of an entry's arguments so a generated check can report through it.
	 *
	 * Found by type and not by position. Every fallible entry takes one and takes it last, but saying so here would make the rule a thing to keep in step and not
	 * a thing that follows from the signature.
	 */
	[[nodiscard]] inline Error * PickError(Error * found, Error * candidate) noexcept
	{
		return candidate != nullptr ? candidate : found;
	}

	template <class T>
	[[nodiscard]] Error * PickError(Error * found, const T &) noexcept
	{
		return found;
	}

	/*
	 * Whether one argument is a live handle. Anything that is not a handle is nothing to check.
	 */
	template <class T>
	[[nodiscard]] bool ArgumentIsUsable(DeviceValidator &, const T &) noexcept
	{
		return true;
	}

	/*
	 * An invalid handle is passed, not refused, since the backend reports that mistake in its own words. Everything else has to be one this device handed out and
	 * still holds. A handle the registry never heard of is refused along with a stale one, because from here a foreign device's handle and a slot that was never
	 * claimed look the same. Every handle a device vends is therefore written down where it is vended.
	 */
	template <class Tag>
	requires requires { detail::ResourceTypeOf<Handle<Tag>>::kValue; }
	[[nodiscard]] bool ArgumentIsUsable(DeviceValidator & validator, const Handle<Tag> handle) noexcept
	{
		if (!handle.IsValid())
		{
			return true;
		}

		return validator.Handles().Lookup(RegisteredHandle{
				   .type	   = detail::ResourceTypeOf<Handle<Tag>>::kValue,
				   .index	   = handle.index,
				   .generation = handle.generation,
			   }) != nullptr;
	}

	/*
	 * The same refusal the generated entries make, for the ones written by hand and for the overloads below.
	 *
	 * An entry that already had a reason to be written out still has to refuse a stale handle or which entries check would depend on which ones happened to need
	 * something else doing.
	 */
	template <class... Handles>
	[[nodiscard]] bool AllUsable(DeviceValidator & validator, const Handles &... handles) noexcept
	{
		bool usable = true;
		((usable = usable && ArgumentIsUsable(validator, handles)), ...);
		return usable;
	}

	/*
	 * Where the handles live in the types that carry them. Most entries name their handles outright and the sweep above finds them by type. The rest carry them
	 * inside a desc or a span, which the signature does not reveal. Each such type says where its own are. One overload per type keeps a desc that gains a handle
	 * to one place to change. These have to precede the span overload below, which dispatches to them from a dependent call.
	 */
	[[nodiscard]] inline bool ArgumentIsUsable(DeviceValidator & validator, const PlacedBufferDesc & desc) noexcept
	{
		return AllUsable(validator, desc.heap);
	}

	[[nodiscard]] inline bool ArgumentIsUsable(DeviceValidator & validator, const PlacedTextureDesc & desc) noexcept
	{
		return AllUsable(validator, desc.heap);
	}

	[[nodiscard]] inline bool ArgumentIsUsable(DeviceValidator & validator, const PipelineLayoutDesc & desc) noexcept
	{
		for (const DescriptorSetLayoutHandle layout : desc.sets)
		{
			if (!AllUsable(validator, layout))
			{
				return false;
			}
		}

		return true;
	}

	[[nodiscard]] inline bool ArgumentIsUsable(DeviceValidator & validator, const GraphicsPipelineDesc & desc) noexcept
	{
		return AllUsable(validator, desc.layout, desc.pipelineCache);
	}

	[[nodiscard]] inline bool ArgumentIsUsable(DeviceValidator & validator, const ComputePipelineDesc & desc) noexcept
	{
		return AllUsable(validator, desc.layout, desc.pipelineCache);
	}

	[[nodiscard]] inline bool ArgumentIsUsable(DeviceValidator & validator, const RayTracingPipelineDesc & desc) noexcept
	{
		return AllUsable(validator, desc.layout, desc.pipelineCache);
	}

	[[nodiscard]] inline bool ArgumentIsUsable(DeviceValidator & validator, const AccelerationStructureDesc & desc) noexcept
	{
		return AllUsable(validator, desc.storage);
	}

	[[nodiscard]] inline bool ArgumentIsUsable(DeviceValidator & validator, const DescriptorSetAllocDesc & desc) noexcept
	{
		return AllUsable(validator, desc.layout);
	}

	[[nodiscard]] inline bool ArgumentIsUsable(DeviceValidator & validator, const RetirePoint & point) noexcept
	{
		return AllUsable(validator, point.timeline);
	}

	[[nodiscard]] inline bool ArgumentIsUsable(DeviceValidator & validator, const TimelinePoint & point) noexcept
	{
		return AllUsable(validator, point.timeline);
	}

	[[nodiscard]] inline bool ArgumentIsUsable(DeviceValidator & validator, const DestroyDesc & desc) noexcept
	{
		return AllUsable(validator, desc.safeAfter);
	}

	[[nodiscard]] inline bool ArgumentIsUsable(DeviceValidator & validator, const RenderingAttachment & attachment) noexcept
	{
		return AllUsable(validator, attachment.view);
	}

	[[nodiscard]] inline bool ArgumentIsUsable(DeviceValidator & validator, const BufferBarrier & barrier) noexcept
	{
		return AllUsable(validator, barrier.buffer);
	}

	[[nodiscard]] inline bool ArgumentIsUsable(DeviceValidator & validator, const TextureBarrier & barrier) noexcept
	{
		return AllUsable(validator, barrier.texture);
	}

	[[nodiscard]] inline bool ArgumentIsUsable(DeviceValidator & validator, const AliasBarrier & barrier) noexcept
	{
		return AllUsable(validator, barrier.beforeBuffer, barrier.beforeTexture, barrier.afterBuffer, barrier.afterTexture);
	}

	[[nodiscard]] inline bool ArgumentIsUsable(DeviceValidator & validator, const NativeTouchedBuffer & touched) noexcept
	{
		return AllUsable(validator, touched.buffer);
	}

	[[nodiscard]] inline bool ArgumentIsUsable(DeviceValidator & validator, const NativeTouchedTexture & touched) noexcept
	{
		return AllUsable(validator, touched.texture);
	}

	[[nodiscard]] inline bool ArgumentIsUsable(DeviceValidator & validator, const ResidencyPriorityDesc & desc) noexcept
	{
		return AllUsable(validator, desc.buffer, desc.texture);
	}

	[[nodiscard]] inline bool ArgumentIsUsable(DeviceValidator & validator, const SparseBufferBind & bind) noexcept
	{
		return AllUsable(validator, bind.buffer, bind.page.heap);
	}

	[[nodiscard]] inline bool ArgumentIsUsable(DeviceValidator & validator, const SparseTextureBind & bind) noexcept
	{
		return AllUsable(validator, bind.texture, bind.page.heap);
	}

	[[nodiscard]] inline bool ArgumentIsUsable(DeviceValidator & validator, const AccelerationStructureBuildDesc & build) noexcept
	{
		return AllUsable(validator, build.dst, build.src, build.instanceBuffer, build.scratchBuffer);
	}

	[[nodiscard]] inline bool ArgumentIsUsable(DeviceValidator & validator, const DescriptorWriteBuffer & write) noexcept
	{
		return AllUsable(validator, write.set, write.buffer);
	}

	[[nodiscard]] inline bool ArgumentIsUsable(DeviceValidator & validator, const DescriptorWriteTexture & write) noexcept
	{
		return AllUsable(validator, write.set, write.view, write.sampler);
	}

	[[nodiscard]] inline bool ArgumentIsUsable(DeviceValidator & validator, const DescriptorWriteSampler & write) noexcept
	{
		return AllUsable(validator, write.set, write.sampler);
	}

	[[nodiscard]] inline bool ArgumentIsUsable(DeviceValidator & validator, const DescriptorWriteAccelerationStructure & write) noexcept
	{
		return AllUsable(validator, write.set, write.accelerationStructure);
	}

	// A span is checked one element at a time, whatever the element turns out to carry.
	template <class T>
	[[nodiscard]] bool ArgumentIsUsable(DeviceValidator & validator, const std::span<const T> items) noexcept
	{
		for (const T & item : items)
		{
			if (!ArgumentIsUsable(validator, item))
			{
				return false;
			}
		}

		return true;
	}

	[[nodiscard]] inline bool ArgumentIsUsable(DeviceValidator & validator, const BeginRenderingDesc & desc) noexcept
	{
		if (!ArgumentIsUsable(validator, desc.colors))
		{
			return false;
		}

		return desc.depthStencil == nullptr || ArgumentIsUsable(validator, *desc.depthStencil);
	}

	[[nodiscard]] inline bool ArgumentIsUsable(DeviceValidator & validator, const BarrierBatch & batch) noexcept
	{
		return ArgumentIsUsable(validator, batch.buffers) && ArgumentIsUsable(validator, batch.textures);
	}

	// A native mutation scope declares what it is about to touch, so the same refusal a barrier makes belongs on the declaration and not only on the barrier.
	[[nodiscard]] inline bool ArgumentIsUsable(DeviceValidator & validator, const NativeMutationDesc & desc) noexcept
	{
		return ArgumentIsUsable(validator, desc.buffers) && ArgumentIsUsable(validator, desc.textures);
	}

	[[nodiscard]] inline bool ArgumentIsUsable(DeviceValidator & validator, const SparseBindDesc & desc) noexcept
	{
		return ArgumentIsUsable(validator, desc.buffers) && ArgumentIsUsable(validator, desc.textures) && ArgumentIsUsable(validator, desc.timelineWaits) &&
			   ArgumentIsUsable(validator, desc.timelineSignals);
	}

	/*
	 * A pass-through entry that first refuses any argument handle this device has taken back. Generated off the block declaration like the others so which
	 * arguments are handles follows from the signature and not a list somebody keeps current. That is what lets a backend stop checking its own tables. Ungated on
	 * purpose: liveness is a load and two compares and it turns a use after free into a refusal so every mode that is not off pays for it.
	 */
	template <auto Member>
	struct Checked;

	template <class Block, class R, class... Args, R (*Block::*Member)(void *, Args...) noexcept>
	struct Checked<Member>
	{
		static R Call(void * impl, Args... args) noexcept
		{
			auto * self = static_cast<WrappedObject *>(impl);

			bool usable = true;
			((usable = usable && ArgumentIsUsable(*self->validator, args)), ...);
			if (!usable)
			{
				Error * error = nullptr;
				((error = PickError(error, args)), ...);
				return self->validator->FailValue<R>(error, "an operation names a handle this device has already taken back");
			}

			return (InnerBlock<Block>(self)->*Member)(self->inner, args...);
		}
	};

	/*
	 * Whether this command is arriving on the thread that began the list.
	 *
	 * Shared with the entries written out by hand so which commands hold a list to one thread does not depend on which ones happened to need something else doing.
	 */
	[[nodiscard]] inline bool OnItsRecordingThread(WrappedCommandList * self) noexcept
	{
		return !self->checksThread || std::this_thread::get_id() == self->recordingThread;
	}

	/*
	 * The same refusal Recorded makes, for the command entries written out by hand.
	 *
	 * Same reason AllUsable exists: an entry that already had a reason to be written out still has to hold the list to one thread or which commands enforce it
	 * would depend on which ones happened to need something else doing.
	 */
	[[nodiscard]] inline bool RecordedOnItsOwnThread(WrappedCommandList * self, Error * error) noexcept
	{
		return OnItsRecordingThread(self) ? true : self->validator->Fail(error, "a command recorded on a thread other than the one that began the list");
	}

	/*
	 * Whether there is an open recording for this command to go into.
	 *
	 * The thread rule deliberately stands aside before Begin, since no thread has claimed the list yet, which leaves this to say what a command arriving then
	 * actually did wrong. No backend can be relied on to say it: Vulkan records through entries that return void, so the driver refusing is not something the
	 * call reports, and one generation of Metal faulted rather than returning at all.
	 */
	[[nodiscard]] inline bool RecordedIntoAnOpenList(WrappedCommandList * self, Error * error) noexcept
	{
		return self->recording ? true : self->validator->Fail(error, "a command recorded on a list that is not between Begin and End");
	}

	/*
	 * A command-list entry with nothing to check, which still has to arrive on the thread that began the list. The recording-thread rule is about the list so
	 * every entry it publishes carries it. This and RecordedChecked below are Forward and Checked with that rule in front. Kept as two because the entries naming
	 * no handle are the dynamic state a frame records most of. Routing those through the handle sweep would cost a call to look at nothing.
	 */
	template <auto Member>
	struct Recorded;

	template <class Block, class R, class... Args, R (*Block::*Member)(void *, Args...) noexcept>
	struct Recorded<Member>
	{
		static R Call(void * impl, Args... args) noexcept
		{
			auto * self = static_cast<WrappedCommandList *>(impl);
			if (!OnItsRecordingThread(self)) [[unlikely]]
			{
				Error * error = nullptr;
				((error = PickError(error, args)), ...);
				return self->validator->FailValue<R>(error, "a command recorded on a thread other than the one that began the list");
			}

			if (!self->recording) [[unlikely]]
			{
				Error * error = nullptr;
				((error = PickError(error, args)), ...);
				return self->validator->FailValue<R>(error, "a command recorded on a list that is not between Begin and End");
			}

			return (InnerBlock<Block>(self)->*Member)(self->inner, args...);
		}
	};

	/*
	 * A command-list entry that also refuses any handle among its arguments that this device has taken back.
	 */
	template <auto Member>
	struct RecordedChecked;

	template <class Block, class R, class... Args, R (*Block::*Member)(void *, Args...) noexcept>
	struct RecordedChecked<Member>
	{
		static R Call(void * impl, Args... args) noexcept
		{
			auto * self = static_cast<WrappedCommandList *>(impl);
			if (!OnItsRecordingThread(self)) [[unlikely]]
			{
				Error * error = nullptr;
				((error = PickError(error, args)), ...);
				return self->validator->FailValue<R>(error, "a command recorded on a thread other than the one that began the list");
			}

			if (!self->recording) [[unlikely]]
			{
				Error * error = nullptr;
				((error = PickError(error, args)), ...);
				return self->validator->FailValue<R>(error, "a command recorded on a list that is not between Begin and End");
			}

			return Checked<Member>::Call(impl, args...);
		}
	};

	/*
	 * Which shape a command list publishes, decided when the list is allocated, not asked per command. A mode that does not check the recording thread publishes
	 * the plain entries so it runs the same instructions it ran before the rule existed. The alternative, one table with a flag every entry reads, measured at a
	 * third of a nanosecond per command against Null. The mode cannot change under a device so there is nothing to keep in step.
	 */
	template <bool ChecksThread, auto Member>
	using RecordedEntry = std::conditional_t<ChecksThread, Recorded<Member>, Forward<Member>>;

	template <bool ChecksThread, auto Member>
	using RecordedCheckedEntry = std::conditional_t<ChecksThread, RecordedChecked<Member>, Checked<Member>>;

	/*
	 * A create, with the handle it produced written into the registry so a later use can be checked against it.
	 *
	 * Generated off the block's own declaration the same way a pass-through is because writing down the handle is the only thing this adds and every create
	 * returns it the same way. The kind is a template argument because a handle does not carry one.
	 */
	template <ResourceType Type, auto Member>
	struct Recording;

	template <ResourceType Type, class Block, class Produced, class... Args, Produced (*Block::*Member)(void *, Args...) noexcept>
	struct Recording<Type, Member>
	{
		static Produced Call(void * impl, Args... args) noexcept
		{
			auto * self = static_cast<WrappedObject *>(impl);

			// A create reads handles too, its desc naming the layout, cache or heap it is built from and those are as able to be stale as any other.
			if (!AllUsable(*self->validator, args...))
			{
				Error * error = nullptr;
				((error = PickError(error, args)), ...);
				return self->validator->FailValue<Produced>(error, "a create names a handle this device has already taken back");
			}

			const Produced handle = (InnerBlock<Block>(self)->*Member)(self->inner, args...);
			if (handle.IsValid())
			{
				/*
				 * A registry that could not grow leaves the handle unrecorded so a later use of it reads as one this device never handed out. The resource is real either
				 * way and failing the create because the checking could not keep up would be the worse of the two answers.
				 */
				static_cast<void>(self->validator->Handles().Record(RegisteredHandle{
					.type		= Type,
					.index		= handle.index,
					.generation = handle.generation,
				}));
			}

			return handle;
		}
	};

	/*
	 * The state an adopted object arrives in, found among the arguments by type the way the error pointer is.
	 *
	 * Only the two adoption descriptions carry one. Everything else answers with nothing, so one fold over the argument pack reaches whichever of them the entry
	 * took without the wrapper naming a position.
	 */
	struct AdoptedSeed final
	{
		ResourceState state{};
		std::uint32_t queueFamily = kIgnoreQueueFamily;
		bool present			  = false;
	};

	template <class T>
	[[nodiscard]] AdoptedSeed PickAdoptedSeed(AdoptedSeed found, const T &) noexcept
	{
		return found;
	}

	[[nodiscard]] inline AdoptedSeed PickAdoptedSeed([[maybe_unused]] AdoptedSeed found, const AdoptedBufferDesc & desc) noexcept
	{
		return AdoptedSeed{ .state = desc.initialState, .queueFamily = desc.initialQueueFamily, .present = true };
	}

	[[nodiscard]] inline AdoptedSeed PickAdoptedSeed([[maybe_unused]] AdoptedSeed found, const AdoptedTextureDesc & desc) noexcept
	{
		return AdoptedSeed{ .state = desc.initialState, .queueFamily = desc.initialQueueFamily, .present = true };
	}

	/*
	 * An adopt, which is a create whose result does not start in no state.
	 *
	 * Recording above writes the handle and nothing else, which is right for a create: a resource this device just made genuinely has no access and no owner yet.
	 * An adopted object arrives in whatever its producer left it in, so its record is seeded from the description instead, and the caller's first barrier then
	 * names a before state that matches reality and not a cleared one.
	 */
	template <ResourceType Type, auto Member>
	struct RecordingAdopted;

	template <ResourceType Type, class Block, class Produced, class... Args, Produced (*Block::*Member)(void *, Args...) noexcept>
	struct RecordingAdopted<Type, Member>
	{
		static Produced Call(void * impl, Args... args) noexcept
		{
			auto * self = static_cast<WrappedObject *>(impl);
			if (!AllUsable(*self->validator, args...))
			{
				Error * error = nullptr;
				((error = PickError(error, args)), ...);
				return self->validator->FailValue<Produced>(error, "an adopt names a handle this device has already taken back");
			}

			const Produced handle = (InnerBlock<Block>(self)->*Member)(self->inner, args...);
			if (!handle.IsValid())
			{
				return handle;
			}

			const RegisteredHandle registered{
				.type		= Type,
				.index		= handle.index,
				.generation = handle.generation,
			};
			if (!self->validator->Handles().Record(registered))
			{
				// Unrecorded for the reason a create's is: the object is real either way and failing because the checking could not keep up is worse.
				return handle;
			}

			AdoptedSeed seed{};
			((seed = PickAdoptedSeed(seed, args)), ...);
			if (!seed.present)
			{
				return handle;
			}

			if (ResourceRecord * record = self->validator->Handles().Lookup(registered))
			{
				record->access.store(static_cast<std::uint32_t>(seed.state.access.Bits()), std::memory_order_relaxed);
				record->accessKnown.store(true, std::memory_order_relaxed);

				// An ignored family means nothing owns it in a way a transfer would have to name, which is the same thing a fresh create records.
				if (seed.queueFamily != kIgnoreQueueFamily)
				{
					record->owner.store(static_cast<std::uint8_t>(seed.queueFamily), std::memory_order_relaxed);
					record->owned.store(true, std::memory_order_relaxed);
				}
			}

			return handle;
		}
	};

	/*
	 * An entry handing back a handle the device owns but no create this layer saw produced. The swapchain's back buffers, their views and its per-image
	 * semaphores, which the backend mints when the swapchain is built and hands out by index. Not Recording, which is for creates: that writes the record every
	 * time so asking once a frame would clear the queue ownership and the state a barrier left. This writes the record only when there is not one already.
	 */
	template <ResourceType Type, auto Member>
	struct Vending;

	template <ResourceType Type, class Block, class Produced, class... Args, Produced (*Block::*Member)(void *, Args...) noexcept>
	struct Vending<Type, Member>
	{
		static Produced Call(void * impl, Args... args) noexcept
		{
			auto * self			  = static_cast<WrappedObject *>(impl);
			const Produced handle = (InnerBlock<Block>(self)->*Member)(self->inner, args...);
			if (!handle.IsValid())
			{
				return handle;
			}

			const RegisteredHandle registered{
				.type		= Type,
				.index		= handle.index,
				.generation = handle.generation,
			};
			if (self->validator->Handles().Lookup(registered) == nullptr)
			{
				static_cast<void>(self->validator->Handles().Record(registered));
			}

			return handle;
		}
	};

	/*
	 * Wraps a device the backend just created or returns null when the wrapper could not be allocated.
	 *
	 * The returned pointer stands in for the device everywhere above it and destroying it through CoreDeviceApi::destroyDevice destroys the inner device and frees
	 * this.
	 */
	[[nodiscard]] void * WrapDevice(void * deviceImpl, ValidationMode mode) noexcept;

	/*
	 * The validator behind a wrapped device or null when the pointer is not one.
	 *
	 * For the tests, which read the failure count without parsing messages.
	 *
	 * Exported, unlike the rest of this header, because a shared build puts the suite on the far side of a binary boundary and an unmarked symbol does not cross
	 * one. It widens nothing a consumer can reach: this header is internal and is never installed, so the declaration is unavailable outside the tree.
	 */
	[[nodiscard]] AZO_RHI_API DeviceValidator * ValidatorOf(void * deviceImpl) noexcept;

} // namespace azo::rhi::validation

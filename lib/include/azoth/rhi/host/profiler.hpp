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

#include "azoth/rhi/core/api.hpp"
#include "azoth/rhi/core/c_string.hpp"
#include "azoth/rhi/device/device.hpp"
#include "azoth/rhi/device/threading.hpp"

#include <atomic>
#include <cstdint>
#include <span>

namespace azo::rhi
{

	struct ZoneLocation final
	{
		CString name	   = nullptr;
		CString file	   = nullptr;
		std::uint32_t line = 0;

		std::uint32_t color = 0;
	};

	class Profiler
	{
	public:
		Profiler()							   = default;
		Profiler(const Profiler &)			   = delete;
		Profiler & operator=(const Profiler &) = delete;
		Profiler(Profiler &&)				   = delete;
		Profiler & operator=(Profiler &&)	   = delete;
		virtual ~Profiler()					   = default;

		virtual void begin_zone([[maybe_unused]] const ZoneLocation & location) {}

		virtual void end_zone() {}

		virtual void plot([[maybe_unused]] CString name, [[maybe_unused]] std::int64_t value) {}

		virtual void gpu_allocate([[maybe_unused]] const void * address, [[maybe_unused]] std::uint64_t size, [[maybe_unused]] CString pool) {}

		virtual void gpu_free([[maybe_unused]] const void * address, [[maybe_unused]] CString pool) {}

		[[nodiscard]] virtual bool initialize_gpu([[maybe_unused]] Device device, [[maybe_unused]] CommandList & cmdList)
		{
			return false;
		}

		virtual void shutdown_gpu() {}

		virtual void begin_gpu_zone([[maybe_unused]] CommandList & cmdList, [[maybe_unused]] const ZoneLocation & location) {}

		virtual void end_gpu_zone([[maybe_unused]] CommandList & cmdList) {}

		virtual void collect_gpu([[maybe_unused]] CommandList & cmdList) {}

		virtual void enter_fiber([[maybe_unused]] FiberId fiber, [[maybe_unused]] CString name) {}

		virtual void leave_fiber([[maybe_unused]] FiberId fiber) {}
	};

	namespace detail
	{
		[[nodiscard]] AZO_RHI_API std::atomic<Profiler *> & profiler_slot() noexcept;
	}

	inline void set_profiler(Profiler * profiler) noexcept
	{
		detail::profiler_slot().store(profiler, std::memory_order_relaxed);
	}

	[[nodiscard]] inline Profiler * get_profiler() noexcept
	{
		return detail::profiler_slot().load(std::memory_order_relaxed);
	}

	class ScopedProfiler final
	{
	public:
		explicit ScopedProfiler(Profiler * profiler) noexcept : m_previous(get_profiler())
		{
			set_profiler(profiler);
		}

		~ScopedProfiler()
		{
			set_profiler(m_previous);
		}

		ScopedProfiler(const ScopedProfiler &)			   = delete;
		ScopedProfiler & operator=(const ScopedProfiler &) = delete;
		ScopedProfiler(ScopedProfiler &&)				   = delete;
		ScopedProfiler & operator=(ScopedProfiler &&)	   = delete;

		[[nodiscard]] Profiler * previous() const noexcept
		{
			return m_previous;
		}

	private:
		Profiler * m_previous;
	};

	class BroadcastProfiler final : public Profiler
	{
	public:
		explicit BroadcastProfiler(std::span<Profiler * const> sinks) noexcept : m_sinks(sinks) {}

		void begin_zone(const ZoneLocation & location) override
		{
			ForEach(&Profiler::begin_zone, location);
		}

		void end_zone() override
		{
			ForEach(&Profiler::end_zone);
		}

		void plot(CString name, std::int64_t value) override
		{
			ForEach(&Profiler::plot, name, value);
		}

		void gpu_allocate(const void * address, std::uint64_t size, CString pool) override
		{
			ForEach(&Profiler::gpu_allocate, address, size, pool);
		}

		void gpu_free(const void * address, CString pool) override
		{
			ForEach(&Profiler::gpu_free, address, pool);
		}

		[[nodiscard]] bool initialize_gpu(Device device, CommandList & cmdList) override
		{
			bool any = false;
			for (Profiler * sink : m_sinks)
			{
				if (sink != nullptr && sink->initialize_gpu(device, cmdList))
				{
					any = true;
				}
			}
			return any;
		}

		void shutdown_gpu() override
		{
			ForEach(&Profiler::shutdown_gpu);
		}

		void begin_gpu_zone(CommandList & cmdList, const ZoneLocation & location) override
		{
			ForEach(&Profiler::begin_gpu_zone, cmdList, location);
		}

		void end_gpu_zone(CommandList & cmdList) override
		{
			ForEach(&Profiler::end_gpu_zone, cmdList);
		}

		void collect_gpu(CommandList & cmdList) override
		{
			ForEach(&Profiler::collect_gpu, cmdList);
		}

		void enter_fiber(FiberId fiber, CString name) override
		{
			ForEach(&Profiler::enter_fiber, fiber, name);
		}

		void leave_fiber(FiberId fiber) override
		{
			ForEach(&Profiler::leave_fiber, fiber);
		}

	private:
		template <typename Method, typename... Args>
		// NOLINTNEXTLINE(cppcoreguidelines-missing-std-forward): every sink is called with the same arguments, so forwarding would move them into the first.
		void ForEach(Method method, Args &&... args)
		{
			for (Profiler * sink : m_sinks)
			{
				if (sink != nullptr)
				{
					(sink->*method)(args...);
				}
			}
		}

		std::span<Profiler * const> m_sinks;
	};

	namespace detail
	{
		class ScopedZone final
		{
		public:
			explicit ScopedZone(const ZoneLocation & location) noexcept : m_profiler(get_profiler())
			{
				if (m_profiler != nullptr)
				{
					m_profiler->begin_zone(location);
				}
			}

			~ScopedZone()
			{
				if (m_profiler != nullptr)
				{
					m_profiler->end_zone();
				}
			}

			ScopedZone(const ScopedZone &)			   = delete;
			ScopedZone & operator=(const ScopedZone &) = delete;
			ScopedZone(ScopedZone &&)				   = delete;
			ScopedZone & operator=(ScopedZone &&)	   = delete;

		private:
			Profiler * m_profiler;
		};

		class ScopedGpuZone final
		{
		public:
			ScopedGpuZone(CommandList & cmdList, const ZoneLocation & location) noexcept : m_profiler(get_profiler()), m_cmdList(cmdList)
			{
				if (m_profiler != nullptr)
				{
					m_profiler->begin_gpu_zone(m_cmdList, location);
				}
			}

			~ScopedGpuZone()
			{
				if (m_profiler != nullptr)
				{
					m_profiler->end_gpu_zone(m_cmdList);
				}
			}

			ScopedGpuZone(const ScopedGpuZone &)			 = delete;
			ScopedGpuZone & operator=(const ScopedGpuZone &) = delete;
			ScopedGpuZone(ScopedGpuZone &&)					 = delete;
			ScopedGpuZone & operator=(ScopedGpuZone &&)		 = delete;

		private:
			Profiler * m_profiler;
			CommandList & m_cmdList;
		};

		class FiberSuspension final
		{
		public:
			explicit FiberSuspension(const SyncOps & sync, Profiler * const deviceProfiler) noexcept
				: m_profiler(sync.currentFiber == nullptr ? nullptr : (deviceProfiler != nullptr ? deviceProfiler : get_profiler()))
			{
				if (m_profiler == nullptr)
				{
					return;
				}

				m_fiber = sync.currentFiber(sync.context);
				m_profiler->leave_fiber(m_fiber);
			}

			~FiberSuspension()
			{
				if (m_profiler != nullptr)
				{
					m_profiler->enter_fiber(m_fiber, nullptr);
				}
			}

			FiberSuspension(const FiberSuspension &)			 = delete;
			FiberSuspension & operator=(const FiberSuspension &) = delete;
			FiberSuspension(FiberSuspension &&)					 = delete;
			FiberSuspension & operator=(FiberSuspension &&)		 = delete;

		private:
			Profiler * m_profiler;
			FiberId m_fiber{};
		};
	}

}

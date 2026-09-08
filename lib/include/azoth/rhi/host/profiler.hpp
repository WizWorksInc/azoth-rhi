// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
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

		virtual void BeginZone([[maybe_unused]] const ZoneLocation & location) {}

		virtual void EndZone() {}

		virtual void Plot([[maybe_unused]] CString name, [[maybe_unused]] std::int64_t value) {}

		virtual void GpuAllocate([[maybe_unused]] const void * address, [[maybe_unused]] std::uint64_t size, [[maybe_unused]] CString pool) {}

		virtual void GpuFree([[maybe_unused]] const void * address, [[maybe_unused]] CString pool) {}

		[[nodiscard]] virtual bool InitializeGpu([[maybe_unused]] Device device, [[maybe_unused]] CommandList & cmdList)
		{
			return false;
		}

		virtual void ShutdownGpu() {}

		virtual void BeginGpuZone([[maybe_unused]] CommandList & cmdList, [[maybe_unused]] const ZoneLocation & location) {}

		virtual void EndGpuZone([[maybe_unused]] CommandList & cmdList) {}

		virtual void CollectGpu([[maybe_unused]] CommandList & cmdList) {}

		virtual void EnterFiber([[maybe_unused]] FiberId fiber, [[maybe_unused]] CString name) {}

		virtual void LeaveFiber([[maybe_unused]] FiberId fiber) {}
	};

	namespace detail
	{
		[[nodiscard]] AZO_RHI_API std::atomic<Profiler *> & ProfilerSlot() noexcept;
	}

	inline void SetProfiler(Profiler * profiler) noexcept
	{
		detail::ProfilerSlot().store(profiler, std::memory_order_relaxed);
	}

	[[nodiscard]] inline Profiler * GetProfiler() noexcept
	{
		return detail::ProfilerSlot().load(std::memory_order_relaxed);
	}

	class ScopedProfiler final
	{
	public:
		explicit ScopedProfiler(Profiler * profiler) noexcept : m_previous(GetProfiler())
		{
			SetProfiler(profiler);
		}

		~ScopedProfiler()
		{
			SetProfiler(m_previous);
		}

		ScopedProfiler(const ScopedProfiler &)			   = delete;
		ScopedProfiler & operator=(const ScopedProfiler &) = delete;
		ScopedProfiler(ScopedProfiler &&)				   = delete;
		ScopedProfiler & operator=(ScopedProfiler &&)	   = delete;

		[[nodiscard]] Profiler * Previous() const noexcept
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

		void BeginZone(const ZoneLocation & location) override
		{
			ForEach(&Profiler::BeginZone, location);
		}

		void EndZone() override
		{
			ForEach(&Profiler::EndZone);
		}

		void Plot(CString name, std::int64_t value) override
		{
			ForEach(&Profiler::Plot, name, value);
		}

		void GpuAllocate(const void * address, std::uint64_t size, CString pool) override
		{
			ForEach(&Profiler::GpuAllocate, address, size, pool);
		}

		void GpuFree(const void * address, CString pool) override
		{
			ForEach(&Profiler::GpuFree, address, pool);
		}

		[[nodiscard]] bool InitializeGpu(Device device, CommandList & cmdList) override
		{
			bool any = false;
			for (Profiler * sink : m_sinks)
			{
				if (sink != nullptr && sink->InitializeGpu(device, cmdList))
				{
					any = true;
				}
			}
			return any;
		}

		void ShutdownGpu() override
		{
			ForEach(&Profiler::ShutdownGpu);
		}

		void BeginGpuZone(CommandList & cmdList, const ZoneLocation & location) override
		{
			ForEach(&Profiler::BeginGpuZone, cmdList, location);
		}

		void EndGpuZone(CommandList & cmdList) override
		{
			ForEach(&Profiler::EndGpuZone, cmdList);
		}

		void CollectGpu(CommandList & cmdList) override
		{
			ForEach(&Profiler::CollectGpu, cmdList);
		}

		void EnterFiber(FiberId fiber, CString name) override
		{
			ForEach(&Profiler::EnterFiber, fiber, name);
		}

		void LeaveFiber(FiberId fiber) override
		{
			ForEach(&Profiler::LeaveFiber, fiber);
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
			explicit ScopedZone(const ZoneLocation & location) noexcept : m_profiler(GetProfiler())
			{
				if (m_profiler != nullptr)
				{
					m_profiler->BeginZone(location);
				}
			}

			~ScopedZone()
			{
				if (m_profiler != nullptr)
				{
					m_profiler->EndZone();
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
			ScopedGpuZone(CommandList & cmdList, const ZoneLocation & location) noexcept : m_profiler(GetProfiler()), m_cmdList(cmdList)
			{
				if (m_profiler != nullptr)
				{
					m_profiler->BeginGpuZone(m_cmdList, location);
				}
			}

			~ScopedGpuZone()
			{
				if (m_profiler != nullptr)
				{
					m_profiler->EndGpuZone(m_cmdList);
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
				: m_profiler(sync.currentFiber == nullptr ? nullptr : (deviceProfiler != nullptr ? deviceProfiler : GetProfiler()))
			{
				if (m_profiler == nullptr)
				{
					return;
				}

				m_fiber = sync.currentFiber(sync.context);
				m_profiler->LeaveFiber(m_fiber);
			}

			~FiberSuspension()
			{
				if (m_profiler != nullptr)
				{
					m_profiler->EnterFiber(m_fiber, nullptr);
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

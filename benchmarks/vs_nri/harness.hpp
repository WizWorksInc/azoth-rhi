// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include "scene.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifdef TRACY_ENABLE
	#include <tracy/Tracy.hpp>

	#define VSNRI_ZONE(name) ZoneScopedN(name)
	#define VSNRI_ZONE_DYNAMIC(text, size)                                                                                                                     \
		ZoneScoped;                                                                                                                                            \
		ZoneName((text), (size))
	#define VSNRI_FRAME_MARK FrameMark
#else
	#define VSNRI_ZONE(name)
	#define VSNRI_ZONE_DYNAMIC(text, size)
	#define VSNRI_FRAME_MARK
#endif

namespace vsnri
{

	struct ShapeTiming final
	{
		std::uint64_t libraryNs = 0;
		std::uint64_t rawNs		= 0;
	};

	struct FrameTiming final
	{
		std::uint64_t recordNs = 0;
		std::uint64_t submitNs = 0;
		std::uint64_t waitNs   = 0;

		// Timestamps from the frame that last used this slot, read once its fence signaled.
		std::uint64_t gpuNs = 0;
		bool gpuValid		= false;
	};

	// The facts a comparison needs both arms to agree on before their numbers mean anything.
	struct Identity final
	{
		std::string library;
		std::string libraryVersion;
		std::string deviceName;
		std::string driverName;
		std::string driverInfo;
		std::string apiVersion;
		std::string driverVersion;
	};

	// Every call covers a whole pass, frame or batch, so the virtual dispatch is never inside a timed loop.
	class Arm
	{
	public:
		Arm()						 = default;
		Arm(const Arm &)			 = delete;
		Arm & operator=(const Arm &) = delete;
		Arm(Arm &&)					 = delete;
		Arm & operator=(Arm &&)		 = delete;
		virtual ~Arm()				 = default;

		[[nodiscard]] virtual const Identity & Describe() const = 0;

		[[nodiscard]] virtual bool RecordShape(Shape shape, std::uint32_t commands, bool rawFirst, ShapeTiming & timing) = 0;

		[[nodiscard]] virtual bool Frame(std::uint32_t draws, FrameTiming & timing) = 0;

		[[nodiscard]] virtual bool ThreadedFrame(std::uint32_t threads, std::uint32_t draws, FrameTiming & timing) = 0;

		// Untimed: creates and uploads everything the scene frames read.
		[[nodiscard]] virtual bool PrepareScene(const SceneAsset & scene) = 0;

		[[nodiscard]] virtual bool SceneFrame(const SceneFrameDesc & frame, FrameTiming & timing) = 0;

		[[nodiscard]] virtual bool ChurnBatch(Churn churn, std::uint32_t count, std::uint64_t & elapsedNs) = 0;

		[[nodiscard]] virtual bool SubmitRoundTrip(std::uint64_t & elapsedNs) = 0;

		[[nodiscard]] virtual bool Drain() = 0;
	};

	// Each arm routes its library's own host allocations here, never the driver's or VMA's, so both libraries sit on one allocator.
	[[nodiscard]] void * CountedAllocate(std::size_t size, std::size_t alignment) noexcept;

	[[nodiscard]] void * CountedReallocate(void * memory, std::size_t size, std::size_t alignment) noexcept;

	void CountedFree(void * memory) noexcept;

	[[nodiscard]] std::uint64_t HostAllocations() noexcept;

	// Apple Silicon schedules a default-QoS thread onto efficiency cores, which would decide the result by itself.
	void RaiseThreadPriority() noexcept;

	[[nodiscard]] inline std::uint64_t Now() noexcept
	{
		return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
	}

	// Fork and join over persistent threads, shared so both arms pay the same wake-up cost.
	class WorkerPool final
	{
	public:
		explicit WorkerPool(std::uint32_t workers);
		WorkerPool(const WorkerPool &)			   = delete;
		WorkerPool & operator=(const WorkerPool &) = delete;
		WorkerPool(WorkerPool &&)				   = delete;
		WorkerPool & operator=(WorkerPool &&)	   = delete;
		~WorkerPool();

		[[nodiscard]] std::uint32_t Workers() const noexcept
		{
			return static_cast<std::uint32_t>(m_threads.size());
		}

		void Run(std::uint32_t tasks, const std::function<void(std::uint32_t)> & task);

	private:
		void Loop(std::uint32_t worker);

		std::vector<std::thread> m_threads;
		std::mutex m_mutex;
		std::condition_variable m_wake;
		std::condition_variable m_done;
		const std::function<void(std::uint32_t)> * m_task = nullptr;
		std::uint32_t m_tasks							  = 0;
		std::uint32_t m_remaining						  = 0;
		std::uint64_t m_generation						  = 0;
		bool m_stop										  = false;
	};

	// Registers the full set under names both executables share, runs it, and reports the arm's identity as context.
	[[nodiscard]] int RunBenchmarks(int argc, char ** argv, Arm & arm);

	// Parses the flags this harness owns ahead of Google Benchmark's own.
	struct HarnessOptions final
	{
		bool waitForProfiler			 = false;
		double profilerWaitSeconds		 = 30.0;
		bool enableGraphicsApiValidation = false;
		std::filesystem::path sceneData	 = DefaultSceneDataDirectory();
	};

	[[nodiscard]] HarnessOptions ReadHarnessOptions(int argc, char ** argv);

	// Blocks until a Tracy server connects or the wait runs out. Does nothing in a build without Tracy.
	void WaitForProfiler(const HarnessOptions & options);

}

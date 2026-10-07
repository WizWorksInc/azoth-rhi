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

#include "harness.hpp"

#include <benchmark/benchmark.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <format>
#include <memory>
#include <print>
#include <span>
#include <string_view>
#include <system_error>

#ifdef __APPLE__
	#include <pthread/qos.h>
#endif

namespace vsnri
{

	namespace
	{

		std::atomic<std::uint64_t> g_hostAllocations{ 0 };

		constexpr std::string_view kTracyWaitFlag	   = "--tracy-wait=";
		constexpr std::string_view kGapiValidationFlag = "--gapi-validation";
		constexpr std::string_view kSceneDataFlag	   = "--scene-data=";

		constexpr double kShapeMinSeconds  = 0.5;
		constexpr double kFrameMinSeconds  = 1.0;
		constexpr double kChurnMinSeconds  = 0.5;
		constexpr double kSubmitMinSeconds = 0.5;
		constexpr double kWarmUpSeconds	   = 0.25;

		[[nodiscard]] bool IsOwnFlag(const std::string_view argument)
		{
			return argument.starts_with(kTracyWaitFlag) || argument == kGapiValidationFlag || argument.starts_with(kSceneDataFlag);
		}

		[[nodiscard]] bool NamesFlag(const std::span<char *> args, const std::string_view flag)
		{
			for (const char * argument : args)
			{
				const std::string_view text(argument);
				if (text.starts_with(flag) && (text.size() == flag.size() || text.at(flag.size()) == '='))
				{
					return true;
				}
			}

			return false;
		}

		[[nodiscard]] std::string EnvironmentValue(const char * name)
		{
			const char * value = std::getenv(name);
			return value != nullptr ? std::string(value) : std::string("unset");
		}

		[[nodiscard]] double PerUnit(const std::uint64_t total, const double units)
		{
			return units > 0.0 ? static_cast<double>(total) / units : 0.0;
		}

		void RegisterShapes(Arm & arm)
		{
			for (const Shape shape : kShapes)
			{
				const std::string name = std::string("record/") + std::string(ShapeName(shape));

				benchmark::RegisterBenchmark(name,
					[&arm, shape, name](benchmark::State & state)
					{
						VSNRI_ZONE_DYNAMIC(name.c_str(), name.size());

						if (!arm.Drain())
						{
							state.SkipWithError("the arm could not drain the queue before the run");
							return;
						}

						std::uint64_t libraryTotal = 0;
						std::uint64_t rawTotal	   = 0;
						std::uint64_t passes	   = 0;
						bool rawFirst			   = false;

						const std::uint64_t allocationsBefore = HostAllocations();

						while (state.KeepRunningBatch(kShapeCommands))
						{
							ShapeTiming timing{};
							if (!arm.RecordShape(shape, kShapeCommands, rawFirst, timing))
							{
								state.SkipWithError("a recorded pass failed, see the diagnostic above");
								return;
							}

							rawFirst = !rawFirst;
							libraryTotal += timing.libraryNs;
							rawTotal += timing.rawNs;
							++passes;

							state.SetIterationTime(static_cast<double>(timing.libraryNs) / 1e9);
						}

						const double commands = static_cast<double>(state.iterations());

						state.counters["raw_ns"]	  = benchmark::Counter(PerUnit(rawTotal, commands));
						state.counters["over_raw_ns"] = benchmark::Counter(PerUnit(libraryTotal, commands) - PerUnit(rawTotal, commands));
						state.counters["allocs_pass"] = benchmark::Counter(PerUnit(HostAllocations() - allocationsBefore, static_cast<double>(passes)));
					})
					->UseManualTime()
					->Unit(benchmark::kNanosecond)
					->MinWarmUpTime(kWarmUpSeconds)
					->MinTime(kShapeMinSeconds);
			}
		}

		// Every frame's CPU time and every GPU time read back, so the tail is reported and not only the mean.
		struct FrameSamples final
		{
			FrameSamples(const benchmark::State & state, const std::uint32_t overrun)
			{
				const std::size_t frames = static_cast<std::size_t>(state.max_iterations) + overrun;
				cpuNs.reserve(frames);
				gpuNs.reserve(frames);
			}

			void Add(const FrameTiming & timing)
			{
				totals.recordNs += timing.recordNs;
				totals.submitNs += timing.submitNs;
				totals.waitNs += timing.waitNs;

				cpuNs.push_back(timing.recordNs + timing.submitNs);
				if (timing.gpuValid)
				{
					gpuNs.push_back(timing.gpuNs);
				}
			}

			FrameTiming totals{};
			std::vector<std::uint64_t> cpuNs;
			std::vector<std::uint64_t> gpuNs;
		};

		// Nearest rank, on samples already sorted.
		[[nodiscard]] double PercentileMicroseconds(const std::span<const std::uint64_t> sorted, const double fraction)
		{
			if (sorted.empty())
			{
				return 0.0;
			}

			const auto rank = static_cast<std::size_t>(std::ceil(fraction * static_cast<double>(sorted.size())));
			return static_cast<double>(sorted[std::clamp<std::size_t>(rank, 1, sorted.size()) - 1]) / 1e3;
		}

		void ReportFrameCounters(benchmark::State & state, FrameSamples & samples, const std::uint64_t allocations, const bool timesGpu)
		{
			const double frames = static_cast<double>(state.iterations());

			state.counters["record_us"] = benchmark::Counter(PerUnit(samples.totals.recordNs, frames) / 1e3);
			state.counters["submit_us"] = benchmark::Counter(PerUnit(samples.totals.submitNs, frames) / 1e3);
			state.counters["wait_us"]	= benchmark::Counter(PerUnit(samples.totals.waitNs, frames) / 1e3);

			std::ranges::sort(samples.cpuNs);
			state.counters["cpu_p50_us"] = benchmark::Counter(PercentileMicroseconds(samples.cpuNs, 0.50));
			state.counters["cpu_p99_us"] = benchmark::Counter(PercentileMicroseconds(samples.cpuNs, 0.99));
			state.counters["cpu_max_us"] = benchmark::Counter(PercentileMicroseconds(samples.cpuNs, 1.0));

			if (timesGpu)
			{
				std::uint64_t gpuTotal = 0;
				for (const std::uint64_t sample : samples.gpuNs)
				{
					gpuTotal += sample;
				}

				std::ranges::sort(samples.gpuNs);
				state.counters["gpu_us"]	  = benchmark::Counter(PerUnit(gpuTotal, static_cast<double>(samples.gpuNs.size())) / 1e3);
				state.counters["gpu_p50_us"]  = benchmark::Counter(PercentileMicroseconds(samples.gpuNs, 0.50));
				state.counters["gpu_p99_us"]  = benchmark::Counter(PercentileMicroseconds(samples.gpuNs, 0.99));
				state.counters["gpu_samples"] = benchmark::Counter(static_cast<double>(samples.gpuNs.size()));
			}

			state.counters["allocs_frame"] = benchmark::Counter(PerUnit(allocations, frames));
		}

		// The synthetic frame at rising draw counts, read the way 3DMark's API overhead test is: where does CPU time cross a 30 fps budget.
		void RegisterSweep(Arm & arm)
		{
			for (const std::uint32_t draws : kSweepDraws)
			{
				const std::string name = "sweep/draws:" + std::to_string(draws);

				benchmark::RegisterBenchmark(name,
					[&arm, draws, name](benchmark::State & state)
					{
						VSNRI_ZONE_DYNAMIC(name.c_str(), name.size());

						if (!arm.Drain())
						{
							state.SkipWithError("the arm could not drain the queue before the run");
							return;
						}

						FrameSamples samples(state, 0);
						const std::uint64_t allocationsBefore = HostAllocations();

						for (auto _ : state)
						{
							FrameTiming timing{};
							if (!arm.Frame(draws, timing))
							{
								state.SkipWithError("a frame failed, see the diagnostic above");
								return;
							}

							samples.Add(timing);
							state.SetIterationTime(static_cast<double>(timing.recordNs + timing.submitNs) / 1e9);
						}

						ReportFrameCounters(state, samples, HostAllocations() - allocationsBefore, true);
						state.counters["draw_ns"] =
							benchmark::Counter(PerUnit(samples.totals.recordNs, static_cast<double>(state.iterations()) * (draws + draws / 2)));
					})
					->UseManualTime()
					->Unit(benchmark::kMicrosecond)
					->MinWarmUpTime(kWarmUpSeconds)
					->MinTime(kFrameMinSeconds);
			}
		}

		// Whole laps of the camera path only, so both arms average over the same frames whatever their iteration counts.
		void RegisterScene(Arm & arm, const std::span<const SceneWorkload> workloads)
		{
			for (std::size_t replica = 0; replica < workloads.size(); ++replica)
			{
				for (const BindPolicy policy : kBindPolicies)
				{
					const SceneWorkload & workload = workloads[replica];
					const std::string name = "scene/replicas:" + std::to_string(kSceneReplicas.at(replica)) + "/policy:" + std::string(BindPolicyName(policy));

					benchmark::RegisterBenchmark(name,
						[&arm, &workload, policy, name](benchmark::State & state)
						{
							VSNRI_ZONE_DYNAMIC(name.c_str(), name.size());

							if (!arm.Drain())
							{
								state.SkipWithError("the arm could not drain the queue before the run");
								return;
							}

							FrameSamples samples(state, kPathFrames);
							std::vector<SceneDraw> visible;
							visible.reserve(workload.Draws());
							std::uint64_t visibleTotal = 0;

							SceneFrameDesc frame{ .models = workload.Models(), .policy = policy };
							const std::uint64_t allocationsBefore = HostAllocations();

							while (state.KeepRunningBatch(kPathFrames))
							{
								std::uint64_t lapNs = 0;
								for (std::uint32_t step = 0; step < kPathFrames; ++step)
								{
									workload.PrepareFrame(step, policy, frame.globals, visible);
									frame.draws = visible;

									FrameTiming timing{};
									if (!arm.SceneFrame(frame, timing))
									{
										state.SkipWithError("a scene frame failed, see the diagnostic above");
										return;
									}

									samples.Add(timing);
									visibleTotal += visible.size();
									lapNs += timing.recordNs + timing.submitNs;
								}

								state.SetIterationTime(static_cast<double>(lapNs) / 1e9);
							}

							ReportFrameCounters(state, samples, HostAllocations() - allocationsBefore, true);
							state.counters["draws_frame"] = benchmark::Counter(PerUnit(visibleTotal, static_cast<double>(state.iterations())));
						})
						->UseManualTime()
						->Unit(benchmark::kMicrosecond)
						->MinWarmUpTime(kWarmUpSeconds)
						->MinTime(kFrameMinSeconds);
				}
			}
		}

		void RegisterThreads(Arm & arm)
		{
			for (const std::uint32_t threads : kThreadCounts)
			{
				const std::string name = "threads/threads:" + std::to_string(threads);

				benchmark::RegisterBenchmark(name,
					[&arm, threads, name](benchmark::State & state)
					{
						VSNRI_ZONE_DYNAMIC(name.c_str(), name.size());

						if (!arm.Drain())
						{
							state.SkipWithError("the arm could not drain the queue before the run");
							return;
						}

						FrameSamples samples(state, 0);
						const std::uint64_t allocationsBefore = HostAllocations();

						for (auto _ : state)
						{
							FrameTiming timing{};
							if (!arm.ThreadedFrame(threads, kThreadedDraws, timing))
							{
								state.SkipWithError("a threaded frame failed, see the diagnostic above");
								return;
							}

							samples.Add(timing);
							state.SetIterationTime(static_cast<double>(timing.recordNs + timing.submitNs) / 1e9);
						}

						ReportFrameCounters(state, samples, HostAllocations() - allocationsBefore, false);
						state.counters["draw_ns"] =
							benchmark::Counter(PerUnit(samples.totals.recordNs, static_cast<double>(state.iterations()) * kThreadedDraws));
					})
					->UseManualTime()
					->Unit(benchmark::kMicrosecond)
					->MinWarmUpTime(kWarmUpSeconds)
					->MinTime(kFrameMinSeconds);
			}
		}

		void RegisterChurn(Arm & arm)
		{
			for (const Churn churn : kChurns)
			{
				const std::string name = std::string("churn/") + std::string(ChurnName(churn));

				benchmark::RegisterBenchmark(name,
					[&arm, churn, name](benchmark::State & state)
					{
						VSNRI_ZONE_DYNAMIC(name.c_str(), name.size());

						if (!arm.Drain())
						{
							state.SkipWithError("the arm could not drain the queue before the run");
							return;
						}

						const std::uint64_t allocationsBefore = HostAllocations();

						while (state.KeepRunningBatch(kChurnBatch))
						{
							std::uint64_t elapsed = 0;
							if (!arm.ChurnBatch(churn, kChurnBatch, elapsed))
							{
								state.SkipWithError("a churn batch failed, see the diagnostic above");
								return;
							}

							state.SetIterationTime(static_cast<double>(elapsed) / 1e9);
						}

						state.counters["allocs_op"] =
							benchmark::Counter(PerUnit(HostAllocations() - allocationsBefore, static_cast<double>(state.iterations())));
					})
					->UseManualTime()
					->Unit(benchmark::kNanosecond)
					->MinWarmUpTime(kWarmUpSeconds)
					->MinTime(kChurnMinSeconds);
			}
		}

		void RegisterSubmit(Arm & arm)
		{
			benchmark::RegisterBenchmark("submit/RoundTrip",
				[&arm](benchmark::State & state)
				{
					VSNRI_ZONE("submit/RoundTrip");

					if (!arm.Drain())
					{
						state.SkipWithError("the arm could not drain the queue before the run");
						return;
					}

					const std::uint64_t allocationsBefore = HostAllocations();

					for (auto _ : state)
					{
						std::uint64_t elapsed = 0;
						if (!arm.SubmitRoundTrip(elapsed))
						{
							state.SkipWithError("a submission failed, see the diagnostic above");
							return;
						}

						state.SetIterationTime(static_cast<double>(elapsed) / 1e9);
					}

					state.counters["allocs_op"] = benchmark::Counter(PerUnit(HostAllocations() - allocationsBefore, static_cast<double>(state.iterations())));
				})
				->UseManualTime()
				->Unit(benchmark::kMicrosecond)
				->MinWarmUpTime(kWarmUpSeconds)
				->MinTime(kSubmitMinSeconds);
		}

		void AddContext(const Arm & arm, const HarnessOptions & options)
		{
			const Identity & identity = arm.Describe();

			benchmark::AddCustomContext("library", identity.library);
			benchmark::AddCustomContext("library_version", identity.libraryVersion);
			benchmark::AddCustomContext("device", identity.deviceName);
			benchmark::AddCustomContext("driver", identity.driverName);
			benchmark::AddCustomContext("driver_info", identity.driverInfo);
			benchmark::AddCustomContext("driver_version", identity.driverVersion);
			benchmark::AddCustomContext("vulkan_api", identity.apiVersion);
			benchmark::AddCustomContext("VK_DRIVER_FILES", EnvironmentValue("VK_DRIVER_FILES"));
			benchmark::AddCustomContext("VK_ICD_FILENAMES", EnvironmentValue("VK_ICD_FILENAMES"));
			benchmark::AddCustomContext("VK_INSTANCE_LAYERS", EnvironmentValue("VK_INSTANCE_LAYERS"));
			benchmark::AddCustomContext("gapi_validation", options.enableGraphicsApiValidation ? "on" : "off");

#ifdef NDEBUG
			benchmark::AddCustomContext("asserts", "off");
#else
			benchmark::AddCustomContext("asserts", "on");
#endif

#ifdef TRACY_ENABLE
			benchmark::AddCustomContext("tracy", "on, these numbers carry Tracy's cost");
#else
			benchmark::AddCustomContext("tracy", "off");
#endif

#ifdef __clang_version__
			benchmark::AddCustomContext("compiler", __clang_version__);
#endif
		}

		void AddSceneContext(const SceneAsset & scene, const std::filesystem::path & document)
		{
			std::array<std::uint32_t, 3> formats{};
			std::uint64_t mips = 0;
			for (const SceneTexture & texture : scene.textures)
			{
				++formats.at(static_cast<std::size_t>(texture.format));
				mips += texture.mips.size();
			}

			benchmark::AddCustomContext("scene", document.filename().string());
			benchmark::AddCustomContext("scene_instances", std::to_string(scene.instances.size()));
			benchmark::AddCustomContext("scene_meshes", std::to_string(scene.meshes.size()));
			benchmark::AddCustomContext("scene_materials", std::to_string(scene.materials.size()));
			benchmark::AddCustomContext("scene_triangles", std::to_string(scene.triangles));
			benchmark::AddCustomContext(
				"scene_textures", std::format("{} ({} rgba8, {} bc1, {} bc7, {} mips)", scene.textures.size(), formats[0], formats[1], formats[2], mips));
			benchmark::AddCustomContext("scene_path_frames", std::to_string(kPathFrames));
		}

	}

	namespace
	{

		struct AllocationHeader final
		{
			void * base		 = nullptr;
			std::size_t size = 0;
		};

		[[nodiscard]] void * AlignedAllocate(const std::size_t size, const std::size_t requested) noexcept
		{
			const std::size_t alignment = std::max(requested, alignof(AllocationHeader));
			void * base					= std::malloc(size + alignment + sizeof(AllocationHeader));
			if (base == nullptr)
			{
				return nullptr;
			}

			const std::uintptr_t first	 = reinterpret_cast<std::uintptr_t>(base) + sizeof(AllocationHeader);
			const std::uintptr_t aligned = (first + alignment - 1) & ~(alignment - 1);
			std::construct_at(reinterpret_cast<AllocationHeader *>(aligned - sizeof(AllocationHeader)), AllocationHeader{ base, size });
			return reinterpret_cast<void *>(aligned);
		}

		[[nodiscard]] const AllocationHeader & HeaderOf(void * memory) noexcept
		{
			return *reinterpret_cast<const AllocationHeader *>(static_cast<std::byte *>(memory) - sizeof(AllocationHeader));
		}

	}

	void * CountedAllocate(const std::size_t size, const std::size_t alignment) noexcept
	{
		g_hostAllocations.fetch_add(1, std::memory_order_relaxed);
		return AlignedAllocate(size, alignment);
	}

	void * CountedReallocate(void * memory, const std::size_t size, const std::size_t alignment) noexcept
	{
		g_hostAllocations.fetch_add(1, std::memory_order_relaxed);
		void * moved = AlignedAllocate(size, alignment);
		if (moved == nullptr)
		{
			return nullptr;
		}

		if (memory != nullptr)
		{
			std::memcpy(moved, memory, std::min(size, HeaderOf(memory).size));
			CountedFree(memory);
		}

		return moved;
	}

	void CountedFree(void * memory) noexcept
	{
		if (memory != nullptr)
		{
			std::free(HeaderOf(memory).base);
		}
	}

	std::uint64_t HostAllocations() noexcept
	{
		return g_hostAllocations.load(std::memory_order_relaxed);
	}

	void RaiseThreadPriority() noexcept
	{
#ifdef __APPLE__
		pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
#endif
	}

	WorkerPool::WorkerPool(const std::uint32_t workers)
	{
		m_threads.reserve(workers);
		for (std::uint32_t worker = 0; worker < workers; ++worker)
		{
			m_threads.emplace_back(
				[this, worker]
				{
					Loop(worker);
				});
		}
	}

	WorkerPool::~WorkerPool()
	{
		{
			const std::scoped_lock lock(m_mutex);
			m_stop = true;
		}
		m_wake.notify_all();

		for (std::thread & thread : m_threads)
		{
			thread.join();
		}
	}

	void WorkerPool::Run(const std::uint32_t tasks, const std::function<void(std::uint32_t)> & task)
	{
		{
			const std::scoped_lock lock(m_mutex);
			m_task		= &task;
			m_tasks		= tasks;
			m_remaining = tasks;
			++m_generation;
		}
		m_wake.notify_all();

		std::unique_lock lock(m_mutex);
		m_done.wait(lock,
			[this]
			{
				return m_remaining == 0;
			});
		m_task = nullptr;
	}

	void WorkerPool::Loop(const std::uint32_t worker)
	{
		RaiseThreadPriority();

		std::uint64_t seen = 0;
		for (;;)
		{
			const std::function<void(std::uint32_t)> * task = nullptr;
			{
				std::unique_lock lock(m_mutex);
				m_wake.wait(lock,
					[this, seen]
					{
						return m_stop || m_generation != seen;
					});
				if (m_stop)
				{
					return;
				}

				seen = m_generation;
				if (worker >= m_tasks)
				{
					continue;
				}
				task = m_task;
			}

			(*task)(worker);

			{
				const std::scoped_lock lock(m_mutex);
				--m_remaining;
			}
			m_done.notify_one();
		}
	}

	HarnessOptions ReadHarnessOptions(const int argc, char ** argv)
	{
		HarnessOptions options{};

		for (const char * argument : std::span(argv, static_cast<std::size_t>(argc)).subspan(1))
		{
			const std::string_view text(argument);
			if (text == kGapiValidationFlag)
			{
				options.enableGraphicsApiValidation = true;
			}
			else if (text.starts_with(kTracyWaitFlag))
			{
				const std::string_view value = text.substr(kTracyWaitFlag.size());
				double seconds				 = 0.0;
				if (const auto [end, failure] = std::from_chars(value.data(), value.data() + value.size(), seconds);
					failure == std::errc{} && end == value.data() + value.size())
				{
					options.waitForProfiler		= seconds > 0.0;
					options.profilerWaitSeconds = seconds;
				}
			}
			else if (text.starts_with(kSceneDataFlag))
			{
				options.sceneData = std::filesystem::path(text.substr(kSceneDataFlag.size()));
			}
		}

		return options;
	}

	void WaitForProfiler([[maybe_unused]] const HarnessOptions & options)
	{
#if defined(TRACY_ENABLE) && defined(TRACY_ON_DEMAND)
		if (!options.waitForProfiler)
		{
			std::println("tracy: on demand and not waiting, so nothing is recorded unless a server is already connected. Pass --tracy-wait=30 to wait.");
			return;
		}

		std::println("tracy: waiting up to {} seconds for a server", options.profilerWaitSeconds);
		const std::uint64_t until = Now() + static_cast<std::uint64_t>(options.profilerWaitSeconds * 1e9);
		while (!TracyIsConnected && Now() < until)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		}

		std::println("tracy: {}", TracyIsConnected ? "connected" : "no server connected, so this run records nothing");
#endif
	}

	int RunBenchmarks(const int argc, char ** argv, Arm & arm)
	{
		const HarnessOptions options = ReadHarnessOptions(argc, argv);

		std::vector<char *> args;
		for (char * argument : std::span(argv, static_cast<std::size_t>(argc)))
		{
			if (!IsOwnFlag(argument))
			{
				args.push_back(argument);
			}
		}

		// Five repetitions in a shuffled order, so drift over the run lands on every benchmark rather than the last ones.
		std::string repetitions	 = "--benchmark_repetitions=5";
		std::string interleaving = "--benchmark_enable_random_interleaving=true";
		if (!NamesFlag(args, "--benchmark_repetitions"))
		{
			args.insert(args.begin() + 1, repetitions.data());
		}
		if (!NamesFlag(args, "--benchmark_enable_random_interleaving"))
		{
			args.insert(args.begin() + 1, interleaving.data());
		}

		int count = static_cast<int>(args.size());
		benchmark::Initialize(&count, args.data());
		if (benchmark::ReportUnrecognizedArguments(count, args.data()))
		{
			return 2;
		}

		const std::filesystem::path document = options.sceneData / "Scenes" / "ShaderBalls" / "ShaderBalls.gltf";
		SceneAsset scene;
		std::string error;
		if (!LoadScene(document, options.sceneData / "Textures", scene, error))
		{
			std::println(stderr, "scene: {} did not load: {}", document.string(), error);
			return 1;
		}

		if (!arm.PrepareScene(scene))
		{
			std::println(stderr, "scene: the arm could not upload the scene, see the diagnostic above");
			return 1;
		}

		std::vector<SceneWorkload> workloads;
		workloads.reserve(kSceneReplicas.size());
		for (const std::uint32_t replicas : kSceneReplicas)
		{
			workloads.emplace_back(scene, replicas);
		}

		RegisterShapes(arm);
		RegisterSweep(arm);
		RegisterScene(arm, workloads);
		RegisterThreads(arm);
		RegisterChurn(arm);
		RegisterSubmit(arm);

		AddContext(arm, options);
		AddSceneContext(scene, document);

		benchmark::RunSpecifiedBenchmarks();
		benchmark::Shutdown();

		return arm.Drain() ? 0 : 1;
	}

}

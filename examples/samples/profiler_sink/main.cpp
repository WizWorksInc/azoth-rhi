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

#include "azoth/rhi/commands/command.hpp"
#include "azoth/rhi/commands/sync.hpp"
#include "azoth/rhi/core/c_string.hpp"
#include "azoth/rhi/core/enums.hpp"
#include "azoth/rhi/core/resource_handles.hpp"
#include "azoth/rhi/core/result.hpp"
#include "azoth/rhi/core/version.hpp"
#include "azoth/rhi/device/api_tags.hpp"
#include "azoth/rhi/device/device.hpp"
#include "azoth/rhi/device/selection.hpp"
#include "azoth/rhi/host/profiler.hpp"
#include "azoth/rhi/resources/resources.hpp"

#include "FW/utility/Log.hpp"
#include "FW/utility/Sample.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <mutex>
#include <print>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rhi = azo::rhi;

namespace
{

	// Counts what the RHI reports. Calls arrive from whichever thread is recording so the tally is behind a mutex: a sink that is not thread safe is a data
	// race waiting for a second thread.
	class TallyProfiler final : public rhi::Profiler
	{
	public:
		void BeginZone(const rhi::ZoneLocation & location) override
		{
			const std::scoped_lock lock(m_mutex);

			++m_open;
			m_deepest = std::max(m_deepest, m_open);
			Accumulate(m_zones, location.name, 1);
		}

		void EndZone() override
		{
			const std::scoped_lock lock(m_mutex);
			if (m_open != 0)
			{
				--m_open;
			}
		}

		void Plot(const rhi::CString name, const std::int64_t value) override
		{
			const std::scoped_lock lock(m_mutex);
			Latest(m_counters, name, value);
		}

		void GpuAllocate(const void * /*address*/, const std::uint64_t size, const rhi::CString pool) override
		{
			const std::scoped_lock lock(m_mutex);
			++m_allocations;
			m_allocatedBytes += size;
			m_pool = pool;
		}

		void GpuFree(const void * /*address*/, const rhi::CString /*pool*/) override
		{
			const std::scoped_lock lock(m_mutex);
			++m_frees;
		}

		void Report()
		{
			const std::scoped_lock lock(m_mutex);

			std::ranges::sort(m_zones,
				[](const Entry & lhs, const Entry & rhs)
				{
					return lhs.value > rhs.value;
				});

			std::int64_t total = 0;
			for (const Entry & zone : m_zones)
			{
				total += zone.value;
			}

			LOG_INFO(fw::Log(), "\n{} zones over {} call sites, deepest nesting {}", total, m_zones.size(), m_deepest);
			for (const Entry & zone : m_zones)
			{
				LOG_INFO(fw::Log(), "  {}  {}", zone.value, zone.name);
			}

			if (!m_counters.empty())
			{
				LOG_INFO(fw::Log(), "\ncounters, last value seen");
				for (const Entry & counter : m_counters)
				{
					LOG_INFO(fw::Log(), "  {} = {}", counter.name, counter.value);
				}
			}

			std::string memory = std::format("device memory: {} allocations totalling {} bytes, {} frees", m_allocations, m_allocatedBytes, m_frees);
			if (m_pool != nullptr)
			{
				memory += std::format(", pool {}", m_pool);
			}
			LOG_INFO(fw::Log(), "{}", memory);
		}

	private:
		struct Entry final
		{
			rhi::CString name  = nullptr;
			std::int64_t value = 0;
		};

		// One rhi::ZoneLocation exists per call site and every name is a literal so the pointer is a stable key. A tool-backed sink caches its own zone handle
		// on that same property.
		static Entry & Find(std::vector<Entry> & entries, const rhi::CString name)
		{
			for (Entry & entry : entries)
			{
				if (entry.name == name)
				{
					return entry;
				}
			}

			return entries.emplace_back(Entry{ .name = name != nullptr ? name : "unnamed" });
		}

		static void Accumulate(std::vector<Entry> & entries, const rhi::CString name, const std::int64_t amount)
		{
			Find(entries, name).value += amount;
		}

		static void Latest(std::vector<Entry> & entries, const rhi::CString name, const std::int64_t value)
		{
			Find(entries, name).value = value;
		}

		std::mutex m_mutex;
		std::vector<Entry> m_zones;
		std::vector<Entry> m_counters;
		std::uint64_t m_open		   = 0;
		std::uint64_t m_deepest		   = 0;
		std::uint64_t m_allocations	   = 0;
		std::uint64_t m_allocatedBytes = 0;
		std::uint64_t m_frees		   = 0;
		rhi::CString m_pool			   = nullptr;
	};

	// The RHI does not own the sink so it has to outlive every device that might call into it.
	TallyProfiler g_profiler;

	// Enough work to have something to report: a few resources, a recorded list and a submission. The device lives and dies inside here so its teardown is
	// instrumented along with everything else.
	bool RunSomeWork(rhi::BackendSelection & backends)
	{
		rhi::DeviceDesc deviceDesc{};
		deviceDesc.requireSwapchain = false;
		deviceDesc.debugName		= "profiler_sink";

		const rhi::Result<rhi::UniqueDevice> device = backends.CreateDevice(deviceDesc);
		if (!device)
		{
			fw::ReportError("failed to create a device", device.GetError());
			return false;
		}

		rhi::Device dev = device.Value().Get();
		LOG_INFO(fw::Log(), "backend: {}", dev.GetGraphicsApiName());

		rhi::Error error{};

		const rhi::BufferDesc bufferDesc{
			.size	   = 4096,
			.usage	   = rhi::Flags<rhi::BufferUsage>(rhi::BufferUsage::eStorage) | rhi::BufferUsage::eCopyDst,
			.memory	   = rhi::MemoryUsage::eGpuOnly,
			.debugName = "example.profiledBuffer",
		};

		const rhi::TextureDesc textureDesc{
			.type	   = rhi::TextureType::eTex2D,
			.format	   = rhi::Format::eRGBA8UNorm,
			.width	   = 64,
			.height	   = 64,
			.usage	   = rhi::Flags<rhi::TextureUsage>(rhi::TextureUsage::eSampled) | rhi::TextureUsage::eCopyDst,
			.debugName = "example.profiledTexture",
		};

		const rhi::BufferHandle buffer	 = dev.CreateBuffer(bufferDesc, error);
		const rhi::TextureHandle texture = dev.CreateTexture(textureDesc, error);
		rhi::Queue queue				 = dev.GetQueue(rhi::QueueType::eGraphics, 0, error);
		rhi::CommandPool pool			 = dev.CreateCommandPool(rhi::CommandPoolDesc{ .debugName = "example.pool" }, error);
		if (!buffer.IsValid() || !texture.IsValid() || !queue.IsValid() || !pool.IsValid())
		{
			fw::ReportError("failed to create the profiled resources", error);
			return false;
		}

		rhi::CommandList list = pool.Allocate("example.profiledList", error);
		if (!list.IsValid() || !list.Begin(error))
		{
			fw::ReportError("failed to begin recording", error);
			return false;
		}

		if (!list.ClearBuffer(buffer, 0, bufferDesc.size, 0, error) || !list.End(error))
		{
			fw::ReportError("failed to record", error);
			return false;
		}

		std::array<const rhi::CommandList *, 1> lists{ &list };
		if (!queue.Submit(rhi::SubmitDesc{ .commandLists = lists, .debugName = "example.profiledSubmit" }, error) || !queue.WaitIdle(error))
		{
			fw::ReportError("failed to submit", error);
			return false;
		}

		dev.Destroy(texture, {}, error);
		dev.Destroy(buffer, {}, error);
		dev.CollectGarbage(error);
		return true;
	}

} // namespace

int main(int argc, char ** argv)
{
	// Installed first so device creation is instrumented along with the rest.
	rhi::SetProfiler(&g_profiler);

	constexpr rhi::BuildInfo build = rhi::GetBuildInfo();
	if (!build.profilingEnabled)
	{
		// With AZOTH_RHI_ENABLE_PROFILING off every instrumentation point compiles to nothing so the sink is installed and simply never called. The empty tally
		// below is the proof.
		LOG_INFO(fw::Log(), "note: this build has profiling compiled out, so nothing will call the sink");
	}

	const char * requested = fw::RequestedBackend(argc, argv);

	// Every backend this build has, registered, with the requested one first. What that set is was settled when the library was compiled so this sample never
	// has to ask.
	rhi::BackendSelection backends{ rhi::BackendPreference{ .requested = requested } };
	if (requested != nullptr && !backends.HonoredRequest())
	{
		LOG_INFO(fw::Log(), "note: this build has no {} backend, using what it does have", requested);
	}

	const bool ran = RunSomeWork(backends);
	g_profiler.Report();

	rhi::SetProfiler(nullptr);
	return ran ? 0 : 1;
}

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

#include "azoth/rhi/commands/command.hpp"
#include "azoth/rhi/core/enums.hpp"
#include "azoth/rhi/core/result.hpp"
#include "azoth/rhi/device/device.hpp"

#include "conformance/samples.hpp"

namespace azo::rhi::test
{

	class Recording final
	{
	public:
		explicit Recording(Device device, const QueueType queueType = QueueType::eGraphics)
		{
			m_pool = device.CreateCommandPool(samples::CommandPool(queueType), m_error);
			if (!m_pool.IsValid())
			{
				return;
			}

			m_list = m_pool.Allocate("azoth.rhi.test.commandList", m_error);
			if (!m_list.IsValid())
			{
				return;
			}

			m_recording = m_list.Begin(m_error);
		}

		Recording(const Recording &)			 = delete;
		Recording & operator=(const Recording &) = delete;
		Recording(Recording &&)					 = delete;
		Recording & operator=(Recording &&)		 = delete;

		~Recording()
		{
			if (m_recording)
			{
				Error ignored{};
				static_cast<void>(m_list.End(ignored));
			}
		}

		[[nodiscard]] bool IsRecording() const noexcept
		{
			return m_recording;
		}

		[[nodiscard]] Error GetError() const noexcept
		{
			return m_error;
		}

		[[nodiscard]] CommandList & List() noexcept
		{
			return m_list;
		}

		[[nodiscard]] CommandPool & Pool() noexcept
		{
			return m_pool;
		}

		[[nodiscard]] bool End()
		{
			if (!m_recording)
			{
				return false;
			}
			m_recording = false;
			return m_list.End(m_error);
		}

	private:
		CommandPool m_pool;
		CommandList m_list;
		Error m_error{};
		bool m_recording = false;
	};

} // namespace azo::rhi::test

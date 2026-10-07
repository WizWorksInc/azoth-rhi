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

#include <azoth/rhi/device/device.hpp>

#include <cstdint> // NOLINT

namespace fw
{
	class MetalCapture final
	{
	public:
		explicit MetalCapture(azo::rhi::Device device);

		~MetalCapture();

		MetalCapture(const MetalCapture &)			   = delete;
		MetalCapture & operator=(const MetalCapture &) = delete;
		MetalCapture(MetalCapture &&)				   = delete;
		MetalCapture & operator=(MetalCapture &&)	   = delete;

		void FramePresented();

		[[nodiscard]] bool IsCapturing() const noexcept
		{
			return m_capturing;
		}

	private:
		void Stop();

		bool m_capturing		 = false;
		std::uint32_t m_frames	 = 0;
		std::uint32_t m_captured = 0;
	};
} // namespace fw

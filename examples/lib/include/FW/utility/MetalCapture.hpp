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

/**
 * \file
 * \brief Taking an Xcode Metal frame capture from a sample, without Xcode driving it.
 *
 * RenderDoc has no Metal backend, so the tool that answers "what did this frame actually bind" here is Xcode's, and a capture you have to click for is no use
 * against a defect in the first frames.
 *
 * MTLCaptureManager is the programmatic form, needing MTL_CAPTURE_ENABLED=1 before the process starts and a device to capture.
 */

#include <azoth/rhi/device/device.hpp>

#include <cstdint>

namespace fw
{
	/**
	 * \brief Captures the first frames of a run into a .gputrace, when the environment asks for one.
	 *
	 * Reads AZOTH_METAL_CAPTURE for the path and AZOTH_METAL_CAPTURE_FRAMES for how many. Does nothing when either is unset or the device is not Metal backed.
	 *
	 * \code
	 * MTL_CAPTURE_ENABLED=1 AZOTH_METAL_CAPTURE=/tmp/deccer.gputrace ./rhi_deccer_cubes
	 * \endcode
	 */
	class MetalCapture final
	{
	public:
		/**
		 * \brief Begins a capture if the environment asked for one and this device can be captured.
		 */
		explicit MetalCapture(azo::rhi::Device device);

		/**
		 * \brief Ends the capture if one is running, which is what writes the trace.
		 */
		~MetalCapture();

		MetalCapture(const MetalCapture &)			   = delete;
		MetalCapture & operator=(const MetalCapture &) = delete;
		MetalCapture(MetalCapture &&)				   = delete;
		MetalCapture & operator=(MetalCapture &&)	   = delete;

		/**
		 * \brief Call once per presented frame. Ends the capture once the asked-for count has passed.
		 */
		void FramePresented();

		/**
		 * \brief Whether a capture is running right now.
		 */
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

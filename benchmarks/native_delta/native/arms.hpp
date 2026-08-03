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
#include "azoth/rhi/device/device.hpp"

#include "shared/shapes.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>

/*
 * The arm the RHI is compared against, with the graphics headers behind it.
 *
 * One file a backend under native/, which is the one place the API boundary check permits a graphics header. That is how main.cpp records the RHI arm without
 * ever naming a vk::, an MTL:: or an ID3D12. Which of them answers is settled once, against the API the device came up on.
 */
namespace bench::native
{

	// Resolves the arm for the API this device came up on, out of the RHI's native surface and out of objects it makes itself where that surface publishes none.
	// False where this build has no arm for it, which is a run with nothing to compare against.
	[[nodiscard]] bool Prepare(azo::rhi::Device device, const Workload & work);

	void Release();

	/**
	 * \brief Why a shape is not compared on this backend or an empty view when it is.
	 *
	 * A shape whose native call needs an object the RHI resolved out of a handle and does not publish is skipped, since approximating the call is how a benchmark
	 * ends up flattering one arm.
	 */
	[[nodiscard]] std::string_view Gap(Kind kind);

	/**
	 * \brief Whether the backend records nothing at all for this shape, which counts as an answer.
	 *
	 * The delta is then the whole of what the RHI costs and there is no ratio to report it as, there being no denominator.
	 */
	[[nodiscard]] bool RecordsNothing(Kind kind);

	/**
	 * \brief Records the shape natively onto the RHI's own list, leaving the time it took in elapsed. False where the recording was refused.
	 *
	 * Inside a CommandList::ModifyNative scope, which is the only legal way to record natively onto an RHI list. The scope is entered once per timed pass so its
	 * cost sits inside the measurement and is amortized over the whole pass.
	 */
	[[nodiscard]] bool Record(Kind kind, azo::rhi::CommandList & list, const Workload & work, std::size_t commands, std::uint64_t & elapsed);

} // namespace bench::native

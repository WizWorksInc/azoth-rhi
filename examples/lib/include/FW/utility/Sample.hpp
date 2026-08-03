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
 * \brief The few lines every sample's main began with before they were written once here.
 */

#include <azoth/rhi/core/result.hpp>

#include <string_view>

namespace fw
{
	/**
	 * \brief What a sample returns when the backend it was asked for has no driver on this machine.
	 *
	 * ctest is told to read 77 as a skip, so a machine without a driver reports honestly instead of failing. Every sample's device bring-up ends this way,
	 * which is why the number lives here and not in each of them.
	 */
	inline constexpr int kSkipExitCode = 77;

	/**
	 * \brief Logs what failed along with whatever the RHI said about it.
	 */
	void ReportError(std::string_view what, const azo::rhi::Error & error);

	/**
	 * \brief The backend named on the command line, or null for whichever this build prefers.
	 *
	 * Every sample takes the same first argument, so a run across all of them can name one backend.
	 */
	[[nodiscard]] const char * RequestedBackend(int argc, char ** argv);

	/**
	 * \brief Reports a device that could not be created and hands back the code that reads as a skip.
	 *
	 * Returned straight out of main, so the reason logged and the exit code chosen cannot drift apart.
	 */
	[[nodiscard]] int ReportNoDevice(const azo::rhi::Error & error);
} // namespace fw

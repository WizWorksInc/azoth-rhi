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

#include "FW/utility/Sample.hpp"

#include "FW/utility/Log.hpp"

#include <cstddef>
#include <span>

namespace fw
{
	void ReportError(const std::string_view what, const azo::rhi::Error & error)
	{
		LOG_ERROR(Log(), "{}: {} (error code {})", what, error.message != nullptr ? error.message : "no diagnostic", static_cast<unsigned>(error.code));
	}

	const char * RequestedBackend(const int argc, char ** argv)
	{
		const std::span<char * const> args(argv, static_cast<std::size_t>(argc));

		return args.size() > 1 ? args[1] : nullptr; // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
	}

	int ReportNoDevice(const azo::rhi::Error & error)
	{
		ReportError("failed to create a device", error);

		return kSkipExitCode;
	}
} // namespace fw

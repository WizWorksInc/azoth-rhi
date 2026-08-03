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

#include "FW/utility/ExecutablePath.hpp"

#include <azoth/rhi/core/platform.hpp>

#include <array>
#include <vector>

// windows.h alone defines several hundred macros, so the header stays with std::filesystem and the platform query is confined here.
#ifdef AZOTH_RHI_OS_WINDOWS
	#ifndef WIN32_LEAN_AND_MEAN
		#define WIN32_LEAN_AND_MEAN
	#endif
	#ifndef NOMINMAX
		#define NOMINMAX
	#endif
	#include <windows.h>
#elifdef AZOTH_RHI_OS_APPLE
	#include <mach-o/dyld.h>

	#include <climits>
	#include <cstdint>
#elifdef AZOTH_RHI_OS_FREEBSD
	#include <sys/sysctl.h>
	#include <sys/types.h>

	#include <climits>
#elifdef AZOTH_RHI_OS_LINUX
	#include <unistd.h>

	#include <climits>
	#include <cstddef>
#endif

namespace fw::util
{
	std::filesystem::path ExecutablePath()
	{
#ifdef AZOTH_RHI_OS_WINDOWS
		// GetModuleFileNameW truncates and reports the buffer size, not what it needed, so a path longer than the buffer is only detectable by the truncation error.
		// Growing until it stops complaining covers the long paths a Windows 10 opt-in allows.
		std::vector<wchar_t> buffer(MAX_PATH);
		while (true)
		{
			const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
			if (length == 0)
			{
				return std::filesystem::current_path();
			}

			if (length < buffer.size())
			{
				return { buffer.data() };
			}

			buffer.resize(buffer.size() * 2);
		}
#elifdef AZOTH_RHI_OS_APPLE
		// _NSGetExecutablePath rewrites size with the length it needed when the buffer was too small, so the second call is the one that can be trusted.
		std::array<char, PATH_MAX> buffer{};
		auto size = static_cast<std::uint32_t>(buffer.size());
		if (_NSGetExecutablePath(buffer.data(), &size) == 0)
		{
			return { buffer.data() };
		}

		std::vector<char> grown(size);
		if (_NSGetExecutablePath(grown.data(), &size) == 0)
		{
			return { grown.data() };
		}

		return std::filesystem::current_path();
#elifdef AZOTH_RHI_OS_FREEBSD
		std::array<int, 4> mib{ CTL_KERN, KERN_PROC, KERN_PROC_PATHNAME, -1 };
		std::array<char, PATH_MAX> buffer{};
		std::size_t size = buffer.size();
		if (sysctl(mib.data(), static_cast<u_int>(mib.size()), buffer.data(), &size, nullptr, 0) == 0)
		{
			return { buffer.data() };
		}

		return std::filesystem::current_path();
#elifdef AZOTH_RHI_OS_LINUX
		// readlink does not terminate what it writes, so the returned length is what says where the path ends.
		std::array<char, PATH_MAX> buffer{};
		if (const ssize_t length = readlink("/proc/self/exe", buffer.data(), buffer.size() - 1); length != -1)
		{
			buffer.at(static_cast<std::size_t>(length)) = '\0';
			return { buffer.data() };
		}

		return std::filesystem::current_path();
#else
		return std::filesystem::current_path();
#endif
	}

	std::filesystem::path ExecutableDirectory()
	{
		return ExecutablePath().parent_path();
	}
} // namespace fw::util

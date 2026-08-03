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

#include <filesystem>

namespace fw::util
{
	/**
	 * \brief Absolute path of the running executable, as the operating system reports it.
	 *
	 * Asked of the OS, not taken from argv[0], which can be relative, empty, or whatever the caller decided to put there.
	 *
	 * \return The executable's path, or the current working directory when the platform query fails.
	 */
	[[nodiscard]] std::filesystem::path ExecutablePath();

	/**
	 * \brief Directory the running executable sits in.
	 */
	[[nodiscard]] std::filesystem::path ExecutableDirectory();
} // namespace fw::util

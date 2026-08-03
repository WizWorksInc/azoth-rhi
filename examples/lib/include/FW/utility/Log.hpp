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
 * \brief The logger every sample writes through.
 */

// ReSharper disable once CppUnusedIncludeDirective
#include <quill/Logger.h>
#include <quill/LogMacros.h>

namespace fw
{
	/**
	 * \brief The one logger, with the backend started the first time it is asked for.
	 *
	 * A pointer and not a reference because that is what Quill's macros take. Quill owns it and outlives every caller.
	 *
	 * Write through it with Quill's own macros, which split the call site and the format string at compile time where a wrapper would hand the backend one
	 * runtime string:
	 *
	 *     LOG_INFO(fw::Log(), "{} frames presented", frames);
	 */
	[[nodiscard]] quill::Logger * Log();
} // namespace fw

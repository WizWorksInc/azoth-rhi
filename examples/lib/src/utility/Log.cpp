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

#include "FW/utility/Log.hpp"

#include <quill/Backend.h>
#include <quill/Frontend.h>
#include <quill/sinks/ConsoleSink.h>

#include <utility>

namespace fw
{
	quill::Logger * Log()
	{
		/*
		 * Started on the first call and not from main, so a sample logs without a line of setup and so the ordering between this and static initialisation
		 * elsewhere cannot be got wrong. Function-local, which makes the once-only guarantee the language's, not ours.
		 */
		static quill::Logger * const logger = []
		{
			quill::Backend::start();

			// A sample's output is read by a person watching it run, so the time and the level are worth the width and the thread and file are not.
			auto sink = quill::Frontend::create_or_get_sink<quill::ConsoleSink>("console");
			return quill::Frontend::create_or_get_logger(
				"sample", std::move(sink), quill::PatternFormatterOptions{ "%(time) %(log_level:<8) %(message)", "%H:%M:%S" });
		}();

		return logger;
	}
} // namespace fw

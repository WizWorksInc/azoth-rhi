// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
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
		static quill::Logger * const logger = []
		{
			quill::Backend::start();

			auto sink = quill::Frontend::create_or_get_sink<quill::ConsoleSink>("console");
			return quill::Frontend::create_or_get_logger(
				"sample", std::move(sink), quill::PatternFormatterOptions{ "%(time) %(log_level:<8) %(message)", "%H:%M:%S" });
		}();

		return logger;
	}
}

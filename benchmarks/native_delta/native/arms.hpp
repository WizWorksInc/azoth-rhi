// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include "azoth/rhi/commands/command.hpp"
#include "azoth/rhi/device/device.hpp"

#include "shared/shapes.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace bench::native
{

	[[nodiscard]] bool Prepare(azo::rhi::Device device, const Workload & work);

	void Release();

	[[nodiscard]] std::string_view Gap(Kind kind);

	[[nodiscard]] bool RecordsNothing(Kind kind);

	[[nodiscard]] bool Record(Kind kind, azo::rhi::CommandList & list, const Workload & work, std::size_t commands, std::uint64_t & elapsed);

}

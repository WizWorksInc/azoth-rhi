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

#include "azoth/rhi/core/c_string.hpp"
#include "azoth/rhi/core/result.hpp"
#include "azoth/rhi/device/device.hpp"

#include <cstddef>
#include <vector>

namespace azo::rhi::test::oracle
{

	struct OverloadReport final
	{
		CString operation = nullptr;

		bool plainSucceeded = false;

		bool erroredSucceeded = false;

		ErrorCode erroredCode = ErrorCode::eOk;

		bool resultSucceeded = false;

		ErrorCode resultCode = ErrorCode::eOk;
	};

	inline constexpr std::size_t kMinimumOperationsProbed = 29;

	[[nodiscard]] std::vector<OverloadReport> ProbeEveryOverload(Device device);

	void CheckOverloadsAgree(Device device);

} // namespace azo::rhi::test::oracle

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

#include "azoth/rhi/device/device.hpp"

#include <cstdint>
#include <string_view>

#ifndef AZOTH_RHI_TEST_VALIDATION_MODE
	#define AZOTH_RHI_TEST_VALIDATION_MODE eDeveloper
#endif

#ifndef AZOTH_RHI_TEST_CONFIGURATION_NAME
	#define AZOTH_RHI_TEST_CONFIGURATION_NAME "validation-eDeveloper"
#endif

namespace azo::rhi::test
{

	constexpr ValidationMode kValidationMode = ValidationMode::AZOTH_RHI_TEST_VALIDATION_MODE;

	constexpr bool kValidatesFully = kValidationMode == ValidationMode::eDeveloper || kValidationMode == ValidationMode::eCapture;

	constexpr bool kValidatesHandles = kValidationMode != ValidationMode::eOff;

	inline constexpr std::uint64_t kWaitTimeoutNanoseconds = 5'000'000'000ULL;

	[[nodiscard]] std::string_view RequestedBackends();

	[[nodiscard]] std::string_view RequiredBackends();

	[[nodiscard]] std::uint32_t StressScale();

	[[nodiscard]] std::uint32_t ScaledIterations(std::uint32_t baseline);

} // namespace azo::rhi::test

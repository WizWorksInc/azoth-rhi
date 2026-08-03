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

#include "azoth/rhi/core/result.hpp"
#include "azoth/rhi/device/device.hpp"
#include "azoth/rhi/device/selection.hpp"

#include <cstddef>
#include <string_view>

namespace azo::rhi::test::minimal
{

	struct HeadlessApi final : GraphicsApiTagRoot
	{
		static constexpr std::string_view canonicalName = "azoth.rhi.test.minimal";
		static constexpr std::string_view displayName	= "Minimal headless fixture";
		static constexpr GraphicsApiId id				= MakeGraphicsApiId(canonicalName);
	};

	static_assert(GraphicsApiTag<HeadlessApi>);

	struct PresentingApi final : GraphicsApiTagRoot
	{
		static constexpr std::string_view canonicalName = "azoth.rhi.test.minimalPresenting";
		static constexpr std::string_view displayName	= "Minimal presenting fixture";
		static constexpr GraphicsApiId id				= MakeGraphicsApiId(canonicalName);
	};

	static_assert(GraphicsApiTag<PresentingApi>);

	Result<void> RegisterHeadless(GraphicsApiRegistry & registry);
	Result<void> RegisterPresenting(GraphicsApiRegistry & registry);

	[[nodiscard]] std::size_t HeadlessEntryCount() noexcept;

	[[nodiscard]] std::size_t PresentingEntryCount() noexcept;

	[[nodiscard]] std::size_t LiveObjectCount() noexcept;

} // namespace azo::rhi::test::minimal

// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include "azoth/rhi/resources/resources.hpp"

namespace azo::rhi::detail
{
	[[nodiscard]] constexpr TextureDesc Recorded(TextureDesc desc) noexcept
	{
		desc.debugName = nullptr;
		return desc;
	}

	[[nodiscard]] constexpr BufferDesc Recorded(BufferDesc desc) noexcept
	{
		desc.debugName = nullptr;
		return desc;
	}
}

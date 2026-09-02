// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include "azoth/rhi/core/result.hpp"
#include "azoth/rhi/device/device.hpp"

namespace azo::rhi
{

	Result<void> RegisterVulkanBackend(GraphicsApiRegistry & registry);
	Result<void> RegisterD3D12Backend(GraphicsApiRegistry & registry);
	Result<void> RegisterMetalBackend(GraphicsApiRegistry & registry);
	Result<void> RegisterMetal4Backend(GraphicsApiRegistry & registry);
	Result<void> RegisterNullBackend(GraphicsApiRegistry & registry);

}

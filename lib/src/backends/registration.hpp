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

namespace azo::rhi
{

	/*
	 * How each bundled backend brings itself into a registry. Internal because a caller reaches these through the catalog in selection.hpp and not by name,
	 * which is what keeps a public header per backend and its conditional includes off anyone wanting whatever the build has.
	 *
	 * Declared unconditionally, defined only where the backend is built. selection.cpp is the one translation unit that calls them, behind the same conditions
	 * that decide which backend sources compile.
	 */
	Result<void> RegisterVulkanBackend(GraphicsApiRegistry & registry);
	Result<void> RegisterD3D12Backend(GraphicsApiRegistry & registry);
	Result<void> RegisterMetalBackend(GraphicsApiRegistry & registry);
	Result<void> RegisterMetal4Backend(GraphicsApiRegistry & registry);
	Result<void> RegisterNullBackend(GraphicsApiRegistry & registry);

} // namespace azo::rhi

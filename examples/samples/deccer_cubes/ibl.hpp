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

#include "azoth/rhi/commands/sync.hpp"
#include "azoth/rhi/core/resource_handles.hpp"
#include "azoth/rhi/device/device.hpp"

#include "shaders.hpp"

#include <cstdint>
#include <string>

namespace deccer
{
	struct Environment final
	{
		azo::rhi::TextureHandle irradiance{};
		azo::rhi::TextureViewHandle irradianceView{};

		azo::rhi::TextureHandle specular{};
		azo::rhi::TextureViewHandle specularView{};

		// Level zero is the environment untouched, which is what the sky is drawn from.
		std::uint32_t specularMips = 0;
	};

	[[nodiscard]] bool BuildEnvironment(azo::rhi::Device dev, azo::rhi::Queue & queue, azo::rhi::TimelineHandle timeline, std::uint64_t signalValue,
		ShaderCompiler & compiler, Environment & out, std::string & error);

} // namespace deccer

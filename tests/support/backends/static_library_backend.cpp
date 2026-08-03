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

#include "azoth/rhi/device/api_tags.hpp"
#include "azoth/rhi/device/device.hpp"
#include "azoth/rhi/device/selection.hpp"

#include <string_view>

namespace
{

	struct StaticLibraryApi final : azo::rhi::GraphicsApiTagRoot
	{
		static constexpr std::string_view canonicalName = "studio.rhi.staticlib";
		static constexpr std::string_view displayName	= "Studio Static Library";
		static constexpr azo::rhi::GraphicsApiId id		= azo::rhi::MakeGraphicsApiId(canonicalName);
	};

	static_assert(azo::rhi::GraphicsApiTag<StaticLibraryApi>);

	[[nodiscard]] azo::rhi::BackendCreateInfo CreateInfo() noexcept
	{
		azo::rhi::BackendCreateInfo info{};
		info.info.canonicalName = StaticLibraryApi::canonicalName;
		info.info.displayName	= StaticLibraryApi::displayName;
		info.createInstance		= [](const void *, azo::rhi::Error *) noexcept
		{
			return static_cast<void *>(nullptr);
		};

		return info;
	}

	azo::rhi::Result<void> RegisterStaticLibraryBackend(azo::rhi::GraphicsApiRegistry & registry)
	{
		return registry.Register<StaticLibraryApi>(CreateInfo());
	}

} // namespace

AZO_RHI_REGISTER_BACKEND(azo::rhi::MakeBackendEntry<StaticLibraryApi>(&RegisterStaticLibraryBackend));

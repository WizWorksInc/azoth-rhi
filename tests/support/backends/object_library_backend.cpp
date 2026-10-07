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

	struct ObjectLibraryApi final : azo::rhi::GraphicsApiTagRoot
	{
		static constexpr std::string_view kCanonicalName = "studio.rhi.objectlib";
		static constexpr std::string_view kDisplayName	 = "Studio Object Library";
		static constexpr azo::rhi::GraphicsApiId kId	 = azo::rhi::make_graphics_api_id(kCanonicalName);
	};

	static_assert(azo::rhi::GraphicsApiTag<ObjectLibraryApi>);

	[[nodiscard]] azo::rhi::BackendCreateInfo CreateInfo() noexcept
	{
		azo::rhi::BackendCreateInfo info{};
		info.info.canonicalName = ObjectLibraryApi::kCanonicalName;
		info.info.displayName	= ObjectLibraryApi::kDisplayName;
		info.createInstance		= [](const void *, azo::rhi::Error *) noexcept
		{
			return static_cast<void *>(nullptr);
		};

		return info;
	}

	azo::rhi::Result<void> RegisterObjectLibraryBackend(azo::rhi::GraphicsApiRegistry & registry)
	{
		return registry.Register<ObjectLibraryApi>(CreateInfo());
	}

} // namespace

AZO_RHI_REGISTER_BACKEND(azo::rhi::make_backend_entry<ObjectLibraryApi>(&RegisterObjectLibraryBackend));

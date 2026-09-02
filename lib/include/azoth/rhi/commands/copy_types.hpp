// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include "azoth/rhi/commands/sync.hpp"

#include <array>
#include <cstdint>

namespace azo::rhi
{

	struct Offset3D final
	{
		std::int32_t x = 0;
		std::int32_t y = 0;
		std::int32_t z = 0;
	};

	struct Extent3D final
	{
		std::uint32_t width	 = 1;
		std::uint32_t height = 1;
		std::uint32_t depth	 = 1;
	};

	struct BufferTextureCopy final
	{
		std::uint64_t bufferOffset		= 0;
		std::uint32_t bufferRowLength	= 0;
		std::uint32_t bufferImageHeight = 0;
		TextureSubresource subresource{};
		Offset3D textureOffset{};
		Extent3D textureExtent{};
	};

	struct TextureCopy final
	{
		TextureSubresource srcSubresource{};
		Offset3D srcOffset{};
		TextureSubresource dstSubresource{};
		Offset3D dstOffset{};
		Extent3D extent{};
	};

	struct TextureResolve final
	{
		TextureSubresource srcSubresource{};
		Offset3D srcOffset{};
		TextureSubresource dstSubresource{};
		Offset3D dstOffset{};
		Extent3D extent{};
	};

	struct TextureBlit final
	{
		TextureSubresource srcSubresource{};
		std::array<Offset3D, 2> srcOffsets{};
		TextureSubresource dstSubresource{};
		std::array<Offset3D, 2> dstOffsets{};
	};

}

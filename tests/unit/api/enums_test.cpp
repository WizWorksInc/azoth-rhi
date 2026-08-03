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

#include "azoth/rhi/commands/sync.hpp"
#include "azoth/rhi/core/enums.hpp"
#include "azoth/rhi/core/flags.hpp"
#include "azoth/rhi/resources/query.hpp"
#include "azoth/rhi/resources/resources.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <type_traits>

namespace rhi = azo::rhi;

namespace
{

	TEST(SampleCount, EnumeratorsEqualTheirSampleCounts)
	{
		static_assert(static_cast<std::uint8_t>(rhi::SampleCount::e1) == 1);
		static_assert(static_cast<std::uint8_t>(rhi::SampleCount::e2) == 2);
		static_assert(static_cast<std::uint8_t>(rhi::SampleCount::e4) == 4);
		static_assert(static_cast<std::uint8_t>(rhi::SampleCount::e8) == 8);
		static_assert(static_cast<std::uint8_t>(rhi::SampleCount::e16) == 16);

		SUCCEED();
	}

	TEST(Format, UndefinedIsZeroSoADefaultDescNamesNoFormat)
	{
		static_assert(static_cast<std::uint16_t>(rhi::Format::eUndefined) == 0);

		constexpr rhi::TextureDesc texture{};
		static_assert(texture.format == rhi::Format::eUndefined);

		SUCCEED();
	}

	TEST(IsDepthFormat, IdentifiesExactlyTheDepthAndDepthStencilFormats)
	{
		static_assert(rhi::IsDepthFormat(rhi::Format::eD16UNorm));
		static_assert(rhi::IsDepthFormat(rhi::Format::eD24UNormS8UInt));
		static_assert(rhi::IsDepthFormat(rhi::Format::eD32Float));
		static_assert(rhi::IsDepthFormat(rhi::Format::eD32FloatS8UInt));
		static_assert(rhi::IsDepthFormat(rhi::Format::eX8D24UNorm));

		SUCCEED();
	}

	TEST(IsDepthFormat, RejectsColorFormatsIncludingTheOnesThatLookNumericallySimilar)
	{
		static_assert(!rhi::IsDepthFormat(rhi::Format::eUndefined));
		static_assert(!rhi::IsDepthFormat(rhi::Format::eRGBA8UNorm));
		static_assert(!rhi::IsDepthFormat(rhi::Format::eBGRA8Srgb));

		static_assert(!rhi::IsDepthFormat(rhi::Format::eR32Float));
		static_assert(!rhi::IsDepthFormat(rhi::Format::eR16Float));

		static_assert(!rhi::IsDepthFormat(rhi::Format::eBC7UNorm));
		static_assert(!rhi::IsDepthFormat(rhi::Format::eBC6HSFloat));

		SUCCEED();
	}

	TEST(IsDepthFormat, AgreesWithASweepOfEveryEnumeratorInTheRange)
	{
		constexpr std::array depthFormats{
			rhi::Format::eD16UNorm, rhi::Format::eD24UNormS8UInt, rhi::Format::eD32Float, rhi::Format::eD32FloatS8UInt, rhi::Format::eX8D24UNorm
		};

		for (std::uint16_t value = 0; value <= static_cast<std::uint16_t>(rhi::Format::eBC6HSFloat); ++value)
		{
			const auto format = static_cast<rhi::Format>(value);
			const bool listed = std::ranges::find(depthFormats, format) != depthFormats.end();
			EXPECT_EQ(rhi::IsDepthFormat(format), listed) << "format enumerator " << value << " disagrees with the depth format list";
		}
	}

	TEST(PublicEnums, KeepTheUnderlyingTypesTheDescsWereSizedFor)
	{
		// The descs are packed on the assumption that these are small. Widening one silently changes the layout of every struct that holds it, which matters
		// because descs cross the API boundary.
		static_assert(std::is_same_v<std::underlying_type_t<rhi::QueueType>, std::uint8_t>);
		static_assert(std::is_same_v<std::underlying_type_t<rhi::Format>, std::uint16_t>);
		static_assert(std::is_same_v<std::underlying_type_t<rhi::TextureType>, std::uint8_t>);
		static_assert(std::is_same_v<std::underlying_type_t<rhi::SampleCount>, std::uint8_t>);
		static_assert(std::is_same_v<std::underlying_type_t<rhi::TextureLayout>, std::uint8_t>);
		static_assert(std::is_same_v<std::underlying_type_t<rhi::LoadOp>, std::uint8_t>);
		static_assert(std::is_same_v<std::underlying_type_t<rhi::StoreOp>, std::uint8_t>);
		static_assert(std::is_same_v<std::underlying_type_t<rhi::PresentMode>, std::uint8_t>);

		static_assert(std::is_same_v<std::underlying_type_t<rhi::PipelineStage>, std::uint64_t>);
		static_assert(std::is_same_v<std::underlying_type_t<rhi::Access>, std::uint64_t>);

		SUCCEED();
	}

	TEST(FlagEnums, UsePowerOfTwoEnumeratorsSoNoTwoBitsOverlap)
	{
		constexpr std::array bufferUsages{ rhi::BufferUsage::eCopySrc,
			rhi::BufferUsage::eCopyDst,
			rhi::BufferUsage::eVertex,
			rhi::BufferUsage::eIndex,
			rhi::BufferUsage::eUniform,
			rhi::BufferUsage::eStorage,
			rhi::BufferUsage::eIndirect,
			rhi::BufferUsage::eShaderBindingTable,
			rhi::BufferUsage::eAccelerationStructureInput,
			rhi::BufferUsage::eAccelerationStructureStorage,
			rhi::BufferUsage::eSparseBinding };

		std::uint32_t seen = 0;
		for (const rhi::BufferUsage usage : bufferUsages)
		{
			const auto bits = static_cast<std::uint32_t>(usage);
			EXPECT_EQ(bits & (bits - 1u), 0u) << "a buffer usage enumerator is not a single bit";
			EXPECT_EQ(seen & bits, 0u) << "two buffer usage enumerators share a bit";
			seen |= bits;
		}
	}

	TEST(FlagEnums, KeepTextureUsagesDistinctToo)
	{
		constexpr std::array textureUsages{ rhi::TextureUsage::eCopySrc,
			rhi::TextureUsage::eCopyDst,
			rhi::TextureUsage::eSampled,
			rhi::TextureUsage::eStorage,
			rhi::TextureUsage::eColorAttachment,
			rhi::TextureUsage::eDepthStencilAttachment,
			rhi::TextureUsage::eTransientAttachment,
			rhi::TextureUsage::ePresent,
			rhi::TextureUsage::eSparseBinding };

		std::uint32_t seen = 0;
		for (const rhi::TextureUsage usage : textureUsages)
		{
			const auto bits = static_cast<std::uint32_t>(usage);
			EXPECT_EQ(bits & (bits - 1u), 0u) << "a texture usage enumerator is not a single bit";
			EXPECT_EQ(seen & bits, 0u) << "two texture usage enumerators share a bit";
			seen |= bits;
		}
	}

	TEST(FlagEnums, KeepPipelineStagesDistinctAcrossTheFullSixtyFourBitDomain)
	{
		constexpr std::array stages{ rhi::PipelineStage::eDrawIndirect,
			rhi::PipelineStage::eVertexInput,
			rhi::PipelineStage::eVertexShader,
			rhi::PipelineStage::eTessellationControlShader,
			rhi::PipelineStage::eTessellationEvaluationShader,
			rhi::PipelineStage::eGeometryShader,
			rhi::PipelineStage::eFragmentShader,
			rhi::PipelineStage::eEarlyFragmentTests,
			rhi::PipelineStage::eLateFragmentTests,
			rhi::PipelineStage::eColorOutput,
			rhi::PipelineStage::eComputeShader,
			rhi::PipelineStage::eCopy,
			rhi::PipelineStage::eResolve,
			rhi::PipelineStage::eClear,
			rhi::PipelineStage::eHost,
			rhi::PipelineStage::eRayTracingShader,
			rhi::PipelineStage::eAccelerationStructureBuild,
			rhi::PipelineStage::eAllGraphics,
			rhi::PipelineStage::eAllCommands };

		std::uint64_t seen = 0;
		for (const rhi::PipelineStage stage : stages)
		{
			const auto bits = static_cast<std::uint64_t>(stage);
			EXPECT_EQ(bits & (bits - 1u), 0u) << "a pipeline stage enumerator is not a single bit";
			EXPECT_EQ(seen & bits, 0u) << "two pipeline stage enumerators share a bit";
			seen |= bits;
		}

		static_assert(static_cast<std::uint64_t>(rhi::PipelineStage::eNone) == 0, "eNone has to be the empty mask and not a bit");
	}

	TEST(FlagEnums, KeepAccessMasksDistinct)
	{
		constexpr std::array accesses{ rhi::Access::eIndirectRead,
			rhi::Access::eVertexRead,
			rhi::Access::eIndexRead,
			rhi::Access::eConstantRead,
			rhi::Access::eShaderRead,
			rhi::Access::eShaderWrite,
			rhi::Access::eColorRead,
			rhi::Access::eColorWrite,
			rhi::Access::eDepthStencilRead,
			rhi::Access::eDepthStencilWrite,
			rhi::Access::eCopyRead,
			rhi::Access::eCopyWrite,
			rhi::Access::eHostRead,
			rhi::Access::eHostWrite,
			rhi::Access::eMemoryRead,
			rhi::Access::eMemoryWrite,
			rhi::Access::eAccelerationStructureRead,
			rhi::Access::eAccelerationStructureWrite };

		std::uint64_t seen = 0;
		for (const rhi::Access access : accesses)
		{
			const auto bits = static_cast<std::uint64_t>(access);
			EXPECT_EQ(bits & (bits - 1u), 0u) << "an access enumerator is not a single bit";
			EXPECT_EQ(seen & bits, 0u) << "two access enumerators share a bit";
			seen |= bits;
		}

		static_assert(static_cast<std::uint64_t>(rhi::Access::eNone) == 0);
	}

	TEST(FlagEnums, KeepPipelineStatisticsDistinct)
	{
		constexpr std::array statistics{ rhi::PipelineStatistic::eInputAssemblyVertices,
			rhi::PipelineStatistic::eInputAssemblyPrimitives,
			rhi::PipelineStatistic::eVertexShaderInvocations,
			rhi::PipelineStatistic::eClippingInvocations,
			rhi::PipelineStatistic::eClippingPrimitives,
			rhi::PipelineStatistic::eFragmentShaderInvocations,
			rhi::PipelineStatistic::eComputeShaderInvocations };

		std::uint32_t seen = 0;
		for (const rhi::PipelineStatistic statistic : statistics)
		{
			const auto bits = static_cast<std::uint32_t>(statistic);
			EXPECT_EQ(bits & (bits - 1u), 0u);
			EXPECT_EQ(seen & bits, 0u);
			seen |= bits;
		}
	}

	TEST(TextureAspect, KeepsTheThreeAspectsOnSeparateBits)
	{
		static_assert(static_cast<std::uint8_t>(rhi::TextureAspect::eColor) == 0b001);
		static_assert(static_cast<std::uint8_t>(rhi::TextureAspect::eDepth) == 0b010);
		static_assert(static_cast<std::uint8_t>(rhi::TextureAspect::eStencil) == 0b100);

		constexpr rhi::Flags<rhi::TextureAspect> both = rhi::Flags<rhi::TextureAspect>(rhi::TextureAspect::eDepth) | rhi::TextureAspect::eStencil;
		static_assert(both.Contains(rhi::TextureAspect::eDepth));
		static_assert(both.Contains(rhi::TextureAspect::eStencil));
		static_assert(!both.Contains(rhi::TextureAspect::eColor));

		SUCCEED();
	}

	TEST(Sentinels, AreTheMaximumSoTheyNeverCollideWithARealValue)
	{
		static_assert(rhi::kInvalidIndex == 0xffffffffu);
		static_assert(rhi::kIgnoreQueueFamily == 0xffffffffu);
		static_assert(rhi::kAllMips == 0xffffffffu);
		static_assert(rhi::kAllLayers == 0xffffffffu);

		constexpr rhi::QueueFamilyTransfer noTransfer{};
		static_assert(noTransfer.src == rhi::kIgnoreQueueFamily);
		static_assert(noTransfer.dst == rhi::kIgnoreQueueFamily);

		SUCCEED();
	}

} // namespace

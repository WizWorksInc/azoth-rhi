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

#ifdef _WIN32

	#include "backends/d3d12/internal.hpp"

namespace azo::rhi::d3d12
{
	/*
	 * Refuses an export declaration Direct3D 12 cannot honor, at creation.
	 *
	 * CreateSharedHandle takes a resource and a fence and produces an NT handle either way, so what is accepted is that handle under both names it travels by:
	 * the plain Win32 one, and the producer-tagged one Vulkan uses when importing the same handle. A file descriptor or a Metal object is refused because no
	 * Windows API produces one.
	 */
	bool D3D12RefuseUnexportable(const Flags<ExternalHandleType> declared, const Flags<ExternalHandleType> allowed, const char * what, Error * error) noexcept
	{
		const Flags<ExternalHandleType> unsupported = declared & ~allowed;
		return unsupported.Empty() ? true : Fail(error, ErrorCode::eUnsupportedFeature, what);
	}

	// Widens a UTF-16 adapter description into the UTF-8 storage AdapterInfo::name points at.
	[[nodiscard]] detail::HostString NarrowAdapterName(const wchar_t * wide)
	{
		detail::HostString out;
		for (const wchar_t * c = wide; c != nullptr && *c != L'\0'; ++c)
		{
			out.push_back(*c < 0x80 ? static_cast<char>(*c) : '?');
		}
		return out;
	}

	bool Succeed(Error * error) noexcept
	{
		if (error != nullptr)
		{
			*error = {};
		}
		return true;
	}

	bool Fail(Error * error, ErrorCode code, const char * message) noexcept
	{
		if (error != nullptr)
		{
			*error = Error{
				.code	 = code,
				.message = message,
			};
		}
		return false;
	}

	// Names an object for PIX, the debug layer and captures. SetName takes UTF-16 so the name is widened. A no-op when naming is off.
	void NameD3D12Object(ID3D12Object * object, CString name, bool enabled) noexcept
	{
		if (!enabled || object == nullptr || name == nullptr || *name == '\0')
		{
			return;
		}

		const int length = static_cast<int>(std::char_traits<char>::length(name));
		const int needed = MultiByteToWideChar(CP_UTF8, 0, name, length, nullptr, 0);
		std::wstring wide(static_cast<std::size_t>(needed), L'\0');
		MultiByteToWideChar(CP_UTF8, 0, name, length, wide.data(), needed);
		object->SetName(wide.c_str());
	}

	// Maps a D3D feature level onto the backend-neutral major.minor ApiVersion.
	[[nodiscard]] ApiVersion FeatureLevelToApiVersion(D3D_FEATURE_LEVEL level) noexcept
	{
		switch (level)
		{
		case D3D_FEATURE_LEVEL_12_2: return { 12, 2 };
		case D3D_FEATURE_LEVEL_12_1: return { 12, 1 };
		default:					 return { 12, 0 };
		}
	}

	/*
	 * Maps the tiled resources tier onto the portable sparse ladder. Tier 1 binds tiles for buffers and Texture2D without promising what an unmapped one reads,
	 * tier 2 adds the read-zero and discard-writes guarantee, and tier 3 extends that to Texture3D. Tier 4 raises limits the RHI does not report, so it reads
	 * as tier 3 and not as a level of its own.
	 */
	[[nodiscard]] SparseTier SparseTierFromTiledResourcesTier(D3D12_TILED_RESOURCES_TIER tier) noexcept
	{
		if (tier >= D3D12_TILED_RESOURCES_TIER_3)
		{
			return SparseTier::eResidentVolumes;
		}
		if (tier >= D3D12_TILED_RESOURCES_TIER_2)
		{
			return SparseTier::eResidentTextures;
		}
		return tier >= D3D12_TILED_RESOURCES_TIER_1 ? SparseTier::eBuffers : SparseTier::eNone;
	}

	// Maps the conservative rasterization tier across. The three levels line up one for one, which is the reason the RHI carries a tier here at all.
	[[nodiscard]] ConservativeRasterTier ConservativeRasterTierFrom(D3D12_CONSERVATIVE_RASTERIZATION_TIER tier) noexcept
	{
		switch (tier)
		{
		case D3D12_CONSERVATIVE_RASTERIZATION_TIER_3: return ConservativeRasterTier::eInnerCoverage;
		case D3D12_CONSERVATIVE_RASTERIZATION_TIER_2: return ConservativeRasterTier::eDegenerateCulling;
		case D3D12_CONSERVATIVE_RASTERIZATION_TIER_1: return ConservativeRasterTier::eBasic;
		default:									  return ConservativeRasterTier::eNone;
		}
	}

	// Maps the resource binding tier across. Tier 1's fixed-size tables are the floor, tier 2 allows dynamic indexing and tier 3 lifts the array bound.
	[[nodiscard]] BindingTier BindingTierFromResourceBindingTier(D3D12_RESOURCE_BINDING_TIER tier) noexcept
	{
		if (tier >= D3D12_RESOURCE_BINDING_TIER_3)
		{
			return BindingTier::eUnbounded;
		}
		return tier >= D3D12_RESOURCE_BINDING_TIER_2 ? BindingTier::eDynamicIndexing : BindingTier::eBasic;
	}

	// Lowest feature level creation accepts for the requested ApiVersion, {0, 0} meaning the 12_0 floor. The device still gets the highest.
	[[nodiscard]] D3D_FEATURE_LEVEL ApiVersionToFloor(ApiVersion requested) noexcept
	{
		if (requested.major >= 12)
		{
			return requested.minor >= 2 ? D3D_FEATURE_LEVEL_12_2 : (requested.minor >= 1 ? D3D_FEATURE_LEVEL_12_1 : D3D_FEATURE_LEVEL_12_0);
		}
		return D3D_FEATURE_LEVEL_12_0;
	}

	[[nodiscard]] D3D12BackendOwner & Owner()
	{
		static D3D12BackendOwner owner;
		return owner;
	}

	// Takes the buffer registry mutex unless the device was created eSingleThreaded, matching Vulkan. Create and destroy take it, recording never does.
} // namespace azo::rhi::d3d12

#endif // _WIN32

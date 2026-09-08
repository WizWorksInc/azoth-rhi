// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#ifdef _WIN32

	#include "backends/d3d12/internal.hpp"

namespace azo::rhi::d3d12
{
	bool D3D12RefuseUnexportable(const Flags<ExternalHandleType> declared, const Flags<ExternalHandleType> allowed, const char * what, Error * error) noexcept
	{
		const Flags<ExternalHandleType> unsupported = declared & ~allowed;
		return unsupported.Empty() ? true : Fail(error, ErrorCode::eUnsupportedFeature, what);
	}

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

	bool FailNative(Error * error, HRESULT hr, const char * message) noexcept
	{
		if (error != nullptr)
		{
			*error = Error{
				.code		= ErrorCode::eNativeApiError,
				.nativeCode = static_cast<std::int32_t>(hr),
				.message	= message,
			};
		}
		return false;
	}

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

	[[nodiscard]] ApiVersion FeatureLevelToApiVersion(D3D_FEATURE_LEVEL level) noexcept
	{
		switch (level)
		{
		case D3D_FEATURE_LEVEL_12_2: return { 12, 2 };
		case D3D_FEATURE_LEVEL_12_1: return { 12, 1 };
		default:					 return { 12, 0 };
		}
	}

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

	[[nodiscard]] BindingTier BindingTierFromResourceBindingTier(D3D12_RESOURCE_BINDING_TIER tier) noexcept
	{
		if (tier >= D3D12_RESOURCE_BINDING_TIER_3)
		{
			return BindingTier::eUnbounded;
		}
		return tier >= D3D12_RESOURCE_BINDING_TIER_2 ? BindingTier::eDynamicIndexing : BindingTier::eBasic;
	}

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

}

#endif

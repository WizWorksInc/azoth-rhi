// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include "azoth/rhi/device/selection.hpp"
#include "azoth/rhi/module/abi_stamp.hpp"

#include <cstddef>

namespace azo::rhi
{

	struct ModuleDescription final
	{
		AbiStamp stamp{};

		const BackendEntry * entries = nullptr;
		std::size_t entryCount		 = 0;

		std::size_t (*liveObjectCount)() = nullptr;
	};

	using ModuleEntryPoint = bool (*)(ModuleDescription * description);

	inline constexpr const char * kModuleEntryPointName = "azoRhiDescribeModule";

}

// Exported symbol attributes and fixed C-linkage names require macros. NOLINTBEGIN(cppcoreguidelines-macro-usage)
#ifdef _WIN32
	#define AZO_RHI_MODULE_EXPORT __declspec(dllexport)
#else
	#define AZO_RHI_MODULE_EXPORT __attribute__((visibility("default")))
#endif

#define AZO_RHI_DEFINE_MODULE(fillDescription)                                                                                                                 \
	extern "C" AZO_RHI_MODULE_EXPORT bool azoRhiDescribeModule(::azo::rhi::ModuleDescription * description)                                                    \
	{                                                                                                                                                          \
		if (description == nullptr)                                                                                                                            \
		{                                                                                                                                                      \
			return false;                                                                                                                                      \
		}                                                                                                                                                      \
                                                                                                                                                               \
		description->stamp = ::azo::rhi::CurrentAbiStamp();                                                                                                    \
		return (fillDescription)(*description);                                                                                                                \
	}
// NOLINTEND(cppcoreguidelines-macro-usage)

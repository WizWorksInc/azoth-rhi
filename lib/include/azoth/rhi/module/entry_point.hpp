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

/**
 * \file
 * \brief Loadable backend module entry-point contract.
 */

#include "azoth/rhi/device/selection.hpp"
#include "azoth/rhi/module/abi_stamp.hpp"

#include <cstddef>

namespace azo::rhi
{

	/**
	 * \brief Description a loadable backend module returns to the host.
	 *
	 * Backend entries and names are borrowed only for the entry-point call. The host copies what it needs so the module can later be unloaded safely.
	 */
	struct ModuleDescription final
	{
		/**
		 * \brief ABI stamp built by the module.
		 *
		 * The host reads this before trusting any other module surface.
		 */
		AbiStamp stamp{};

		/**
		 * \brief Backend entries exported by the module.
		 *
		 * Borrowed for the duration of the entry-point call and copied by the host.
		 */
		const BackendEntry * entries = nullptr;
		std::size_t entryCount		 = 0;

		/**
		 * \brief Returns how many objects created through this module are still alive.
		 *
		 * The host refuses unload while this reports a nonzero count.
		 */
		std::size_t (*liveObjectCount)() = nullptr;
	};

	/**
	 * \brief Function type of the loadable backend entry point.
	 *
	 * The symbol uses C linkage for lookup, but the payload is C++ and is guarded by AbiStamp.
	 */
	using ModuleEntryPoint = bool (*)(ModuleDescription * description);

	/**
	 * \brief Fixed exported symbol name a host looks up in a loadable backend module.
	 */
	inline constexpr const char * kModuleEntryPointName = "azoRhiDescribeModule";

} // namespace azo::rhi

/**
 * \def AZO_RHI_MODULE_EXPORT
 * \brief Export attribute used by AZO_RHI_DEFINE_MODULE.
 */

/**
 * \def AZO_RHI_DEFINE_MODULE
 * \brief Defines the fixed loadable-backend entry point with the current ABI stamp.
 *
 * \param fillDescription Function called after the stamp is written. It receives ModuleDescription& and returns bool.
 */

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

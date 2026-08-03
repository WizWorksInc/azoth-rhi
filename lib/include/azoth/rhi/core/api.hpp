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
 * \brief The attribute that places a declaration in the exported set of a shared build.
 */

/**
 * \def AZO_RHI_API
 * \brief Marks a declaration as part of the surface a shared build exports.
 *
 * Goes on what is declared in a public header and defined in a source file here. Anything inline, template or constexpr needs none of it.
 *
 * Both switches come from the build. AZOTH_RHI_SHARED says the library was compiled as a shared one and reaches a consumer through the exported target.
 * AZOTH_RHI_BUILDING is private and tells the two Windows spellings apart.
 */

// An export attribute has no spelling that is not a macro. NOLINTBEGIN(cppcoreguidelines-macro-usage)
#ifndef AZOTH_RHI_SHARED
	#define AZO_RHI_API
#elifdef _WIN32
	#ifdef AZOTH_RHI_BUILDING
		#define AZO_RHI_API __declspec(dllexport)
	#else
		#define AZO_RHI_API __declspec(dllimport)
	#endif
#else
	// One spelling for both sides, unlike Windows above. ELF and Mach-O resolve an import by name at load time, so consuming needs no counterpart to dllimport and
	// this only has to undo the hidden preset the library itself is compiled under.
	#define AZO_RHI_API __attribute__((visibility("default")))
#endif
// NOLINTEND(cppcoreguidelines-macro-usage)

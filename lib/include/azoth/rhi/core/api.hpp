// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

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
	#define AZO_RHI_API __attribute__((visibility("default")))
#endif
// NOLINTEND(cppcoreguidelines-macro-usage)

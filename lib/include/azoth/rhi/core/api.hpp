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

#include "azoth/base/attr/export.hpp"
#include "azoth/base/attr/import.hpp"

// An export attribute has no spelling that is not a macro. NOLINTBEGIN(cppcoreguidelines-macro-usage)
#ifndef AZOTH_RHI_SHARED
	#define AZO_RHI_API
#elifdef AZOTH_RHI_BUILDING
	#define AZO_RHI_API AZO_ATTR_EXPORT
#else
	#define AZO_RHI_API AZO_ATTR_IMPORT
#endif
// NOLINTEND(cppcoreguidelines-macro-usage)

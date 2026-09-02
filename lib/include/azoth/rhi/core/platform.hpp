// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#if defined(__x86_64__) || defined(_M_X64)
	#define AZOTH_RHI_CPU_X64
#elif defined(__i386) || defined(_M_IX86) || defined(__arm__)
	#error 32-bit platforms are not supported.
#elif defined(__aarch64__) || defined(_M_ARM64)
	#define AZOTH_RHI_CPU_ARM
#else
	#error Unknown CPU architecture
#endif

#if defined(_WIN32) || defined(_WIN64)
	#define AZOTH_RHI_OS_WINDOWS
#elifdef __APPLE__
	#define AZOTH_RHI_OS_APPLE
	#define AZOTH_RHI_OS_POSIX
	#include "TargetConditionals.h"

	#if TARGET_OS_IPHONE
		#define AZOTH_RHI_OS_IOS
	#elif TARGET_OS_MAC
		#define AZOTH_RHI_OS_MAC
	#else
		#error Unknown Apple platform
	#endif
#elifdef __ANDROID__
	#define AZOTH_RHI_OS_ANDROID
	#define AZOTH_RHI_OS_LINUX
	#define AZOTH_RHI_OS_POSIX
#elif defined(__linux__)
	#define AZOTH_RHI_OS_LINUX
	#define AZOTH_RHI_OS_POSIX
#elif defined(__FreeBSD__)
	#define AZOTH_RHI_OS_FREEBSD
	#define AZOTH_RHI_OS_POSIX
#else
	#error Unknown operating system
#endif

#ifdef _MSC_VER
	#define AZOTH_RHI_COMPILER_MSVC
#elifdef __clang__
	#define AZOTH_RHI_COMPILER_CLANG
#elifdef __GNUC__
	#define AZOTH_RHI_COMPILER_GCC
#else
	#error Unknown compiler
#endif

#ifdef AZOTH_RHI_COMPILER_MSVC
	#define AZO_RHI_DEBUG_BREAK() __debugbreak()
#elif defined(__has_builtin) && __has_builtin(__builtin_debugtrap)
	#define AZO_RHI_DEBUG_BREAK() __builtin_debugtrap()
#else
	#define AZO_RHI_DEBUG_BREAK() ((void)0)
#endif

#ifdef AZOTH_RHI_NO_FORCE_INLINE
	#define AZO_RHI_FORCE_INLINE inline
#elifdef AZOTH_RHI_COMPILER_MSVC
	#define AZO_RHI_FORCE_INLINE __forceinline
#elif defined(AZOTH_RHI_COMPILER_CLANG) || defined(AZOTH_RHI_COMPILER_GCC)
	#define AZO_RHI_FORCE_INLINE __attribute__((always_inline)) inline
#else
	#define AZO_RHI_FORCE_INLINE inline
#endif

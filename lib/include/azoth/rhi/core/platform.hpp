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
 * \brief Compile-time platform, compiler, and low-level utility macros.
 */

// CPU architecture.
#if defined(__x86_64__) || defined(_M_X64)
	#define AZOTH_RHI_CPU_X64
#elif defined(__i386) || defined(_M_IX86) || defined(__arm__)
	#error 32-bit platforms are not supported.
#elif defined(__aarch64__) || defined(_M_ARM64)
	#define AZOTH_RHI_CPU_ARM
#else
	#error Unknown CPU architecture
#endif

// Operating system. AZOTH_RHI_OS_POSIX is additive and may be defined with another OS macro.
#if defined(_WIN32) || defined(_WIN64)
	#define AZOTH_RHI_OS_WINDOWS
#elifdef __APPLE__
	#define AZOTH_RHI_OS_APPLE
	#define AZOTH_RHI_OS_POSIX
	#include "TargetConditionals.h"

	// TargetConditionals defines these macros on every Apple platform, so test their values instead of definedness. Check the iPhone family first because
	// TARGET_OS_MAC means any Apple OS, not macOS specifically.
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

// Compiler. Check MSVC before Clang because clang-cl also defines MSVC compatibility macros.
#ifdef _MSC_VER
	#define AZOTH_RHI_COMPILER_MSVC
#elifdef __clang__
	#define AZOTH_RHI_COMPILER_CLANG
#elifdef __GNUC__
	#define AZOTH_RHI_COMPILER_GCC
#else
	#error Unknown compiler
#endif

/**
 * \def AZO_RHI_DEBUG_BREAK
 * \brief Traps into an attached debugger for validation break settings.
 *
 * This must be resumable so execution can continue after inspecting the failed validation check. GCC has no resumable debug trap that works everywhere, so
 * the fallback is a no-op without terminating the process.
 */
#ifdef AZOTH_RHI_COMPILER_MSVC
	#define AZO_RHI_DEBUG_BREAK() __debugbreak()
#elif defined(__has_builtin) && __has_builtin(__builtin_debugtrap)
	#define AZO_RHI_DEBUG_BREAK() __builtin_debugtrap()
#else
	#define AZO_RHI_DEBUG_BREAK() ((void)0)
#endif

/**
 * \def AZO_RHI_FORCE_INLINE
 * \brief Requests forced inlining for small dispatch helpers.
 *
 * Define AZOTH_RHI_NO_FORCE_INLINE before including this header to make the macro expand to plain inline.
 */
#ifdef AZOTH_RHI_NO_FORCE_INLINE
	#define AZO_RHI_FORCE_INLINE inline
#elifdef AZOTH_RHI_COMPILER_MSVC
	#define AZO_RHI_FORCE_INLINE __forceinline
#elif defined(AZOTH_RHI_COMPILER_CLANG) || defined(AZOTH_RHI_COMPILER_GCC)
	#define AZO_RHI_FORCE_INLINE __attribute__((always_inline)) inline
#else
	#define AZO_RHI_FORCE_INLINE inline
#endif

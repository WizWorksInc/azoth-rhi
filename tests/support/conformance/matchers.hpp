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

#include "azoth/rhi/core/handle.hpp"
#include "azoth/rhi/core/result.hpp"
#include "azoth/rhi/device/device.hpp"

#include <gtest/gtest.h>

#include <ostream>
#include <string_view>

namespace azo::rhi::test
{

	[[nodiscard]] constexpr std::string_view ErrorCodeName(const ErrorCode code) noexcept
	{
		switch (code)
		{
		case ErrorCode::eOk:						return "eOk";
		case ErrorCode::eUnknown:					return "eUnknown";
		case ErrorCode::eUnsupportedApi:			return "eUnsupportedApi";
		case ErrorCode::eUnsupportedFeature:		return "eUnsupportedFeature";
		case ErrorCode::eUnsupportedFormat:			return "eUnsupportedFormat";
		case ErrorCode::eInvalidArgument:			return "eInvalidArgument";
		case ErrorCode::eInvalidHandle:				return "eInvalidHandle";
		case ErrorCode::eOutOfMemory:				return "eOutOfMemory";
		case ErrorCode::eOutOfHostMemory:			return "eOutOfHostMemory";
		case ErrorCode::eOutOfDeviceMemory:			return "eOutOfDeviceMemory";
		case ErrorCode::eMemoryBudgetExceeded:		return "eMemoryBudgetExceeded";
		case ErrorCode::eResidencyFailed:			return "eResidencyFailed";
		case ErrorCode::eDeviceLost:				return "eDeviceLost";
		case ErrorCode::eSurfaceLost:				return "eSurfaceLost";
		case ErrorCode::eSwapchainOutOfDate:		return "eSwapchainOutOfDate";
		case ErrorCode::eTimeout:					return "eTimeout";
		case ErrorCode::eInvalidState:				return "eInvalidState";
		case ErrorCode::eValidationFailed:			return "eValidationFailed";
		case ErrorCode::ePipelineCacheIncompatible: return "ePipelineCacheIncompatible";
		case ErrorCode::eNativeApiError:			return "eNativeApiError";
		case ErrorCode::eIncompatibleAbi:			return "eIncompatibleAbi";
		}
		return "<unnamed ErrorCode>";
	}

	[[nodiscard]] inline std::string Describe(const Error & error)
	{
		std::string text{ ErrorCodeName(error.code) };
		text += " (";
		text += error.message != nullptr ? error.message : "no diagnostic";
		text += ')';
		return text;
	}

	template <class T>
	[[nodiscard]] ::testing::AssertionResult Ok(const Result<T> & result)
	{
		if (result.HasValue())
		{
			return ::testing::AssertionSuccess();
		}
		return ::testing::AssertionFailure() << "expected success, got " << Describe(result.GetError());
	}

	template <class T>
	[[nodiscard]] ::testing::AssertionResult Failed(const Result<T> & result, const ErrorCode expected)
	{
		if (result.HasValue())
		{
			return ::testing::AssertionFailure() << "expected " << ErrorCodeName(expected) << ", got success";
		}
		if (result.GetError().code != expected)
		{
			return ::testing::AssertionFailure() << "expected " << ErrorCodeName(expected) << ", got " << Describe(result.GetError());
		}
		return ::testing::AssertionSuccess();
	}

	[[nodiscard]] inline ::testing::AssertionResult Ok(const bool succeeded, const Error & error)
	{
		if (succeeded)
		{
			return ::testing::AssertionSuccess();
		}
		return ::testing::AssertionFailure() << "expected success, got " << Describe(error);
	}

	[[nodiscard]] inline ::testing::AssertionResult Failed(const bool succeeded, const Error & error, const ErrorCode expected)
	{
		if (succeeded)
		{
			return ::testing::AssertionFailure() << "expected " << ErrorCodeName(expected) << ", the call reported success";
		}
		if (error.code != expected)
		{
			return ::testing::AssertionFailure() << "expected " << ErrorCodeName(expected) << ", got " << Describe(error);
		}
		return ::testing::AssertionSuccess();
	}

	[[nodiscard]] inline ::testing::AssertionResult ErrorIsPopulated(const Error & error)
	{
		if (error.code == ErrorCode::eOk)
		{
			return ::testing::AssertionFailure() << "a failed call left ErrorCode::eOk behind";
		}
		if (error.message == nullptr)
		{
			return ::testing::AssertionFailure() << "a failed call reported " << ErrorCodeName(error.code) << " with no diagnostic message";
		}
		return ::testing::AssertionSuccess();
	}

	template <class T>
	[[nodiscard]] ::testing::AssertionResult IsResetOnFailure(const T & value, const T & fresh)
	{
		if (value == fresh)
		{
			return ::testing::AssertionSuccess();
		}
		return ::testing::AssertionFailure() << "a failed call left its output modified";
	}

} // namespace azo::rhi::test

namespace azo::rhi
{

	// Found by argument-dependent lookup, which is the only place GoogleTest looks.

	inline void PrintTo(const ErrorCode code, std::ostream * out)
	{
		*out << test::ErrorCodeName(code);
	}

	inline void PrintTo(const Error & error, std::ostream * out)
	{
		*out << test::Describe(error);
	}

	inline void PrintTo(const GraphicsApiId id, std::ostream * out)
	{
		*out << "GraphicsApiId{0x" << std::hex << id.value << std::dec << '}';
	}

	template <class Tag>
	void PrintTo(const Handle<Tag> handle, std::ostream * out)
	{
		if (!handle.IsValid())
		{
			*out << "Handle{invalid}";
			return;
		}
		*out << "Handle{index=" << handle.index << ", generation=" << handle.generation << '}';
	}

} // namespace azo::rhi

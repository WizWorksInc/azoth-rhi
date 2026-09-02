// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <cstdint>
#include <utility>

namespace azo::rhi
{

	enum class ErrorCode : std::uint16_t // NOLINT(performance-enum-size)
	{
		eOk,
		eUnknown,
		eUnsupportedApi,
		eUnsupportedFeature,
		eUnsupportedFormat,
		eInvalidArgument,
		eInvalidHandle,
		eOutOfMemory,
		eOutOfHostMemory,
		eOutOfDeviceMemory,
		eMemoryBudgetExceeded,

		eResidencyFailed,

		eDeviceLost,

		eSurfaceLost,

		eSwapchainOutOfDate,

		eTimeout,
		eInvalidState,

		eValidationFailed,

		ePipelineCacheIncompatible,

		eNativeApiError,

		eIncompatibleAbi,
	};

	struct Error final
	{
		ErrorCode code		 = ErrorCode::eOk;
		const char * message = nullptr;
	};

	template <class T>
	class Result final
	{
	public:
		// Intentionally implicit so a fallible function can return a value directly. NOLINTNEXTLINE(hicpp-explicit-conversions)
		constexpr Result(T value) noexcept : m_value(std::move(value)), m_hasValue(true) {}

		// Intentionally implicit so a fallible function can return Error directly. NOLINTNEXTLINE(hicpp-explicit-conversions)
		constexpr Result(Error error) noexcept : m_error(error) {}

		[[nodiscard]] constexpr bool HasValue() const noexcept
		{
			return m_hasValue;
		}

		[[nodiscard]] constexpr explicit operator bool() const noexcept
		{
			return m_hasValue;
		}

		[[nodiscard]] constexpr T & Value() & noexcept
		{
			return m_value;
		}

		[[nodiscard]] constexpr const T & Value() const & noexcept
		{
			return m_value;
		}

		[[nodiscard]] constexpr T && Value() && noexcept
		{
			return std::move(m_value);
		}

		[[nodiscard]] constexpr Error GetError() const noexcept
		{
			return m_error;
		}

	private:
		T m_value{};
		Error m_error{};
		bool m_hasValue = false;
	};

	template <>
	class Result<void> final
	{
	public:
		constexpr Result() noexcept = default;

		// Intentionally implicit so a fallible function can return Error directly. NOLINTNEXTLINE(hicpp-explicit-conversions)
		constexpr Result(Error error) noexcept : m_error(error), m_hasValue(false) {}

		[[nodiscard]] constexpr bool HasValue() const noexcept
		{
			return m_hasValue;
		}

		[[nodiscard]] constexpr explicit operator bool() const noexcept
		{
			return m_hasValue;
		}

		[[nodiscard]] constexpr Error GetError() const noexcept
		{
			return m_error;
		}

	private:
		Error m_error{};
		bool m_hasValue = true;
	};

}

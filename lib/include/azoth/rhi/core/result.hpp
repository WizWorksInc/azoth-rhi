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
 * \brief Public error codes and lightweight result objects.
 */

// ReSharper disable once CppUnusedIncludeDirective
#include <cstdint>
#include <utility>

namespace azo::rhi
{

	/**
	 * \brief Error code returned by failable RHI operations.
	 *
	 * Exceptions do not cross the public API boundary.
	 */
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

		/**
		 * \brief Resource residency or memory commitment failed.
		 *
		 * This can happen after a sparse or reserved resource object has already been created.
		 */
		eResidencyFailed,

		/**
		 * \brief The native device entered a lost state.
		 *
		 * Most backend objects should be treated as unusable until the device is torn down and recreated.
		 */
		eDeviceLost,

		/**
		 * \brief The presentation surface is no longer usable.
		 */
		eSurfaceLost,

		/**
		 * \brief The swapchain no longer matches its surface.
		 *
		 * Recreate the swapchain before acquiring or presenting more images.
		 */
		eSwapchainOutOfDate,

		eTimeout,
		eInvalidState,

		/**
		 * \brief Azoth validation rejected the operation.
		 */
		eValidationFailed,

		/**
		 * \brief Cache data came from another driver, adapter, backend, or incompatible RHI build.
		 */
		ePipelineCacheIncompatible,

		/**
		 * \brief Backend-specific failure without a better portable mapping.
		 */
		eNativeApiError,

		/**
		 * \brief A loadable backend was built against an incompatible ABI layout.
		 *
		 * The host rejects the backend before calling into it so a mismatched module cannot misread interface blocks and corrupt memory.
		 */
		eIncompatibleAbi,
	};

	/**
	 * \brief Portable error payload returned by failable operations.
	 *
	 * message is optional borrowed diagnostic text. Callers must not assume it has dynamic lifetime unless the producing API says so.
	 */
	struct Error final
	{
		ErrorCode code		 = ErrorCode::eOk;
		const char * message = nullptr;
	};

	/**
	 * \brief Value-or-error result used by facade wrappers, setup code, tools, and tests.
	 *
	 * Backend dispatch and hot recording paths use bool with output parameters instead.
	 * \attention Value access is unchecked. Call HasValue or test the result before calling Value.
	 */
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

		/**
		 * \brief Returns the stored value without checking whether one is present.
		 */
		[[nodiscard]] constexpr T & Value() & noexcept
		{
			return m_value;
		}

		/**
		 * \brief Returns the stored value without checking whether one is present.
		 */
		[[nodiscard]] constexpr const T & Value() const & noexcept
		{
			return m_value;
		}

		/**
		 * \brief Moves the stored value out of an rvalue result without checking whether one is present.
		 */
		[[nodiscard]] constexpr T && Value() && noexcept
		{
			return std::move(m_value);
		}

		/**
		 * \brief Returns the stored error.
		 *
		 * \note Meaningful only when HasValue returns false.
		 */
		[[nodiscard]] constexpr Error GetError() const noexcept
		{
			return m_error;
		}

	private:
		T m_value{};
		Error m_error{};
		bool m_hasValue = false;
	};

	/**
	 * \brief Result object for fallible operations without a value.
	 *
	 * Useful for setup, tooling, tests, and higher-level systems.
	 */
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

		/**
		 * \brief Returns the stored error.
		 *
		 * \note Meaningful only when HasValue returns false.
		 */
		[[nodiscard]] constexpr Error GetError() const noexcept
		{
			return m_error;
		}

	private:
		Error m_error{};
		bool m_hasValue = true;
	};

} // namespace azo::rhi

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

#include "azoth/rhi/core/c_string.hpp"
#include "azoth/rhi/core/result.hpp"
#include "azoth/rhi/device/device.hpp"

#include "conformance/backend_contract.hpp"
#include "harness/environment.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <ostream>
#include <span>
#include <string>

namespace azo::rhi::test
{

	// One backend the suite can be pointed at. Trivially copyable on purpose, since GoogleTest stores parameters by value.
	struct Backend final
	{
		GraphicsApiId id{};

		// The globally namespaced name, for example azoth.rhi.vulkan.
		CString canonicalName = nullptr;

		// The last component, for example vulkan. Used in ctest case names and in AZOTH_RHI_TEST_BACKENDS.
		CString shortName = nullptr;

		CString displayName = nullptr;

		Result<void> (*RegisterInto)(GraphicsApiRegistry &) = nullptr;

		operator oracle::BackendIdentity() const noexcept
		{
			return { id, displayName };
		}
	};

	/*
	 * GoogleTest records PrintToString of the parameter alongside each test, and CMake's gtest discovery substitutes that string for whatever follows the last
	 * slash in the test name. Without this the ctest name of every parameterized case is a byte dump of this struct, carrying the pointers it was holding when
	 * discovery ran, so ctest -R cannot select a backend and the names change from one configure to the next.
	 */
	inline void PrintTo(const Backend & backend, std::ostream * out)
	{
		*out << (backend.shortName != nullptr ? backend.shortName : "unknown");
	}

	[[nodiscard]] std::span<const Backend> AvailableBackends() noexcept;

	[[nodiscard]] std::span<const Backend> SelectedBackends();

	[[nodiscard]] std::string ValidateBackendSelection();

	[[nodiscard]] bool BackendIsRequired(std::string_view shortName);

	[[nodiscard]] const Backend * FindBackend(GraphicsApiId id) noexcept;

	[[nodiscard]] std::string BackendParamName(const ::testing::TestParamInfo<Backend> & info);

	[[nodiscard]] DeviceDesc DefaultDeviceDesc() noexcept;

	class DeviceHarness final
	{
	public:
		DeviceHarness(const Backend & backend, const DeviceDesc & desc);

		explicit DeviceHarness(const Backend & backend) : DeviceHarness(backend, DefaultDeviceDesc()) {}

		DeviceHarness(const DeviceHarness &)			 = delete;
		DeviceHarness & operator=(const DeviceHarness &) = delete;
		DeviceHarness(DeviceHarness &&)					 = delete;
		DeviceHarness & operator=(DeviceHarness &&)		 = delete;
		~DeviceHarness()								 = default;

		[[nodiscard]] bool IsValid() const noexcept
		{
			return m_device.IsValid();
		}

		[[nodiscard]] Error GetError() const noexcept
		{
			return m_error;
		}

		[[nodiscard]] Device Get() const noexcept
		{
			return m_device.Get();
		}

		[[nodiscard]] const Backend & GetBackend() const noexcept
		{
			return m_backend;
		}

		[[nodiscard]] GraphicsApiRegistry & Registry() noexcept
		{
			return m_registry;
		}

	private:
		Backend m_backend{};
		GraphicsApiRegistry m_registry;
		UniqueDevice m_device;
		Error m_error{};
	};

	struct BackendGaps final
	{
	};

	[[nodiscard]] BackendGaps GapsFor(GraphicsApiId id) noexcept;

	class BackendTest : public ::testing::TestWithParam<Backend>
	{
	protected:
		void SetUp() override;

		[[nodiscard]] virtual DeviceDesc MakeDeviceDesc() const
		{
			return DefaultDeviceDesc();
		}

		[[nodiscard]] Device Dev() const noexcept
		{
			return m_harness->Get();
		}

		[[nodiscard]] const DeviceCaps & Caps() const noexcept
		{
			return m_harness->Get().GetCaps();
		}

		[[nodiscard]] const Backend & CurrentBackend() const noexcept
		{
			return GetParam();
		}

		[[nodiscard]] bool IsNullBackend() const noexcept
		{
			return GetParam().id == NullApi::id;
		}

		[[nodiscard]] BackendGaps Gaps() const noexcept
		{
			return GapsFor(GetParam().id);
		}

		[[nodiscard]] DeviceHarness & Harness() const noexcept
		{
			return *m_harness;
		}

	private:
		std::unique_ptr<DeviceHarness> m_harness;
	};

} // namespace azo::rhi::test

#define AZO_RHI_BACKEND_SUITE(suite)                                                                                                                           \
	INSTANTIATE_TEST_SUITE_P(Backends,                                                                                                                         \
		suite,                                                                                                                                                 \
		::testing::ValuesIn(::azo::rhi::test::SelectedBackends().begin(), ::azo::rhi::test::SelectedBackends().end()),                                         \
		::azo::rhi::test::BackendParamName)

#define AZO_RHI_REQUIRE_CAP(supported, capability)                                                                                                             \
	do                                                                                                                                                         \
	{                                                                                                                                                          \
		if (!(supported))                                                                                                                                      \
		{                                                                                                                                                      \
			GTEST_SKIP() << CurrentBackend().displayName << " does not support " << (capability);                                                              \
		}                                                                                                                                                      \
	} while (false)

#define AZO_RHI_EXPECT_NO_VALIDATION_ERRORS(device, what)                                                                                                      \
	do                                                                                                                                                         \
	{                                                                                                                                                          \
		if ((device).GetCaps().reportsValidationMessageCounts)                                                                                                 \
		{                                                                                                                                                      \
			EXPECT_EQ((device).GetValidationMessageCounts().errors, 0u) << what;                                                                               \
		}                                                                                                                                                      \
	} while (false)

#define AZO_RHI_REQUIRE_NO_GAP(gapField, description)                                                                                                          \
	do                                                                                                                                                         \
	{                                                                                                                                                          \
		if (!Gaps().gapField)                                                                                                                                  \
		{                                                                                                                                                      \
			GTEST_SKIP() << CurrentBackend().displayName << " does not yet " << (description);                                                                 \
		}                                                                                                                                                      \
	} while (false)

#define AZO_RHI_REQUIRE_HANDLE_VALIDATION()                                                                                                                    \
	do                                                                                                                                                         \
	{                                                                                                                                                          \
		if (!::azo::rhi::test::kValidatesHandles)                                                                                                              \
		{                                                                                                                                                      \
			GTEST_SKIP() << "handle liveness is not tracked under " << AZOTH_RHI_TEST_CONFIGURATION_NAME;                                                      \
		}                                                                                                                                                      \
	} while (false)

#define AZO_RHI_REQUIRE_FULL_VALIDATION()                                                                                                                      \
	do                                                                                                                                                         \
	{                                                                                                                                                          \
		if (!::azo::rhi::test::kValidatesFully)                                                                                                                \
		{                                                                                                                                                      \
			GTEST_SKIP() << "resource state is not tracked under " << AZOTH_RHI_TEST_CONFIGURATION_NAME;                                                       \
		}                                                                                                                                                      \
	} while (false)

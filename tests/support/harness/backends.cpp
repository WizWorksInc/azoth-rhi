// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#include "harness/backends.hpp"

#include "azoth/rhi/device/api_tags.hpp"
#include "azoth/rhi/device/selection.hpp"

#include "harness/environment.hpp"

#include <algorithm>
#include <array>
#include <deque>
#include <string>
#include <string_view>
#include <vector>

namespace azo::rhi::test
{

	std::string BackendParamName(const ::testing::TestParamInfo<Backend> & info)
	{
		return info.param.shortName;
	}

	BackendGaps GapsFor([[maybe_unused]] const GraphicsApiId id) noexcept
	{
		return BackendGaps{};
	}

	DeviceHarness::DeviceHarness(const Backend & backend, const DeviceDesc & desc) : m_backend(backend)
	{
		if (backend.RegisterInto == nullptr)
		{
			m_error = Error{
				.code	 = ErrorCode::eUnsupportedApi,
				.message = "backend has no registration entry point",
			};
			return;
		}

		if (const Result<void> registered = backend.RegisterInto(m_registry); !registered)
		{
			m_error = registered.GetError();
			return;
		}

		const std::array preferred{ backend.id };
		Result<UniqueDevice> device = CreateDevice(m_registry, preferred, desc);
		if (!device)
		{
			m_error = device.GetError();
			return;
		}

		m_device = std::move(device).Value();
	}

	void BackendTest::SetUp()
	{
		if (!BackendIsSelected(GetParam().shortName))
		{
			GTEST_SKIP() << GetParam().displayName << " is not among the backends AZOTH_RHI_TEST_BACKENDS asked for";
		}

		ClearValidationMessageLog();

		m_harness = std::make_unique<DeviceHarness>(GetParam(), MakeDeviceDesc());
		if (m_harness->IsValid())
		{
			return;
		}

		const Error error	   = m_harness->GetError();
		const char * why	   = error.message != nullptr ? error.message : "no diagnostic";
		const bool mustBeThere = BackendIsRequired(GetParam().shortName);

		if (mustBeThere)
		{
			if (error.code == ErrorCode::eNoCompatibleAdapter)
			{
				GTEST_SKIP() << GetParam().displayName << " is required by AZOTH_RHI_TEST_REQUIRE_BACKENDS but no adapter on this machine can back it: " << why;
			}

			FAIL() << GetParam().displayName << " is required by AZOTH_RHI_TEST_REQUIRE_BACKENDS but produced no device on this machine: " << why;
		}

		GTEST_SKIP() << GetParam().displayName << " is compiled in but produced no device on this machine: " << why;
	}

}

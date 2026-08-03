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

	namespace
	{

		[[nodiscard]] const std::vector<Backend> & Available()
		{
			static std::deque<std::string> names;
			static const std::vector<Backend> table = []
			{
				std::vector<Backend> entries;
				for (const BackendEntry & entry : rhi::AvailableBackends())
				{
					entries.push_back(Backend{ .id = entry.id,
						.canonicalName			   = names.emplace_back(entry.canonicalName).c_str(),
						.shortName				   = names.emplace_back(ShortApiName(entry.canonicalName)).c_str(),
						.displayName			   = names.emplace_back(entry.displayName).c_str(),
						.RegisterInto			   = entry.Register });
				}
				return entries;
			}();
			return table;
		}

		[[nodiscard]] std::vector<std::string_view> Split(std::string_view rest)
		{
			std::vector<std::string_view> names;
			while (!rest.empty())
			{
				const std::size_t comma		= rest.find(',');
				const std::string_view head = rest.substr(0, comma);
				if (!head.empty())
				{
					names.push_back(head);
				}
				if (comma == std::string_view::npos)
				{
					break;
				}
				rest.remove_prefix(comma + 1);
			}
			return names;
		}

		[[nodiscard]] const std::vector<Backend> & Selected()
		{
			static const std::vector<Backend> selected = []
			{
				const std::span<const Backend> all			  = AvailableBackends();
				const std::vector<std::string_view> requested = Split(RequestedBackends());
				if (requested.empty())
				{
					return std::vector<Backend>(all.begin(), all.end());
				}

				std::vector<Backend> filtered;
				for (const Backend & backend : all)
				{
					const bool wanted = std::ranges::any_of(requested,
						[&backend](std::string_view name)
						{
							return name == backend.shortName;
						});
					if (wanted)
					{
						filtered.push_back(backend);
					}
				}
				return filtered;
			}();
			return selected;
		}

	} // namespace

	std::span<const Backend> AvailableBackends() noexcept
	{
		return Available();
	}

	std::span<const Backend> SelectedBackends()
	{
		return Selected();
	}

	std::string ValidateBackendSelection()
	{
		for (const char * variable : { "AZOTH_RHI_TEST_BACKENDS", "AZOTH_RHI_TEST_REQUIRE_BACKENDS" })
		{
			const std::string_view value = std::string_view{ variable } == "AZOTH_RHI_TEST_BACKENDS" ? RequestedBackends() : RequiredBackends();

			std::string unknown;
			for (const std::string_view name : Split(value))
			{
				const bool known = std::ranges::any_of(AvailableBackends(),
					[name](const Backend & backend)
					{
						return name == backend.shortName;
					});
				if (!known)
				{
					if (!unknown.empty())
					{
						unknown += ", ";
					}
					unknown += name;
				}
			}

			if (unknown.empty())
			{
				continue;
			}

			std::string message = std::string{ variable } + " names backends this build does not have: " + unknown + ". Available:";
			for (const Backend & backend : AvailableBackends())
			{
				message += ' ';
				message += backend.shortName;
			}
			return message;
		}

		return {};
	}

	bool BackendIsRequired(const std::string_view shortName)
	{
		const std::vector<std::string_view> required = Split(RequiredBackends());
		return std::ranges::any_of(required,
			[shortName](std::string_view name)
			{
				return name == shortName;
			});
	}

	const Backend * FindBackend(const GraphicsApiId id) noexcept
	{
		for (const Backend & backend : AvailableBackends())
		{
			if (backend.id == id)
			{
				return &backend;
			}
		}
		return nullptr;
	}

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
			FAIL() << GetParam().displayName << " is required by AZOTH_RHI_TEST_REQUIRE_BACKENDS but produced no device on this machine: " << why;
		}

		GTEST_SKIP() << GetParam().displayName << " is compiled in but produced no device on this machine: " << why;
	}

} // namespace azo::rhi::test

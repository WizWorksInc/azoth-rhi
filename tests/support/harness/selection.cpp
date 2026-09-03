// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

// Which backends a run was asked for, kept apart from the fixture that consumes them. BackendTest carries a vtable reaching DefaultDeviceDesc, which every suite
// defines for itself, so a suite with its own main cannot link the fixture's object at all. Asking which backends are selected does not need any of that.

#include "azoth/rhi/device/selection.hpp"

#include "azoth/rhi/device/api_tags.hpp"

#include "harness/backends.hpp"
#include "harness/environment.hpp"

#include <algorithm>
#include <deque>
#include <mutex>
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

	}

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

	bool BackendIsSelected(const std::string_view shortName)
	{
		return std::ranges::any_of(SelectedBackends(),
			[shortName](const Backend & backend)
			{
				return shortName == backend.shortName;
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

	namespace
	{

		constexpr std::size_t kValidationLogLimit = 16;

		[[nodiscard]] std::mutex & ValidationLogMutex()
		{
			static std::mutex mutex;
			return mutex;
		}

		[[nodiscard]] std::vector<std::string> & ValidationLogLines()
		{
			static std::vector<std::string> lines;
			return lines;
		}

	}

	void RecordValidationMessage(const ValidationMessageSeverity severity, const char * message, void *) noexcept
	{
		if (severity != ValidationMessageSeverity::eError || message == nullptr)
		{
			return;
		}

		const std::lock_guard<std::mutex> held(ValidationLogMutex());
		std::vector<std::string> & lines = ValidationLogLines();
		if (lines.size() < kValidationLogLimit)
		{
			lines.emplace_back(message);
		}
	}

	std::string ValidationMessageLog()
	{
		const std::lock_guard<std::mutex> held(ValidationLogMutex());
		const std::vector<std::string> & lines = ValidationLogLines();
		if (lines.empty())
		{
			return {};
		}

		std::string joined = "\nvalidation errors recorded by any device in this process:";
		for (const std::string & line : lines)
		{
			joined += "\n  ";
			joined += line;
		}
		return joined;
	}

	void ClearValidationMessageLog()
	{
		const std::lock_guard<std::mutex> held(ValidationLogMutex());
		ValidationLogLines().clear();
	}
}

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
 * \brief Scope-exit cleanup helper for backend ownership-transfer code.
 */

#include <utility>

namespace azo::rhi::detail
{
	/**
	 * \brief Runs a cleanup action at scope exit unless ownership transfer disarms it first.
	 *
	 * \warning The cleanup action must not throw because the destructor may run during stack unwinding.
	 * \attention Dismiss leaves cleanup responsibility with the caller, so the guarded resource must already be owned elsewhere.
	 * \attention Fn must be nothrow move constructible because construction is noexcept.
	 */
	template <class Fn>
	class ScopeGuard final
	{
	public:
		explicit ScopeGuard(Fn action) noexcept : m_action(std::move(action)) {}

		ScopeGuard(const ScopeGuard &)			   = delete;
		ScopeGuard & operator=(const ScopeGuard &) = delete;
		ScopeGuard(ScopeGuard &&)				   = delete;
		ScopeGuard & operator=(ScopeGuard &&)	   = delete;

		~ScopeGuard()
		{
			if (m_armed)
			{
				m_action();
			}
		}

		void Dismiss() noexcept
		{
			m_armed = false;
		}

	private:
		Fn m_action;
		bool m_armed = true;
	};

	template <class Fn>
	[[nodiscard]] ScopeGuard<Fn> MakeScopeGuard(Fn action) noexcept
	{
		return ScopeGuard<Fn>{ std::move(action) };
	}
} // namespace azo::rhi::detail

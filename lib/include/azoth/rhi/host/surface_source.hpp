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
 * \brief API-neutral host surface request interface.
 */

#include "azoth/rhi/backend/interface.hpp"

#include <cstddef>

namespace azo::rhi
{

	/**
	 * \brief One host-window request identified by interface id and payload size.
	 *
	 * Payload types append fields across versions, so byteSize lets old hosts fill the prefix they know and lets new hosts refuse short storage.
	 */
	struct SurfaceRequest final
	{
		InterfaceId id{};
		std::size_t byteSize = 0;
		void * payload		 = nullptr;
	};

	/**
	 * \brief Returns the typed payload carried by a surface request.
	 *
	 * Payload must expose kId. Returns null when the request id differs, the payload is null, or the storage is too short for Payload.
	 */
	template <class Payload>
	[[nodiscard]] Payload * SurfacePayloadOf(const SurfaceRequest & request) noexcept
	{
		if (request.id != Payload::kId || request.payload == nullptr || request.byteSize < sizeof(Payload))
		{
			return nullptr;
		}

		return static_cast<Payload *>(request.payload);
	}

	/**
	 * \brief Host-supplied window access answered one request at a time.
	 *
	 * Payload declarations live beside native surface payloads so this interface does not need one virtual method per graphics API.
	 */
	class SurfaceSource
	{
	public:
		SurfaceSource()									 = default;
		SurfaceSource(const SurfaceSource &)			 = delete;
		SurfaceSource & operator=(const SurfaceSource &) = delete;
		SurfaceSource(SurfaceSource &&)					 = delete;
		SurfaceSource & operator=(SurfaceSource &&)		 = delete;
		virtual ~SurfaceSource()						 = default;

		/**
		 * \brief Fills a supported surface request.
		 *
		 * False is the normal answer for an API or window capability the host cannot provide. True means the payload was written with a valid answer.
		 */
		[[nodiscard]] virtual bool Provide(const SurfaceRequest & request) = 0;
	};

} // namespace azo::rhi

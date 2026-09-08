// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include "azoth/rhi/backend/interface.hpp"

#include <cstddef>

namespace azo::rhi
{

	struct SurfaceRequest final
	{
		InterfaceId id{};
		std::size_t byteSize = 0;
		void * payload		 = nullptr;
	};

	template <class Payload>
	[[nodiscard]] Payload * SurfacePayloadOf(const SurfaceRequest & request) noexcept
	{
		if (request.id != Payload::kId || request.payload == nullptr || request.byteSize < sizeof(Payload))
		{
			return nullptr;
		}

		return static_cast<Payload *>(request.payload);
	}

	class SurfaceSource
	{
	public:
		SurfaceSource()									 = default;
		SurfaceSource(const SurfaceSource &)			 = delete;
		SurfaceSource & operator=(const SurfaceSource &) = delete;
		SurfaceSource(SurfaceSource &&)					 = delete;
		SurfaceSource & operator=(SurfaceSource &&)		 = delete;
		virtual ~SurfaceSource()						 = default;

		[[nodiscard]] virtual bool Provide(const SurfaceRequest & request) = 0;
	};

}

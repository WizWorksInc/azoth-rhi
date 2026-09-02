// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include "azoth/rhi/backend/blocks/common.hpp"

#include <cstdint>

namespace azo::rhi
{

	struct SwapchainApi final
	{
		InterfaceHeader header{ .byteSize = sizeof(SwapchainApi), .version = 1 };

		AcquireResult (*acquireNextImage)(void * impl, std::uint64_t timeoutNanoseconds, Error * error) noexcept = nullptr;

		PresentResult (*present)(
			void * impl, std::uint32_t imageIndex, BinarySemaphoreHandle renderFinished, void * queueImpl, Error * error) noexcept = nullptr;

		TextureHandle (*getBackBuffer)(void * impl, std::uint32_t imageIndex) noexcept = nullptr;

		TextureViewHandle (*getBackBufferView)(void * impl, std::uint32_t imageIndex) noexcept = nullptr;

		BinarySemaphoreHandle (*getPerImagePresentSemaphore)(void * impl, std::uint32_t imageIndex) noexcept = nullptr;

		Format (*getFormat)(void * impl) noexcept			 = nullptr;
		PresentMode (*getPresentMode)(void * impl) noexcept	 = nullptr;
		std::uint32_t (*getImageCount)(void * impl) noexcept = nullptr;
		std::uint32_t (*getWidth)(void * impl) noexcept		 = nullptr;
		std::uint32_t (*getHeight)(void * impl) noexcept	 = nullptr;

		bool (*resize)(void * impl, std::uint32_t width, std::uint32_t height, Error * error) noexcept = nullptr;

		bool (*setPresentMode)(void * impl, PresentMode mode, Error * error) noexcept = nullptr;

		bool (*supportsReadback)(void * impl) noexcept = nullptr;
	};

}

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
 * \brief Null backend native interop payloads and accessors.
 */

#include "azoth/rhi/device/api_tags.hpp"
#include "azoth/rhi/native/native_access.hpp"

namespace azo::rhi::native
{

	/**
	 * \name Empty Null backend native views
	 *
	 * The Null backend exposes native-access types for API uniformity, but none carry a native handle. \{
	 */

	struct NullDeviceView final
	{
	};

	struct NullQueueView final
	{
	};

	struct NullCommandListView final
	{
	};

	struct NullBufferView final
	{
	};

	struct NullTextureView final
	{
	};

	/** \} */

	/**
	 * \brief Native access surface for the Null backend.
	 *
	 * Every view is empty because the backend has no native graphics objects to expose.
	 */
	template <>
	struct NativeAccess<NullApi> final
	{
		using DeviceView	  = NullDeviceView;
		using QueueView		  = NullQueueView;
		using CommandListView = NullCommandListView;
		using BufferView	  = NullBufferView;
		using TextureView	  = NullTextureView;

		[[nodiscard]] static NullCommandListView MakeCommandListView([[maybe_unused]] void * commandListImpl) noexcept
		{
			return {};
		}
	};

} // namespace azo::rhi::native

namespace azo::rhi
{

	/**
	 * \brief Empty native buffer import and export payload for the Null backend.
	 */
	template <>
	struct NativeBuffer<NullApi> final
	{
	};

	/**
	 * \brief Empty native texture import and export payload for the Null backend.
	 */
	template <>
	struct NativeTexture<NullApi> final
	{
	};

} // namespace azo::rhi

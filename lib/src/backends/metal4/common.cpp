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

#include "backends/metal4/internal.hpp"

namespace azo::rhi::metal4
{
	/*
	 * Refuses an export declaration Metal cannot honor, at creation.
	 *
	 * What it can honor is narrow and was established by reading the API, not assumed: a texture through MTLSharedTextureHandle, a timeline or binary semaphore
	 * through MTLSharedEventHandle, and a buffer through nothing at all, MTLBuffer having no shared handle and MTLDevice no entry point that makes one from a
	 * handle.
	 */
	bool RefuseUnexportable(const Flags<ExternalHandleType> declared, const Flags<ExternalHandleType> allowed, const char * what, Error * error) noexcept
	{
		// Anything outside what this kind can take is refused by name. A buffer allows nothing, which this expresses as an empty mask and not as a separate case, so
		// the one that can export nothing reads the same as the two that can export one thing.
		const Flags<ExternalHandleType> unsupported = declared & ~allowed;
		return unsupported.Empty() ? true : Fail(error, ErrorCode::eUnsupportedFeature, what);
	}

	[[nodiscard]] void * AllocObject(Metal4Device * device, const BackendObject * published, QueueType queueType)
	{
		Metal4Object * object = device->objects.New();
		if (object == nullptr)
		{
			return nullptr;
		}

		object->object	  = published;
		object->owner	  = device;
		object->queueType = queueType;
		object->list	  = nullptr;
		return object;
	}

	// Process lifetime owner for Metal backend instances and devices.

	[[nodiscard]] Metal4BackendOwner & Owner()
	{
		static Metal4BackendOwner owner;
		return owner;
	}

	GraphicsApiId DeviceApiId([[maybe_unused]] void * impl) noexcept
	{
		return Metal4Api::id;
	}

	std::string_view DeviceApiName([[maybe_unused]] void * impl) noexcept
	{
		return Metal4Api::displayName;
	}

	const DeviceCaps & DeviceCapsOf(void * impl) noexcept
	{
		return static_cast<Metal4Device *>(impl)->caps;
	}

	const AdapterInfo & DeviceAdapterInfo(void * impl) noexcept
	{
		return static_cast<Metal4Device *>(impl)->adapter;
	}

	/*
	 * Metal's validation layer is switched on outside the process (Metal API Validation in a scheme or METAL_DEVICE_WRAPPER_TYPE) and reports by printing and by
	 * trapping. It exposes no counter and no callback to tally so there is nothing here to read and the counts stay zero.
	 *
	 * caps.reportsValidationMessageCounts is false for the same reason, since a zero from a device that cannot count would otherwise read as proof of a clean run.
	 */
	ValidationMessageCounts DeviceValidationMessageCounts([[maybe_unused]] void * impl) noexcept
	{
		return {};
	}

} // namespace azo::rhi::metal4

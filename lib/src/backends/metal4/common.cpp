// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#include "backends/metal4/internal.hpp"

namespace azo::rhi::metal4
{
	bool Metal4RefuseUnexportable(const Flags<ExternalHandleType> declared, const Flags<ExternalHandleType> allowed, const char * what, Error * error) noexcept
	{
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

	[[nodiscard]] Metal4BackendOwner & Owner()
	{
		static Metal4BackendOwner owner;
		return owner;
	}

	GraphicsApiId Metal4DeviceApiId([[maybe_unused]] void * impl) noexcept
	{
		return Metal4Api::id;
	}

	std::string_view Metal4DeviceApiName([[maybe_unused]] void * impl) noexcept
	{
		return Metal4Api::displayName;
	}

	const DeviceCaps & Metal4DeviceCaps(void * impl) noexcept
	{
		return static_cast<Metal4Device *>(impl)->caps;
	}

	const AdapterInfo & Metal4DeviceAdapterInfo(void * impl) noexcept
	{
		return static_cast<Metal4Device *>(impl)->adapter;
	}

	ValidationMessageCounts Metal4DeviceValidationMessageCounts([[maybe_unused]] void * impl) noexcept
	{
		return {};
	}

}

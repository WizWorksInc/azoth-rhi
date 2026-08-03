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

#include "backends/metal/internal.hpp"

namespace azo::rhi::metal
{
	/*
	 * Refuses an export declaration Metal cannot honor, at creation.
	 *
	 * What it can honor is narrow and was established by reading the API, not assumed: a texture through MTLSharedTextureHandle, a timeline or binary semaphore
	 * through MTLSharedEventHandle, and a buffer through nothing at all, MTLBuffer having no shared handle and MTLDevice no entry point that makes one from a
	 * handle.
	 */
	bool MetalRefuseUnexportable(const Flags<ExternalHandleType> declared, const Flags<ExternalHandleType> allowed, const char * what, Error * error) noexcept
	{
		// Anything outside what this kind can take is refused by name. A buffer allows nothing, which this expresses as an empty mask and not as a separate case, so
		// the one that can export nothing reads the same as the two that can export one thing.
		const Flags<ExternalHandleType> unsupported = declared & ~allowed;
		return unsupported.Empty() ? true : Fail(error, ErrorCode::eUnsupportedFeature, what);
	}

	[[nodiscard]] void * AllocObject(MetalDevice * device, const BackendObject * published, QueueType queueType)
	{
		MetalObject * object = device->objects.New();
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

	/*
	 * Residency, which is what makes a descriptor written after recording reachable at all.
	 *
	 * One set per resource kind, holding everything of that kind the device made, and every one added to every queue. Coarser than tracking what each list
	 * touches, and deliberately so: the alternative rebuilds a set per submission. A resource that is resident and unread costs memory pressure, one read and
	 * not resident reads as zeros.
	 *
	 * Not a replacement for the useResource calls a bind makes.
	 */
	void MetalDevice::NoteAllocation(const Residency kind, const MTL::Allocation * allocation) noexcept
	{
		// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index): kind is an enumerator of the array's own size.
		const NS::SharedPtr<MTL::ResidencySet> & set = residencySets[static_cast<std::size_t>(kind)];
		if (allocation == nullptr || set.get() == nullptr)
		{
			return;
		}

		set->addAllocation(allocation);

		/*
		 * Committed per addition, not batched. Metal takes a commit as the point the set's contents become what the queue sees, so deferring it would
		 * leave a resource created mid-frame unreachable until something else happened to commit. Adding is cheap and the commit is what makes it true.
		 */
		set->commit();
		set->requestResidency();
	}

	// Process lifetime owner for Metal backend instances and devices.

	[[nodiscard]] MetalBackendOwner & Owner()
	{
		static MetalBackendOwner owner;
		return owner;
	}

	GraphicsApiId MetalDeviceApiId([[maybe_unused]] void * impl) noexcept
	{
		return MetalApi::id;
	}

	std::string_view MetalDeviceApiName([[maybe_unused]] void * impl) noexcept
	{
		return MetalApi::displayName;
	}

	const DeviceCaps & MetalDeviceCaps(void * impl) noexcept
	{
		return static_cast<MetalDevice *>(impl)->caps;
	}

	const AdapterInfo & MetalDeviceAdapterInfo(void * impl) noexcept
	{
		return static_cast<MetalDevice *>(impl)->adapter;
	}

	/*
	 * Metal's validation layer is switched on outside the process (Metal API Validation in a scheme or METAL_DEVICE_WRAPPER_TYPE) and reports by printing and by
	 * trapping. It exposes no counter and no callback to tally so there is nothing here to read and the counts stay zero.
	 *
	 * caps.reportsValidationMessageCounts is false for the same reason, since a zero from a device that cannot count would otherwise read as proof of a clean run.
	 */
	ValidationMessageCounts MetalDeviceValidationMessageCounts([[maybe_unused]] void * impl) noexcept
	{
		return {};
	}

} // namespace azo::rhi::metal

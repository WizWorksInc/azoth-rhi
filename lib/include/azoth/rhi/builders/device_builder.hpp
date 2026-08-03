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
 * \brief Builder for device creation descriptions.
 */

#include "azoth/rhi/core/enums.hpp"
#include "azoth/rhi/core/result.hpp"
#include "azoth/rhi/device/api_tags.hpp"
#include "azoth/rhi/device/device.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
// ReSharper disable once CppUnusedIncludeDirective
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace azo::rhi
{
	/**
	 * \brief Builds DeviceDesc values and creates devices from them.
	 *
	 * By default, the builder requests one graphics queue. Adding explicit queues suppresses that default unless DefaultGraphicsQueue is enabled again after
	 * ClearQueues leaves no explicit requests.
	 */
	class DeviceBuilder final
	{
	public:
		static constexpr std::size_t kMaxQueueRequests	 = 8;
		static constexpr std::size_t kMaxFeatureRequests = 16;

		DeviceBuilder() noexcept = default;

		DeviceBuilder & DebugName(const std::string_view name)
		{
			m_debugName.assign(name.data(), name.size());
			return *this;
		}

		DeviceBuilder & Validation(const ValidationMode mode) noexcept
		{
			m_desc.validation = mode;
			return *this;
		}

		DeviceBuilder & ApiVersionRequest(const std::uint32_t major, const std::uint32_t minor) noexcept
		{
			m_desc.apiVersion = ApiVersion{
				.major = major,
				.minor = minor,
			};
			return *this;
		}

		DeviceBuilder & DebugNames(const bool enabled = true) noexcept
		{
			m_desc.enableDebugNames = enabled;
			return *this;
		}

		DeviceBuilder & DebugLabels(const bool enabled = true) noexcept
		{
			m_desc.enableDebugLabels = enabled;
			return *this;
		}

		DeviceBuilder & PreferDiscreteGpu(const bool enabled = true) noexcept
		{
			m_desc.preferDiscreteGpu = enabled;
			return *this;
		}

		DeviceBuilder & RequireSwapchain(const bool enabled = true) noexcept
		{
			m_desc.requireSwapchain = enabled;
			return *this;
		}

		DeviceBuilder & Headless(const bool enabled = true) noexcept
		{
			m_desc.requireSwapchain = !enabled;
			return *this;
		}

		DeviceBuilder & DynamicRendering(const DynamicRenderingMode mode) noexcept
		{
			m_desc.dynamicRendering = mode;
			return *this;
		}

		DeviceBuilder & Threading(const ThreadingMode mode) noexcept
		{
			m_desc.threading = mode;
			return *this;
		}

		/**
		 * \brief Selects cooperative threading and stores the host synchronization callbacks.
		 *
		 * \attention ops.context must remain valid for any device created from this builder because SyncOps is copied but the context pointer is not owned.
		 */
		DeviceBuilder & Cooperative(const SyncOps & ops) noexcept
		{
			m_desc.threading = ThreadingMode::eCooperative;
			m_desc.sync		 = ops;
			return *this;
		}

		DeviceBuilder & AllowSoftwareAdapter(const bool enabled = true) noexcept
		{
			m_desc.allowSoftwareAdapter = enabled;
			return *this;
		}

		DeviceBuilder & AllowedLinkedAdapters(const bool enabled = true) noexcept
		{
			m_desc.allowLinkedAdapters = enabled;
			return *this;
		}

		DeviceBuilder & PreferredAdapter(const std::uint32_t adapterIndex) noexcept
		{
			m_desc.preferredAdapterIndex = adapterIndex;
			return *this;
		}

		/**
		 * \brief Enables or disables the implicit graphics queue request.
		 *
		 * \note The implicit request is emitted only when there are no explicit queue requests.
		 */
		DeviceBuilder & DefaultGraphicsQueue(const bool enabled = true) noexcept
		{
			m_useDefaultGraphicsQueue = enabled;
			return *this;
		}

		/**
		 * \brief Removes all explicit queue requests while preserving the implicit graphics queue setting.
		 */
		DeviceBuilder & ClearQueues() noexcept
		{
			m_queueCount	  = 0;
			m_queueOverflowed = false;
			return *this;
		}

		/**
		 * \brief Adds or replaces a queue request for one queue type.
		 *
		 * \param minCount Minimum queue count for this type. Zero is rejected during Build validation.
		 * \param requireDedicatedQueue True to reject a shared queue-family fallback for compute or copy queues.
		 * \attention Only one request per queue type is stored. Calling Queue again for the same type replaces the earlier request.
		 */
		DeviceBuilder & Queue(const QueueType type, const std::uint32_t minCount = 1, const bool requireDedicatedQueue = false) noexcept
		{
			const QueueRequest request{
				.type				   = type,
				.minCount			   = minCount,
				.requireDedicatedQueue = requireDedicatedQueue,
			};

			const std::size_t existing = FindQueue(type);
			if (existing != kInvalidQueueIndex)
			{
				// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index, cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
				m_queues[existing] = request;
				return *this;
			}

			if (m_queueCount >= m_queues.size())
			{
				m_queueOverflowed = true;
				return *this;
			}

			// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index, cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
			m_queues[m_queueCount] = request;
			++m_queueCount;
			return *this;
		}

		DeviceBuilder & GraphicsQueue(const std::uint32_t minCount = 1, const bool requireDedicatedQueue = false) noexcept
		{
			return Queue(QueueType::eGraphics, minCount, requireDedicatedQueue);
		}

		DeviceBuilder & ComputeQueue(const std::uint32_t minCount = 1, const bool requireDedicatedQueue = false) noexcept
		{
			return Queue(QueueType::eCompute, minCount, requireDedicatedQueue);
		}

		DeviceBuilder & CopyQueue(const std::uint32_t minCount = 1, const bool requireDedicatedQueue = false) noexcept
		{
			return Queue(QueueType::eCopy, minCount, requireDedicatedQueue);
		}

		DeviceBuilder & DedicatedComputeQueue(const std::uint32_t minCount = 1) noexcept
		{
			return ComputeQueue(minCount, true);
		}

		DeviceBuilder & DedicatedCopyQueue(const std::uint32_t minCount = 1) noexcept
		{
			return CopyQueue(minCount, true);
		}

		/**
		 * \brief Adds a required feature request.
		 *
		 * Required features must be supported by the selected adapter and are enabled on the created device.
		 * \attention Additional distinct required features beyond kMaxFeatureRequests are ignored.
		 */
		DeviceBuilder & RequireFeature(const DeviceFeature feature) noexcept
		{
			AddFeature(m_requiredFeatures, m_requiredFeatureCount, feature);
			return *this;
		}

		/**
		 * \brief Adds a preferred feature request.
		 *
		 * Preferred features bias adapter selection and are enabled only when supported.
		 * \attention Additional distinct preferred features beyond kMaxFeatureRequests are ignored.
		 */
		DeviceBuilder & PreferFeature(const DeviceFeature feature) noexcept
		{
			AddFeature(m_preferredFeatures, m_preferredFeatureCount, feature);
			return *this;
		}

		/**
		 * \brief Creates a device through a compile-time-selected graphics API.
		 *
		 * \attention The DeviceDesc spans and debugName pointer assembled here are valid only during the CreateDevice call.
		 */
		template <GraphicsApiTag Api>
		[[nodiscard]] Result<UniqueDevice> Build() const
		{
			std::array<QueueRequest, kMaxQueueRequests> queues{};
			const std::size_t queueCount = MakeQueueRequests(queues);

			const Result<void> validation = Validate(std::span<const QueueRequest>{ queues.data(), queueCount });
			if (!validation)
			{
				return validation.GetError();
			}

			DeviceDesc desc		   = m_desc;
			desc.queues			   = std::span<const QueueRequest>{ queues.data(), queueCount };
			desc.debugName		   = m_debugName.empty() ? nullptr : m_debugName.c_str();
			desc.requiredFeatures  = std::span<const DeviceFeature>{ m_requiredFeatures.data(), m_requiredFeatureCount };
			desc.preferredFeatures = std::span<const DeviceFeature>{ m_preferredFeatures.data(), m_preferredFeatureCount };

			return CreateDevice<Api>(desc);
		}

		/**
		 * \brief Creates a device from a registry and ordered API preference list.
		 *
		 * \param preferredApis Ordered graphics API ids. The list must not be empty.
		 * \attention The DeviceDesc spans and debugName pointer assembled here are valid only during the CreateDevice call.
		 */
		[[nodiscard]] Result<UniqueDevice> Build(GraphicsApiRegistry & registry, std::span<const GraphicsApiId> preferredApis) const
		{
			if (preferredApis.empty())
			{
				return Error{
					.code	 = ErrorCode::eInvalidArgument,
					.message = "DeviceBuilder requires at least one preferred graphics API",
				};
			}

			std::array<QueueRequest, kMaxQueueRequests> queues{};
			const std::size_t queueCount = MakeQueueRequests(queues);

			const Result<void> validation = Validate(std::span<const QueueRequest>{ queues.data(), queueCount });
			if (!validation)
			{
				return validation.GetError();
			}

			DeviceDesc desc		   = m_desc;
			desc.queues			   = std::span<const QueueRequest>{ queues.data(), queueCount };
			desc.debugName		   = m_debugName.empty() ? nullptr : m_debugName.c_str();
			desc.requiredFeatures  = std::span<const DeviceFeature>{ m_requiredFeatures.data(), m_requiredFeatureCount };
			desc.preferredFeatures = std::span<const DeviceFeature>{ m_preferredFeatures.data(), m_preferredFeatureCount };

			return CreateDevice(registry, preferredApis, desc);
		}

	private:
		static constexpr std::size_t kInvalidQueueIndex = static_cast<std::size_t>(-1);

		/**
		 * \brief Adds a distinct feature request while preserving insertion order.
		 *
		 * \note Extra requests past kMaxFeatureRequests are ignored.
		 */
		static void AddFeature(std::array<DeviceFeature, kMaxFeatureRequests> & features, std::size_t & count, const DeviceFeature feature) noexcept
		{
			for (std::size_t index = 0; index < count; ++index)
			{
				// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index, cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
				if (features[index] == feature)
				{
					return;
				}
			}

			if (count < features.size())
			{
				// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index, cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
				features[count] = feature;
				++count;
			}
		}

		/**
		 * \brief Writes explicit queues, or one implicit graphics queue when no explicit queues exist.
		 *
		 * \param outQueues Fixed output storage that receives at most kMaxQueueRequests entries.
		 */
		[[nodiscard]] std::size_t MakeQueueRequests(std::array<QueueRequest, kMaxQueueRequests> & outQueues) const noexcept
		{
			for (std::size_t index = 0; index < m_queueCount; ++index)
			{
				// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index, cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
				outQueues[index] = m_queues[index];
			}

			if (m_queueCount == 0 && m_useDefaultGraphicsQueue)
			{
				// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
				outQueues[0] = QueueRequest{
					.type				   = QueueType::eGraphics,
					.minCount			   = 1,
					.requireDedicatedQueue = false,
				};

				return 1;
			}

			return m_queueCount;
		}

		/**
		 * \brief Validates the completed queue portion of the device request.
		 *
		 * \param queues Resolved queue requests after applying the implicit graphics queue rule.
		 */
		[[nodiscard]] Result<void> Validate(const std::span<const QueueRequest> queues) const noexcept
		{
			if (m_queueOverflowed)
			{
				return Error{
					.code	 = ErrorCode::eInvalidArgument,
					.message = "DeviceBuilder exceeded the maximum queue request count",
				};
			}

			if (queues.empty())
			{
				return Error{
					.code	 = ErrorCode::eInvalidArgument,
					.message = "DeviceBuilder requires at least one queue request",
				};
			}

			for (const QueueRequest & queue : queues)
			{
				if (queue.minCount == 0)
				{
					return Error{
						.code	 = ErrorCode::eInvalidArgument,
						.message = "DeviceBuilder queue minCount must be greater than zero",
					};
				}
			}

			if (m_desc.requireSwapchain && !ContainsQueue(queues, QueueType::eGraphics))
			{
				return Error{
					.code	 = ErrorCode::eInvalidArgument,
					.message = "DeviceBuilder requires a graphics queue when swapchain support is required",
				};
			}

			return {};
		}

		[[nodiscard]] static bool ContainsQueue(std::span<const QueueRequest> queues, QueueType type) noexcept
		{
			return std::ranges::any_of(
				queues,
				[type](const QueueType queueType) noexcept
				{
					return queueType == type;
				},
				&QueueRequest::type);
		}

		[[nodiscard]] std::size_t FindQueue(const QueueType type) const noexcept
		{
			for (std::size_t index = 0; index < m_queueCount; ++index)
			{
				// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index, cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
				if (m_queues[index].type == type)
				{
					return index;
				}
			}

			return kInvalidQueueIndex;
		}

		DeviceDesc m_desc{};
		std::array<QueueRequest, kMaxQueueRequests> m_queues{};
		std::size_t m_queueCount	   = 0;
		bool m_queueOverflowed		   = false;
		bool m_useDefaultGraphicsQueue = true;
		std::array<DeviceFeature, kMaxFeatureRequests> m_requiredFeatures{};
		std::size_t m_requiredFeatureCount = 0;
		std::array<DeviceFeature, kMaxFeatureRequests> m_preferredFeatures{};
		std::size_t m_preferredFeatureCount = 0;
		std::string m_debugName;
	};
} // namespace azo::rhi

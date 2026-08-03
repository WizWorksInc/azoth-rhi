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

#include "fixtures/minimal_backend.hpp"

#include "azoth/rhi/backend/dispatch.hpp"
#include "azoth/rhi/backend/support/host_containers.hpp"
#include "azoth/rhi/backend/table_validation.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <new>

namespace azo::rhi::test::minimal
{
	namespace
	{

		std::atomic<std::size_t> g_live{ 0 };
		std::atomic<std::uint32_t> g_nextHandle{ 1 };

		struct Object final
		{
			const BackendObject * object = nullptr;
			bool presenting				 = false;
			QueueType queueType			 = QueueType::eGraphics;
			DeviceCaps caps{};
			AdapterInfo adapter{};

			Object * instance = nullptr;

			Object * owner = nullptr;
			detail::HostVector<Object *> children;

			std::uint32_t width	 = 64;
			std::uint32_t height = 64;

			Object()
			{
				g_live.fetch_add(1, std::memory_order_relaxed);
			}

			~Object()
			{
				g_live.fetch_sub(1, std::memory_order_relaxed);
			}

			Object(const Object &)			   = delete;
			Object & operator=(const Object &) = delete;
			Object(Object &&)				   = delete;
			Object & operator=(Object &&)	   = delete;
		};

		[[nodiscard]] Object * New(const BackendObject * published, const bool presenting = false)
		{
			// NOLINTNEXTLINE(cppcoreguidelines-owning-memory): the RHI holds this as a void * and hands it back to destroy, which no owner type survives.
			auto * object	   = new Object();
			object->object	   = published;
			object->presenting = presenting;
			return object;
		}

		[[nodiscard]] Object * NewChild(Object * owner, const BackendObject * published)
		{
			Object * child = New(published, owner->presenting);
			child->owner   = owner;
			owner->children.push_back(child);
			return child;
		}

		bool Succeed(Error * error) noexcept
		{
			if (error != nullptr)
			{
				*error = Error{};
			}

			return true;
		}

		template <class HandleT>
		HandleT NewHandle(Error * error) noexcept
		{
			Succeed(error);
			return HandleT{ .index = g_nextHandle.fetch_add(1, std::memory_order_relaxed), .generation = 1 };
		}

		template <class... Args>
		bool Accept(void *, Args...) noexcept
		{
			return true;
		}

		template <class... Args>
		bool AcceptReporting(void *, Args..., Error * error) noexcept
		{
			return Succeed(error);
		}

	} // namespace

	namespace
	{

		const CoreDeviceApi & CoreBlock() noexcept;
		const QueueApi & QueueBlock() noexcept;
		const CommandPoolApi & CommandPoolBlock() noexcept;
		const DescriptorArenaApi & DescriptorArenaBlock() noexcept;
		const RenderCommandApi & RenderBlock() noexcept;
		const SwapchainApi & SwapchainBlock() noexcept;

		const BackendObject * CommandListObject() noexcept
		{
			return PublishingObject<Published<RenderCommandApi, &RenderBlock>>();
		}

		const BackendObject * QueueObject() noexcept
		{
			return PublishingObject<Published<QueueApi, &QueueBlock>>();
		}

		const BackendObject * CommandPoolObject() noexcept
		{
			return PublishingObject<Published<CommandPoolApi, &CommandPoolBlock>>();
		}

		const BackendObject * DescriptorArenaObject() noexcept
		{
			return PublishingObject<Published<DescriptorArenaApi, &DescriptorArenaBlock>>();
		}

		const BackendObject * SwapchainObject() noexcept
		{
			return PublishingObject<Published<SwapchainApi, &SwapchainBlock>>();
		}

		GraphicsApiId DeviceApiId(void * impl) noexcept
		{
			return static_cast<Object *>(impl)->presenting ? PresentingApi::id : HeadlessApi::id;
		}

		std::string_view DeviceApiName(void * impl) noexcept
		{
			return static_cast<Object *>(impl)->presenting ? PresentingApi::displayName : HeadlessApi::displayName;
		}

		void * CreateDescriptorArena(void * impl, const DescriptorArenaDesc &, Error * error) noexcept
		{
			Succeed(error);
			return NewChild(static_cast<Object *>(impl), DescriptorArenaObject());
		}

		void * CreateCommandPool(void * impl, const CommandPoolDesc & desc, Error * error) noexcept
		{
			Succeed(error);
			Object * pool	= NewChild(static_cast<Object *>(impl), CommandPoolObject());
			pool->queueType = desc.queueType;
			return pool;
		}

		void * GetQueue(void * impl, const QueueType type, const std::uint32_t index, Error * error) noexcept
		{
			if (index != 0)
			{
				if (error != nullptr)
				{
					*error = Error{ .code = ErrorCode::eInvalidArgument, .message = "this fixture exposes one queue of each type" };
				}

				return nullptr;
			}

			Succeed(error);
			Object * queue	 = NewChild(static_cast<Object *>(impl), QueueObject());
			queue->queueType = type;
			return queue;
		}

		MappedMemory Map(void *, BufferHandle, const MapDesc &, Error * error) noexcept
		{
			if (error != nullptr)
			{
				*error = Error{ .code = ErrorCode::eUnsupportedFeature, .message = "this fixture exposes no mappable memory" };
			}

			return MappedMemory{};
		}

		const DeviceCaps & GetCaps(void * impl) noexcept
		{
			return static_cast<Object *>(impl)->caps;
		}

		FormatSupport GetFormatSupport(void *, Format) noexcept
		{
			return FormatSupport{};
		}

		const AdapterInfo & GetAdapterInfo(void * impl) noexcept
		{
			return static_cast<Object *>(impl)->adapter;
		}

		ValidationMessageCounts GetValidationMessageCounts(void *) noexcept
		{
			return ValidationMessageCounts{};
		}

		bool Destroy(void *, ResourceType, RawHandle, const DestroyDesc &, Error * error) noexcept
		{
			return Succeed(error);
		}

		// NOLINTBEGIN(cppcoreguidelines-owning-memory): these are the destroy entries the block ABI names, reached through a void * no owner type survives.
		void DestroyDevice(void * impl) noexcept
		{
			auto * device = static_cast<Object *>(impl);
			for (Object * child : device->children)
			{
				delete child;
			}

			delete device->instance;
			delete device;
		}

		void DestroyChild(void * impl) noexcept
		{
			delete static_cast<Object *>(impl);
		}
		// NOLINTEND(cppcoreguidelines-owning-memory)

		const CoreDeviceApi & CoreBlock() noexcept
		{
			static const CoreDeviceApi block{
				.getGraphicsApiId	= &DeviceApiId,
				.getGraphicsApiName = &DeviceApiName,
				.createBuffer =
					[](void *, const BufferDesc &, Error * error) noexcept
				{
					return NewHandle<BufferHandle>(error);
				},
				.createTexture =
					[](void *, const TextureDesc &, Error * error) noexcept
				{
					return NewHandle<TextureHandle>(error);
				},
				.createTextureView =
					[](void *, TextureHandle, const TextureViewDesc &, Error * error) noexcept
				{
					return NewHandle<TextureViewHandle>(error);
				},
				.createSampler =
					[](void *, const SamplerDesc &, Error * error) noexcept
				{
					return NewHandle<SamplerHandle>(error);
				},
				.createDescriptorSetLayout =
					[](void *, const DescriptorSetLayoutDesc &, Error * error) noexcept
				{
					return NewHandle<DescriptorSetLayoutHandle>(error);
				},
				.createPipelineLayout =
					[](void *, const PipelineLayoutDesc &, Error * error) noexcept
				{
					return NewHandle<PipelineLayoutHandle>(error);
				},
				.createGraphicsPipeline =
					[](void *, const GraphicsPipelineDesc &, Error * error) noexcept
				{
					return NewHandle<GraphicsPipelineHandle>(error);
				},
				.createComputePipeline =
					[](void *, const ComputePipelineDesc &, Error * error) noexcept
				{
					return NewHandle<ComputePipelineHandle>(error);
				},
				.createTimeline =
					[](void *, const TimelineDesc &, Error * error) noexcept
				{
					return NewHandle<TimelineHandle>(error);
				},
				.createBinarySemaphore =
					[](void *, const BinarySemaphoreDesc &, Error * error) noexcept
				{
					return NewHandle<BinarySemaphoreHandle>(error);
				},
				.createDescriptorArena = &CreateDescriptorArena,
				.createCommandPool	   = &CreateCommandPool,
				.getQueue			   = &GetQueue,
				.map				   = &Map,
				.unmap =
					[](void *, BufferHandle, Error * error) noexcept
				{
					return Succeed(error);
				},
				.flushMappedRange =
					[](void *, BufferHandle, std::uint64_t, std::uint64_t, Error * error) noexcept
				{
					return Succeed(error);
				},
				.invalidateMappedRange =
					[](void *, BufferHandle, std::uint64_t, std::uint64_t, Error * error) noexcept
				{
					return Succeed(error);
				},
				.updateDescriptorsBuffer =
					[](void *, std::span<const DescriptorWriteBuffer>, Error * error) noexcept
				{
					return Succeed(error);
				},
				.updateDescriptorsTexture =
					[](void *, std::span<const DescriptorWriteTexture>, Error * error) noexcept
				{
					return Succeed(error);
				},
				.updateDescriptorsSampler =
					[](void *, std::span<const DescriptorWriteSampler>, Error * error) noexcept
				{
					return Succeed(error);
				},
				.getCaps					= &GetCaps,
				.getFormatSupport			= &GetFormatSupport,
				.getAdapterInfo				= &GetAdapterInfo,
				.getValidationMessageCounts = &GetValidationMessageCounts,
				.destroy					= &Destroy,
				.collectGarbage =
					[](void *, ResourceType, Error * error) noexcept
				{
					return Succeed(error);
				},
				.collectGarbageTimeline =
					[](void *, ResourceType, TimelineHandle, std::uint64_t, Error * error) noexcept
				{
					return Succeed(error);
				},
				.destroyDevice = &DestroyDevice,
			};

			return block;
		}

		const QueueApi & QueueBlock() noexcept
		{
			static const QueueApi block{
				.getType =
					[](void * impl) noexcept
				{
					return static_cast<Object *>(impl)->queueType;
				},
				.getFamilyIndex =
					[](void *) noexcept
				{
					return std::uint32_t{ 0 };
				},
				.submit =
					[](void *, const SubmitDesc &, Error * error) noexcept
				{
					return Succeed(error);
				},
				.waitIdle =
					[](void *, Error * error) noexcept
				{
					return Succeed(error);
				},
				.getCompletedValue =
					[](void *, TimelineHandle, std::uint64_t * out, Error * error) noexcept
				{
					*out = ~std::uint64_t{ 0 };
					return Succeed(error);
				},
				.wait =
					[](void *, TimelineHandle, std::uint64_t, std::uint64_t, Error * error) noexcept
				{
					return Succeed(error);
				},
				.signal =
					[](void *, TimelineHandle, std::uint64_t, Error * error) noexcept
				{
					return Succeed(error);
				},
				.beginDebugLabel =
					[](void *, CString, std::uint32_t, Error * error) noexcept
				{
					return Succeed(error);
				},
				.endDebugLabel =
					[](void *, Error * error) noexcept
				{
					return Succeed(error);
				},
			};

			return block;
		}

		const CommandPoolApi & CommandPoolBlock() noexcept
		{
			static const CommandPoolApi block{
				.allocate =
					[](void * impl, CString, Error * error) noexcept
				{
					Succeed(error);
					return static_cast<void *>(NewChild(static_cast<Object *>(impl)->owner, CommandListObject()));
				},
				.reset =
					[](void *, RetirePoint, Error * error) noexcept
				{
					return Succeed(error);
				},
			};

			return block;
		}

		const DescriptorArenaApi & DescriptorArenaBlock() noexcept
		{
			static const DescriptorArenaApi block{
				.allocate =
					[](void *, const DescriptorSetAllocDesc &, Error * error) noexcept
				{
					return NewHandle<DescriptorSetHandle>(error);
				},
				.reset =
					[](void *, RetirePoint, Error * error) noexcept
				{
					return Succeed(error);
				},
			};

			return block;
		}

		const RenderCommandApi & RenderBlock() noexcept
		{
			static const RenderCommandApi block{
				.begin =
					[](void *, Error * error) noexcept
				{
					return Succeed(error);
				},
				.end =
					[](void *, Error * error) noexcept
				{
					return Succeed(error);
				},
				.barriers =
					[](void *, const BarrierBatch &, Error * error) noexcept
				{
					return Succeed(error);
				},
				.beginRendering =
					[](void *, const BeginRenderingDesc &, Error * error) noexcept
				{
					return Succeed(error);
				},
				.endRendering =
					[](void *, Error * error) noexcept
				{
					return Succeed(error);
				},
				.setGraphicsPipeline =
					[](void *, GraphicsPipelineHandle, Error * error) noexcept
				{
					return Succeed(error);
				},
				.setComputePipeline =
					[](void *, ComputePipelineHandle, Error * error) noexcept
				{
					return Succeed(error);
				},
				.bindDescriptorSet =
					[](void *, PipelineLayoutHandle, std::uint32_t, DescriptorSetHandle, std::span<const DynamicDescriptorOffset>, Error * error) noexcept
				{
					return Succeed(error);
				},
				.pushConstants =
					[](void *, PipelineLayoutHandle, Flags<ShaderStage>, std::uint32_t, std::uint32_t, const void *, Error * error) noexcept
				{
					return Succeed(error);
				},
				.setViewport =
					[](void *, const Viewport &, Error * error) noexcept
				{
					return Succeed(error);
				},
				.setScissor =
					[](void *, const Rect2D &, Error * error) noexcept
				{
					return Succeed(error);
				},
				.setBlendConstants =
					[](void *, float, float, float, float, Error * error) noexcept
				{
					return Succeed(error);
				},
				.setStencilReference =
					[](void *, std::uint32_t, Error * error) noexcept
				{
					return Succeed(error);
				},
				.setDepthBias =
					[](void *, float, float, float, Error * error) noexcept
				{
					return Succeed(error);
				},
				.setVertexBuffer =
					[](void *, std::uint32_t, BufferHandle, std::uint64_t, Error * error) noexcept
				{
					return Succeed(error);
				},
				.setIndexBuffer =
					[](void *, BufferHandle, std::uint64_t, bool, Error * error) noexcept
				{
					return Succeed(error);
				},
				.draw =
					[](void *, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, Error * error) noexcept
				{
					return Succeed(error);
				},
				.drawIndexed =
					[](void *, std::uint32_t, std::uint32_t, std::uint32_t, std::int32_t, std::uint32_t, Error * error) noexcept
				{
					return Succeed(error);
				},
				.dispatch =
					[](void *, std::uint32_t, std::uint32_t, std::uint32_t, Error * error) noexcept
				{
					return Succeed(error);
				},
				.copyBuffer =
					[](void *, BufferHandle, std::uint64_t, BufferHandle, std::uint64_t, std::uint64_t, Error * error) noexcept
				{
					return Succeed(error);
				},
				.copyBufferToTexture =
					[](void *, TextureHandle, BufferHandle, std::span<const BufferTextureCopy>, Error * error) noexcept
				{
					return Succeed(error);
				},
				.copyTextureToBuffer =
					[](void *, BufferHandle, TextureHandle, std::span<const BufferTextureCopy>, Error * error) noexcept
				{
					return Succeed(error);
				},
				.copyTexture =
					[](void *, TextureHandle, TextureHandle, std::span<const TextureCopy>, Error * error) noexcept
				{
					return Succeed(error);
				},
				.clearBuffer =
					[](void *, BufferHandle, std::uint64_t, std::uint64_t, std::uint32_t, Error * error) noexcept
				{
					return Succeed(error);
				},
				.clearTexture =
					[](void *, TextureHandle, const ClearColor &, std::span<const TextureSubresourceRange>, Error * error) noexcept
				{
					return Succeed(error);
				},
				.resolveTexture =
					[](void *, TextureHandle, TextureHandle, std::span<const TextureResolve>, Error * error) noexcept
				{
					return Succeed(error);
				},
				.blit =
					[](void *, TextureHandle, TextureHandle, std::span<const TextureBlit>, Filter, Error * error) noexcept
				{
					return Succeed(error);
				},
				.generateMips =
					[](void *, TextureHandle, Error * error) noexcept
				{
					return Succeed(error);
				},
				.beginDebugLabel =
					[](void *, CString, std::uint32_t, Error * error) noexcept
				{
					return Succeed(error);
				},
				.endDebugLabel =
					[](void *, Error * error) noexcept
				{
					return Succeed(error);
				},
			};

			return block;
		}

		const SwapchainApi & SwapchainBlock() noexcept
		{
			static const SwapchainApi block{
				.acquireNextImage =
					[](void *, std::uint64_t, Error * error) noexcept
				{
					Succeed(error);
					return AcquireResult{ .status = SwapchainStatus::eOk, .imageIndex = 0, .imageAvailable = {} };
				},
				.present =
					[](void *, std::uint32_t, BinarySemaphoreHandle, void *, Error * error) noexcept
				{
					Succeed(error);
					return PresentResult{ .status = SwapchainStatus::eOk };
				},
				.getBackBuffer =
					[](void *, std::uint32_t) noexcept
				{
					return TextureHandle{ .index = 1, .generation = 1 };
				},
				.getBackBufferView =
					[](void *, std::uint32_t) noexcept
				{
					return TextureViewHandle{ .index = 1, .generation = 1 };
				},
				.getPerImagePresentSemaphore =
					[](void *, std::uint32_t) noexcept
				{
					return BinarySemaphoreHandle{ .index = 1, .generation = 1 };
				},
				.getFormat =
					[](void *) noexcept
				{
					return Format::eBGRA8UNorm;
				},
				.getPresentMode =
					[](void *) noexcept
				{
					return PresentMode::eFifo;
				},
				.getImageCount =
					[](void *) noexcept
				{
					return std::uint32_t{ 2 };
				},
				.getWidth =
					[](void * impl) noexcept
				{
					return static_cast<Object *>(impl)->width;
				},
				.getHeight =
					[](void * impl) noexcept
				{
					return static_cast<Object *>(impl)->height;
				},
				.resize =
					[](void * impl, const std::uint32_t width, const std::uint32_t height, Error * error) noexcept
				{
					auto * self	 = static_cast<Object *>(impl);
					self->width	 = width;
					self->height = height;
					return Succeed(error);
				},
				.setPresentMode =
					[](void *, PresentMode, Error * error) noexcept
				{
					return Succeed(error);
				},
				.supportsReadback =
					[](void *) noexcept
				{
					return false;
				},
			};

			return block;
		}

		const PresentApi & PresentBlock() noexcept
		{
			static const PresentApi block{
				.createSwapchain =
					[](void * impl, const SwapchainDesc & desc, Error * error) noexcept
				{
					Succeed(error);
					Object * swapchain = NewChild(static_cast<Object *>(impl), SwapchainObject());
					if (swapchain != nullptr)
					{
						swapchain->width  = desc.width;
						swapchain->height = desc.height;
					}
					return static_cast<void *>(swapchain);
				},
			};

			return block;
		}

		const BackendObject * HeadlessDeviceObject() noexcept
		{
			return PublishingObject<Published<CoreDeviceApi, &CoreBlock>>();
		}

		const BackendObject * PresentingDeviceObject() noexcept
		{
			return PublishingObject<Published<CoreDeviceApi, &CoreBlock>, Published<PresentApi, &PresentBlock>>();
		}

		template <bool Presenting>
		void * CreateDevice(void * instanceImpl, const DeviceDesc &, Error * error) noexcept
		{
			Succeed(error);
			Object * device	 = New(Presenting ? PresentingDeviceObject() : HeadlessDeviceObject(), Presenting);
			device->instance = static_cast<Object *>(instanceImpl);

			device->caps.apiId					  = Presenting ? PresentingApi::id : HeadlessApi::id;
			device->caps.graphicsQueueCount		  = 1;
			device->caps.computeQueueCount		  = 1;
			device->caps.copyQueueCount			  = 1;
			device->caps.supportsDynamicRendering = true;
			device->adapter.apiId				  = device->caps.apiId;
			device->adapter.name				  = Presenting ? "Minimal presenting fixture" : "Minimal headless fixture";

			return device;
		}

		template <bool Presenting>
		const InstanceApi & InstanceBlock() noexcept
		{
			static const InstanceApi block{
				.getGraphicsApiId =
					[](void *) noexcept
				{
					return Presenting ? PresentingApi::id : HeadlessApi::id;
				},
				.enumerateAdapters =
					[](void *, std::span<AdapterInfo> adapters, std::uint32_t * out, Error * error) noexcept
				{
					*out = 1;
					if (!adapters.empty())
					{
						adapters[0]		  = AdapterInfo{};
						adapters[0].apiId = Presenting ? PresentingApi::id : HeadlessApi::id;
						adapters[0].name  = Presenting ? "Minimal presenting fixture" : "Minimal headless fixture";
					}

					return Succeed(error);
				},
				.createDevice	 = &CreateDevice<Presenting>,
				.destroyInstance = &DestroyChild,
			};

			return block;
		}

		template <bool Presenting>
		void * CreateInstance(const void *, Error * error) noexcept
		{
			Succeed(error);
			return New(PublishingObject<Published<InstanceApi, &InstanceBlock<Presenting>>>(), Presenting);
		}

	} // namespace

	Result<void> RegisterHeadless(GraphicsApiRegistry & registry)
	{
		BackendCreateInfo info{};
		info.info.canonicalName = HeadlessApi::canonicalName;
		info.info.displayName	= HeadlessApi::displayName;
		info.createInstance		= &CreateInstance<false>;
		return registry.Register<HeadlessApi>(info);
	}

	Result<void> RegisterPresenting(GraphicsApiRegistry & registry)
	{
		BackendCreateInfo info{};
		info.info.canonicalName	   = PresentingApi::canonicalName;
		info.info.displayName	   = PresentingApi::displayName;
		info.info.supportsSurfaces = true;
		info.createInstance		   = &CreateInstance<true>;
		return registry.Register<PresentingApi>(info);
	}

	std::size_t HeadlessEntryCount() noexcept
	{
		return detail::BlockEntries<InstanceApi>::kNames.size() + detail::BlockEntries<CoreDeviceApi>::kNames.size() +
			   detail::BlockEntries<QueueApi>::kNames.size() + detail::BlockEntries<CommandPoolApi>::kNames.size() +
			   detail::BlockEntries<DescriptorArenaApi>::kNames.size() + detail::BlockEntries<RenderCommandApi>::kNames.size();
	}

	std::size_t PresentingEntryCount() noexcept
	{
		return HeadlessEntryCount() + detail::BlockEntries<PresentApi>::kNames.size() + detail::BlockEntries<SwapchainApi>::kNames.size();
	}

	std::size_t LiveObjectCount() noexcept
	{
		return g_live.load(std::memory_order_relaxed);
	}

} // namespace azo::rhi::test::minimal

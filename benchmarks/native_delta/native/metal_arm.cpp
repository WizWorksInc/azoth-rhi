// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#include "azoth/rhi/core/result.hpp"
#include "azoth/rhi/native/metal_native.hpp"
#include "azoth/rhi/resources/descriptors.hpp"
#include "azoth/rhi/resources/native_slot.hpp"

#include "native_delta/native/backend_arms.hpp"
#include "shared/options.hpp"

#include <Metal/Metal.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <print>
#include <string_view>

namespace rhi = azo::rhi;

namespace bench::native
{

	namespace
	{

		struct Arm final
		{
			MTL::Buffer * buffer				= nullptr;
			MTL::RenderPipelineState * pipeline = nullptr;
			bool hasPipeline					= false;

			bool four					= false;
			MTL4::Compiler * compiler	= nullptr;
			MTL::Buffer * pushConstants = nullptr;
		};

		Arm g_arm;

		[[nodiscard]] MTL::RenderPipelineState * BuildMetalPipeline(MTL::Device * device)
		{
			const NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

			NS::SharedPtr<NS::String> source	= NS::TransferPtr(NS::String::alloc()->init(kMetalSource.data(), NS::UTF8StringEncoding));
			NS::Error * compileError			= nullptr;
			NS::SharedPtr<MTL::Library> library = NS::TransferPtr(device->newLibrary(source.get(), nullptr, &compileError));
			if (library.get() == nullptr)
			{
				return nullptr;
			}

			NS::SharedPtr<NS::String> vertexName	= NS::TransferPtr(NS::String::alloc()->init("vertexMain", NS::UTF8StringEncoding));
			NS::SharedPtr<NS::String> fragmentName	= NS::TransferPtr(NS::String::alloc()->init("fragmentMain", NS::UTF8StringEncoding));
			NS::SharedPtr<MTL::Function> vertexFn	= NS::TransferPtr(library->newFunction(vertexName.get()));
			NS::SharedPtr<MTL::Function> fragmentFn = NS::TransferPtr(library->newFunction(fragmentName.get()));
			if (vertexFn.get() == nullptr || fragmentFn.get() == nullptr)
			{
				return nullptr;
			}

			NS::SharedPtr<MTL::RenderPipelineDescriptor> descriptor = NS::TransferPtr(MTL::RenderPipelineDescriptor::alloc()->init());
			descriptor->setVertexFunction(vertexFn.get());
			descriptor->setFragmentFunction(fragmentFn.get());
			descriptor->colorAttachments()->object(0)->setPixelFormat(MTL::PixelFormatRGBA8Unorm);

			NS::Error * pipelineError = nullptr;
			return device->newRenderPipelineState(descriptor.get(), &pipelineError);
		}

		[[nodiscard]] MTL::RenderPipelineState * BuildMetal4Pipeline(MTL::Device * device, MTL4::Compiler *& outCompiler)
		{
			const NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

			const NS::SharedPtr<NS::String> source	  = NS::TransferPtr(NS::String::alloc()->init(kMetalSource.data(), NS::UTF8StringEncoding));
			NS::Error * compileError				  = nullptr;
			const NS::SharedPtr<MTL::Library> library = NS::TransferPtr(device->newLibrary(source.get(), nullptr, &compileError));
			if (library.get() == nullptr)
			{
				return nullptr;
			}

			const NS::SharedPtr<MTL4::CompilerDescriptor> compilerDesc = NS::TransferPtr(MTL4::CompilerDescriptor::alloc()->init());

			NS::Error * compilerError = nullptr;
			outCompiler				  = device->newCompiler(compilerDesc.get(), &compilerError);
			if (outCompiler == nullptr)
			{
				return nullptr;
			}

			const auto functionFor = [&library](const char * name)
			{
				const NS::SharedPtr<NS::String> text			  = NS::TransferPtr(NS::String::alloc()->init(name, NS::UTF8StringEncoding));
				NS::SharedPtr<MTL4::LibraryFunctionDescriptor> fn = NS::TransferPtr(MTL4::LibraryFunctionDescriptor::alloc()->init());
				fn->setLibrary(library.get());
				fn->setName(text.get());
				return fn;
			};

			const NS::SharedPtr<MTL4::LibraryFunctionDescriptor> vertexFn	= functionFor("vertexMain");
			const NS::SharedPtr<MTL4::LibraryFunctionDescriptor> fragmentFn = functionFor("fragmentMain");

			const NS::SharedPtr<MTL4::RenderPipelineDescriptor> descriptor = NS::TransferPtr(MTL4::RenderPipelineDescriptor::alloc()->init());
			descriptor->setVertexFunctionDescriptor(vertexFn.get());
			descriptor->setFragmentFunctionDescriptor(fragmentFn.get());
			descriptor->colorAttachments()->object(0)->setPixelFormat(MTL::PixelFormatRGBA8Unorm);

			NS::Error * pipelineError = nullptr;
			return outCompiler->newRenderPipelineState(descriptor.get(), nullptr, &pipelineError);
		}

		[[nodiscard]] bool PrepareArm(rhi::Device device, const Workload & work)
		{
			g_arm.four = device.GetGraphicsApiId() == rhi::Metal4Api::id;

			rhi::Error error{};
			if (g_arm.four)
			{
#ifdef AZOTH_RHI_BENCH_METAL4
				rhi::NativeBuffer<rhi::Metal4Api> buffer{};
				if (!device.GetNativeBuffer<rhi::Metal4Api>(work.scratch, buffer, error))
				{
					ReportError("failed to reach the MTLBuffer behind the scratch buffer", error);
					return false;
				}

				g_arm.buffer = buffer.buffer;
#endif
			}
			else
			{
#ifdef AZOTH_RHI_BENCH_METAL3
				rhi::NativeBuffer<rhi::MetalApi> buffer{};
				if (!device.GetNativeBuffer<rhi::MetalApi>(work.scratch, buffer, error))
				{
					ReportError("failed to reach the MTLBuffer behind the scratch buffer", error);
					return false;
				}

				g_arm.buffer = buffer.buffer;
#endif
			}

			if (g_arm.buffer == nullptr)
			{
				return false;
			}

			if (work.pipeline.IsValid())
			{
				g_arm.pipeline	  = g_arm.four ? BuildMetal4Pipeline(g_arm.buffer->device(), g_arm.compiler) : BuildMetalPipeline(g_arm.buffer->device());
				g_arm.hasPipeline = g_arm.pipeline != nullptr;
			}

			if (g_arm.four)
			{
				g_arm.pushConstants = g_arm.buffer->device()->newBuffer(kPushConstantBytes, MTL::ResourceStorageModeShared);
				if (g_arm.pushConstants == nullptr)
				{
					return false;
				}
			}

			return true;
		}

		void ReleaseArm()
		{
			if (g_arm.pipeline != nullptr)
			{
				g_arm.pipeline->release();
				g_arm.pipeline = nullptr;
			}
			if (g_arm.pushConstants != nullptr)
			{
				g_arm.pushConstants->release();
				g_arm.pushConstants = nullptr;
			}
			if (g_arm.compiler != nullptr)
			{
				g_arm.compiler->release();
				g_arm.compiler = nullptr;
			}
		}

		[[nodiscard]] std::uint64_t RecordThree(const Kind kind, MTL::RenderCommandEncoder * encoder, const Workload & work, const std::size_t commands)
		{
			const MTL::Viewport viewport{
				.originX = work.viewport.x,
				.originY = work.viewport.y,
				.width	 = work.viewport.width,
				.height	 = work.viewport.height,
				.znear	 = work.viewport.minDepth,
				.zfar	 = work.viewport.maxDepth,
			};
			const MTL::ScissorRect scissor{
				.x		= static_cast<NS::UInteger>(work.scissor.x),
				.y		= static_cast<NS::UInteger>(work.scissor.y),
				.width	= work.scissor.width,
				.height = work.scissor.height,
			};
			const std::uint32_t argumentIndex = rhi::MetalArgumentBufferIndexForSet(0);

			const std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
			switch (kind)
			{
			case Kind::eSetViewport:
				for (std::size_t index = 0; index < commands; ++index)
				{
					encoder->setViewport(viewport);
				}
				break;

			case Kind::eSetScissor:
				for (std::size_t index = 0; index < commands; ++index)
				{
					encoder->setScissorRect(scissor);
				}
				break;

			case Kind::ePushConstants:
				for (std::size_t index = 0; index < commands; ++index)
				{
					encoder->setVertexBytes(work.pushConstants.data(), kPushConstantBytes, 0);
					encoder->setFragmentBytes(work.pushConstants.data(), kPushConstantBytes, 0);
				}
				break;

			case Kind::eBindDescriptorSet:
				for (std::size_t index = 0; index < commands; ++index)
				{
					encoder->setVertexBuffer(g_arm.buffer, 0, argumentIndex);
					encoder->setFragmentBuffer(g_arm.buffer, 0, argumentIndex);
					encoder->useResource(g_arm.buffer, MTL::ResourceUsageRead | MTL::ResourceUsageWrite, MTL::RenderStageVertex | MTL::RenderStageFragment);
				}
				break;

			case Kind::eSetGraphicsPipeline:
				for (std::size_t index = 0; index < commands; ++index)
				{
					encoder->setRenderPipelineState(g_arm.pipeline);
					encoder->setCullMode(MTL::CullModeNone);
					encoder->setFrontFacingWinding(MTL::WindingCounterClockwise);
					encoder->setTriangleFillMode(MTL::TriangleFillModeFill);
				}
				break;

			case Kind::eDraw:
				for (std::size_t index = 0; index < commands; ++index)
				{
					encoder->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger{ 0 }, NS::UInteger{ 3 }, NS::UInteger{ 1 }, NS::UInteger{ 0 });
				}
				break;

			case Kind::eDrawIndexed:
				for (std::size_t index = 0; index < commands; ++index)
				{
					encoder->drawIndexedPrimitives(
						MTL::PrimitiveTypeTriangle, NS::UInteger{ 3 }, MTL::IndexTypeUInt16, g_arm.buffer, 0, NS::UInteger{ 1 }, 0, 0);
				}
				break;

			case Kind::eBarrier: break;
			}
			const std::chrono::steady_clock::time_point finished = std::chrono::steady_clock::now();

			return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(finished - started).count());
		}

		[[nodiscard]] std::uint64_t RecordFour(
			const Kind kind, MTL4::RenderCommandEncoder * encoder, MTL4::ArgumentTable * table, const Workload & work, const std::size_t commands)
		{
			const MTL::Viewport viewport{
				.originX = work.viewport.x,
				.originY = work.viewport.y,
				.width	 = work.viewport.width,
				.height	 = work.viewport.height,
				.znear	 = work.viewport.minDepth,
				.zfar	 = work.viewport.maxDepth,
			};
			const MTL::ScissorRect scissor{
				.x		= static_cast<NS::UInteger>(work.scissor.x),
				.y		= static_cast<NS::UInteger>(work.scissor.y),
				.width	= work.scissor.width,
				.height = work.scissor.height,
			};

			const std::uint32_t argumentIndex	  = rhi::MetalArgumentBufferIndexForSet(0);
			const MTL::GPUAddress bufferAddress	  = g_arm.buffer->gpuAddress();
			const MTL::GPUAddress constantAddress = g_arm.pushConstants != nullptr ? g_arm.pushConstants->gpuAddress() : 0;
			void * constantContents				  = g_arm.pushConstants != nullptr ? g_arm.pushConstants->contents() : nullptr;

			const std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
			switch (kind)
			{
			case Kind::eSetViewport:
				for (std::size_t index = 0; index < commands; ++index)
				{
					encoder->setViewport(viewport);
				}
				break;

			case Kind::eSetScissor:
				for (std::size_t index = 0; index < commands; ++index)
				{
					encoder->setScissorRect(scissor);
				}
				break;

			case Kind::ePushConstants:
				for (std::size_t index = 0; index < commands; ++index)
				{
					std::memcpy(constantContents, work.pushConstants.data(), kPushConstantBytes);
					table->setAddress(constantAddress, rhi::kMetalPushConstantBufferIndex);
				}
				break;

			case Kind::eBindDescriptorSet:
				for (std::size_t index = 0; index < commands; ++index)
				{
					table->setAddress(bufferAddress, argumentIndex);
				}
				break;

			case Kind::eSetGraphicsPipeline:
				for (std::size_t index = 0; index < commands; ++index)
				{
					encoder->setRenderPipelineState(g_arm.pipeline);
					encoder->setCullMode(MTL::CullModeNone);
					encoder->setFrontFacingWinding(MTL::WindingCounterClockwise);
					encoder->setTriangleFillMode(MTL::TriangleFillModeFill);
				}
				break;

			case Kind::eDraw:
				for (std::size_t index = 0; index < commands; ++index)
				{
					encoder->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger{ 0 }, NS::UInteger{ 3 }, NS::UInteger{ 1 }, NS::UInteger{ 0 });
				}
				break;

			case Kind::eDrawIndexed:
				for (std::size_t index = 0; index < commands; ++index)
				{
					encoder->drawIndexedPrimitives(
						MTL::PrimitiveTypeTriangle, NS::UInteger{ 3 }, MTL::IndexTypeUInt16, bufferAddress, g_arm.buffer->length(), NS::UInteger{ 1 }, 0, 0);
				}
				break;

			case Kind::eBarrier:
				for (std::size_t index = 0; index < commands; ++index)
				{
					encoder->barrierAfterEncoderStages(MTL::StageFragment, MTL::StageFragment, MTL4::VisibilityOptionDevice);
					encoder->barrierAfterQueueStages(MTL::StageFragment, MTL::StageFragment, MTL4::VisibilityOptionDevice);
				}
				break;
			}
			const std::chrono::steady_clock::time_point finished = std::chrono::steady_clock::now();

			return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(finished - started).count());
		}

	}

	bool PrepareMetal(rhi::Device device, const Workload & work)
	{
		return PrepareArm(device, work);
	}

	void ReleaseMetal()
	{
		ReleaseArm();
	}

	std::string_view MetalGap(const Kind kind)
	{
		if (NeedsPipeline(kind) && !g_arm.hasPipeline)
		{
			return "no pipeline: this device takes compiled binaries and the only shader here is Metal source";
		}

		return {};
	}

	bool MetalRecordsNothing(const Kind kind)
	{
		return !g_arm.four && kind == Kind::eBarrier;
	}

	bool RecordMetal(const Kind kind, rhi::CommandList & list, const rhi::NativeMutationDesc & mutation, const Workload & work, const std::size_t commands,
		std::uint64_t & elapsed)
	{
#ifdef AZOTH_RHI_BENCH_METAL4
		if (g_arm.four)
		{
			MTL4::RenderCommandEncoder * fourEncoder = rhi::GetMetal4RenderCommandEncoder(list);
			MTL4::ArgumentTable * table				 = rhi::GetMetal4ArgumentTable(list);
			if (fourEncoder == nullptr && NeedsRenderingScope(kind))
			{
				std::println("the Metal 4 command list has no open render encoder");
				return false;
			}

			rhi::Error fourError{};
			const bool recordedFour = list.ModifyNative<rhi::Metal4Api>(
				mutation,
				[&](const rhi::native::Metal4CommandListView &)
				{
					elapsed = RecordFour(kind, fourEncoder, table, work, commands);
				},
				fourError);
			if (!recordedFour)
			{
				ReportError("the native mutation scope was refused", fourError);
			}

			return recordedFour;
		}
#endif

#ifndef AZOTH_RHI_BENCH_METAL3
		static_cast<void>(mutation);
		static_cast<void>(work);
		static_cast<void>(commands);
		std::println("this build has no Metal 3 backend, so there is no native arm for it");
		return false;
#else
		MTL::RenderCommandEncoder * encoder = rhi::GetMetalRenderCommandEncoder(list);
		if (encoder == nullptr && NeedsRenderingScope(kind))
		{
			std::println("the Metal command list has no open render encoder");
			return false;
		}

		rhi::Error error{};
		const bool recorded = list.ModifyNative<rhi::MetalApi>(
			mutation,
			[&](const rhi::native::MetalCommandListView &)
			{
				elapsed = RecordThree(kind, encoder, work, commands);
			},
			error);
		if (!recorded)
		{
			ReportError("the native mutation scope was refused", error);
		}

		return recorded;
#endif
	}

}

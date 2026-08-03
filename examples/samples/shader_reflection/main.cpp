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

/*
 * Building a pipeline layout out of the shader instead of beside it.
 *
 * Every other sample here writes its bindings twice, once in the shader and once in a DescriptorBinding array that has to agree with it, and nothing checks
 * that they do.
 *
 * Slang keeps a reflection of what it compiled, so the second copy is unnecessary. This sample builds its whole layout from that reflection, so adding a
 * resource to reflected.slang changes what runs without touching this file.
 */

#include "azoth/rhi/builders/device_builder.hpp"
#include "azoth/rhi/builders/resource_builders.hpp"
#include "azoth/rhi/commands/command.hpp"
#include "azoth/rhi/commands/sync.hpp"
#include "azoth/rhi/core/enums.hpp"
#include "azoth/rhi/core/flags.hpp"
#include "azoth/rhi/core/resource_handles.hpp"
#include "azoth/rhi/core/result.hpp"
#include "azoth/rhi/device/api_tags.hpp"
#include "azoth/rhi/device/device.hpp"
#include "azoth/rhi/device/selection.hpp"
#include "azoth/rhi/resources/descriptors.hpp"
#include "azoth/rhi/resources/pipeline.hpp"
#include "azoth/rhi/resources/resources.hpp"

#include "FW/utility/AssetPath.hpp"
#include "FW/utility/Log.hpp"
#include "FW/utility/Sample.hpp"

#include <slang-com-ptr.h>
#include <slang.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <format>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rhi = azo::rhi;

namespace
{

	constexpr std::uint32_t kElements	= 32;
	constexpr std::uint64_t kBufferSize = kElements * sizeof(float);

	// What reflected.slang's Params holds, which is the one thing here that is written on both sides. Reflection reports that a uniform block is bound at some
	// number, not what a caller is supposed to put in it.
	struct Params final
	{
		float scale = 0.0f;
		float bias	= 0.0f;
	};

	constexpr Params kParams{ .scale = 2.0f, .bias = 0.5f };

	// One binding as the shader declares it, which is what a DescriptorSetLayoutDesc is built out of.
	struct Reflected final
	{
		std::string name;
		rhi::DescriptorBinding binding{};
	};

	struct Program final
	{
		std::vector<Reflected> bindings;
		std::uint32_t pushConstantSize = 0;
		std::string entryPoint;
		std::array<std::uint32_t, 3> threadgroup{ 1, 1, 1 };
		std::vector<std::uint8_t> code;
		rhi::ShaderBinaryFormat format{};
		bool metalNames = false;
	};

	// What each backend takes, the same table the other samples keep. A backend of your own joins this and not the code below.
	struct Target final
	{
		rhi::GraphicsApiId api{};
		SlangCompileTarget slang = SLANG_TARGET_UNKNOWN;
		const char * profile	 = nullptr;
		rhi::ShaderBinaryFormat format{};
		bool keepsEntryPointName = false;
	};

	[[nodiscard]] const Target * TargetFor(const rhi::GraphicsApiId api)
	{
		static constexpr std::array targets{
			Target{ .api = rhi::VulkanApi::id, .slang = SLANG_SPIRV, .profile = "spirv_1_5", .format = rhi::ShaderBinaryFormat::eSpirV },
			Target{ .api = rhi::D3D12Api::id, .slang = SLANG_DXIL, .profile = "sm_6_0", .format = rhi::ShaderBinaryFormat::eDxil },
			Target{ .api			 = rhi::MetalApi::id,
				.slang				 = SLANG_METAL_LIB,
				.profile			 = "metallib_2_4",
				.format				 = rhi::ShaderBinaryFormat::eBackendNative,
				.keepsEntryPointName = true },
		};

		// NOLINTNEXTLINE(readability-qualified-auto): this is a pointer on libc++ and a class iterator on MSVC, and the check's fix only builds on the first.

		/*
		 * Both Metal backends take the same binary, so Metal 4 looks its target up under the Metal 3 id instead of duplicating the row. The binding ABI does
		 * not change with the generation: a metallib compiled once is bound the same way whether an encoder or an argument table does the binding.
		 */
		const rhi::GraphicsApiId target = api == rhi::Metal4Api::id ? rhi::MetalApi::id : api;

		const auto found = std::ranges::find(targets, target, &Target::api);
		return found != targets.end() ? &*found : nullptr;
	}

	/*
	 * What kind of descriptor one reflected parameter needs.
	 *
	 * Slang reports a resource's shape and its access separately, so a structured buffer read from and one written to arrive as the same shape and differ only
	 * in whether the access is read only. That distinction is the whole of the mapping: everything else is the kind.
	 */
	[[nodiscard]] bool DescriptorTypeFor(slang::TypeLayoutReflection * layout, rhi::DescriptorType & out)
	{
		switch (layout->getKind())
		{
		case slang::TypeReflection::Kind::ConstantBuffer: out = rhi::DescriptorType::eUniformBuffer; return true;

		case slang::TypeReflection::Kind::SamplerState: out = rhi::DescriptorType::eSampler; return true;

		case slang::TypeReflection::Kind::Resource: break;

		default: return false;
		}

		const SlangResourceShape shape	 = layout->getType()->getResourceShape();
		const SlangResourceAccess access = layout->getType()->getResourceAccess();
		const bool writable				 = access != SLANG_RESOURCE_ACCESS_READ;

		switch (shape & SLANG_RESOURCE_BASE_SHAPE_MASK)
		{
		case SLANG_STRUCTURED_BUFFER:
		case SLANG_BYTE_ADDRESS_BUFFER: out = rhi::DescriptorType::eStorageBuffer; return true;

		case SLANG_TEXTURE_1D:
		case SLANG_TEXTURE_2D:
		case SLANG_TEXTURE_3D:
		case SLANG_TEXTURE_CUBE: out = writable ? rhi::DescriptorType::eTextureUAV : rhi::DescriptorType::eTextureSRV; return true;

		case SLANG_TEXTURE_BUFFER: out = writable ? rhi::DescriptorType::eTexelBufferUAV : rhi::DescriptorType::eTexelBufferSRV; return true;

		default: return false;
		}
	}

	[[nodiscard]] bool Reflect(const rhi::GraphicsApiId api, Program & out, std::string & error)
	{
		const Target * target = TargetFor(api);
		if (target == nullptr)
		{
			error = "no Slang target is known for this backend";
			return false;
		}

		const std::string source = fw::util::LoadTextAsset("shader_reflection/shaders/reflected.slang", error);
		if (source.empty())
		{
			return false;
		}

		Slang::ComPtr<slang::IGlobalSession> global;
		if (SLANG_FAILED(slang::createGlobalSession(global.writeRef())))
		{
			error = "the Slang global session did not open";
			return false;
		}

		slang::TargetDesc targetDesc{};
		targetDesc.format  = target->slang;
		targetDesc.profile = global->findProfile(target->profile);

		slang::SessionDesc sessionDesc{};
		sessionDesc.targets		= &targetDesc;
		sessionDesc.targetCount = 1;

		Slang::ComPtr<slang::ISession> session;
		if (SLANG_FAILED(global->createSession(sessionDesc, session.writeRef())))
		{
			error = "the Slang session did not open";
			return false;
		}

		Slang::ComPtr<slang::IBlob> diagnostics;
		slang::IModule * module = session->loadModuleFromSourceString("reflected.slang", "reflected.slang", source.c_str(), diagnostics.writeRef());
		if (module == nullptr)
		{
			error = diagnostics != nullptr ? static_cast<const char *>(diagnostics->getBufferPointer()) : "the shader did not compile";
			return false;
		}

		Slang::ComPtr<slang::IEntryPoint> entry;
		if (SLANG_FAILED(module->findEntryPointByName("transformMain", entry.writeRef())))
		{
			error = "reflected.slang defines no transformMain";
			return false;
		}

		const std::array<slang::IComponentType *, 2> parts{ module, entry.get() };

		Slang::ComPtr<slang::IComponentType> composed;
		Slang::ComPtr<slang::IComponentType> linked;
		Slang::ComPtr<slang::IBlob> code;
		if (SLANG_FAILED(
				session->createCompositeComponentType(parts.data(), static_cast<SlangInt>(parts.size()), composed.writeRef(), diagnostics.writeRef())) ||
			SLANG_FAILED(composed->link(linked.writeRef(), diagnostics.writeRef())) ||
			SLANG_FAILED(linked->getEntryPointCode(0, 0, code.writeRef(), diagnostics.writeRef())))
		{
			error = diagnostics != nullptr ? static_cast<const char *>(diagnostics->getBufferPointer()) : "the shader did not link";
			return false;
		}

		// Everything below reads the linked program and not the source, so it sees the numbers the compiler settled on and not the ones the file asked for.
		slang::ProgramLayout * layout = linked->getLayout(0, diagnostics.writeRef());
		if (layout == nullptr)
		{
			error = "the compiled program carried no reflection";
			return false;
		}

		slang::EntryPointReflection * reflectedEntry = layout->getEntryPointByIndex(0);
		if (reflectedEntry == nullptr)
		{
			error = "the compiled program reflects no entry point";
			return false;
		}

		std::array<SlangUInt, 3> sizes{ 1, 1, 1 };
		reflectedEntry->getComputeThreadGroupSize(sizes.size(), sizes.data());
		for (std::size_t axis = 0; axis < sizes.size(); ++axis)
		{
			out.threadgroup.at(axis) = static_cast<std::uint32_t>(sizes.at(axis));
		}

		out.entryPoint = reflectedEntry->getName() != nullptr ? reflectedEntry->getName() : "transformMain";

		const unsigned count = layout->getParameterCount();
		for (unsigned index = 0; index < count; ++index)
		{
			slang::VariableLayoutReflection * parameter	  = layout->getParameterByIndex(index);
			slang::TypeLayoutReflection * parameterLayout = parameter->getTypeLayout();

			/*
			 * A push constant is a range and not a binding, so its size is what the layout needs and it has no descriptor type at all.
			 *
			 * Named by kind as well as by category because the two targets do not agree: compiling for Metal reports a push constant and a ParameterBlock in
			 * the same constant buffer category, and only the kind tells them apart. A loose ConstantBuffer at global scope is the push constant block under
			 * this ABI either way.
			 */
			if (parameter->getCategory() == slang::ParameterCategory::PushConstantBuffer ||
				parameterLayout->getKind() == slang::TypeReflection::Kind::ConstantBuffer)
			{
				out.pushConstantSize = static_cast<std::uint32_t>(parameterLayout->getElementTypeLayout()->getSize());
				continue;
			}

			/*
			 * A ParameterBlock is one parameter here and a whole set in the layout, so the bindings are its members, not itself. Their numbers are the
			 * order they were declared in, which is what a descriptor set layout is written in too.
			 */
			if (parameterLayout->getKind() == slang::TypeReflection::Kind::ParameterBlock)
			{
				slang::TypeLayoutReflection * contents = parameterLayout->getElementTypeLayout();
				for (unsigned member = 0; member < contents->getFieldCount(); ++member)
				{
					slang::VariableLayoutReflection * field = contents->getFieldByIndex(member);

					rhi::DescriptorType type{};
					if (!DescriptorTypeFor(field->getTypeLayout(), type))
					{
						error = std::format("{} is a kind this sample has no descriptor type for", field->getName());
						return false;
					}

					out.bindings.push_back(Reflected{
						.name	 = field->getName(),
						.binding = {
							.binding = member,
							.type	 = type,
							.stages	 = rhi::ShaderStage::eCompute,
						},
					});
				}

				continue;
			}

			rhi::DescriptorType type{};
			if (!DescriptorTypeFor(parameterLayout, type))
			{
				error = std::format("{} is a kind this sample has no descriptor type for", parameter->getName());
				return false;
			}

			out.bindings.push_back(Reflected{
				.name	 = parameter->getName(),
				.binding = {
					.binding = static_cast<std::uint32_t>(parameter->getBindingIndex()),
					.type	 = type,
					.stages	 = rhi::ShaderStage::eCompute,
				},
			});
		}

		const auto * bytes = static_cast<const std::uint8_t *>(code->getBufferPointer());

		// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic): a pointer and a length is the only shape a Slang blob comes in.
		out.code.assign(bytes, bytes + code->getBufferSize());
		out.format	   = target->format;
		out.metalNames = target->keepsEntryPointName;

		return true;
	}

	[[nodiscard]] std::string_view NameOf(const rhi::DescriptorType type)
	{
		switch (type)
		{
		case rhi::DescriptorType::eUniformBuffer: return "uniform buffer";
		case rhi::DescriptorType::eStorageBuffer: return "storage buffer";
		case rhi::DescriptorType::eSampler:		  return "sampler";
		case rhi::DescriptorType::eTextureSRV:	  return "sampled texture";
		case rhi::DescriptorType::eTextureUAV:	  return "storage texture";
		default:								  return "other";
		}
	}

} // namespace

int main(int argc, char ** argv)
{
	const std::span<char * const> args(argv, static_cast<std::size_t>(argc));
	// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access): the size test on this same line is the bounds check.
	const char * requested = args.size() > 1 ? args[1] : nullptr;

	rhi::BackendSelection backends{ rhi::BackendPreference{ .requested = requested, .includeNull = false } };
	const rhi::Result<rhi::UniqueDevice> device = rhi::DeviceBuilder()
													  .DebugName("shader_reflection")
													  .Validation(rhi::ValidationMode::eDeveloper)
													  .Headless()
													  .ComputeQueue()
													  .Build(backends.Registry(), backends.PreferredApis());
	if (!device)
	{
		fw::ReportError("failed to create a device", device.GetError());
		// 77 is what ctest reads as a skip: no driver here is not a failure of the sample.
		return 77;
	}

	rhi::Device dev = device.Value().Get();
	LOG_INFO(fw::Log(), "backend: {}", dev.GetGraphicsApiName());

	Program program;
	std::string why;
	if (!Reflect(dev.GetGraphicsApiId(), program, why))
	{
		LOG_ERROR(fw::Log(), "{}", why);
		return 77;
	}

	LOG_INFO(fw::Log(),
		"{} takes a group of {}x{}x{}, {} bytes of push constants and {} bindings, none of them written here:",
		program.entryPoint,
		program.threadgroup.at(0),
		program.threadgroup.at(1),
		program.threadgroup.at(2),
		program.pushConstantSize,
		program.bindings.size());

	for (const Reflected & reflected : program.bindings)
	{
		LOG_INFO(fw::Log(), "      binding {} is {}, a {}", reflected.binding.binding, reflected.name, NameOf(reflected.binding.type));
	}

	rhi::Error error{};

	// The whole point: the layout is the reflection, not a second copy of it.
	std::vector<rhi::DescriptorBinding> bindings;
	bindings.reserve(program.bindings.size());
	for (const Reflected & reflected : program.bindings)
	{
		bindings.push_back(reflected.binding);
	}

	const rhi::DescriptorSetLayoutHandle setLayout =
		dev.CreateDescriptorSetLayout(rhi::DescriptorSetLayoutDesc{ .bindings = bindings, .debugName = "reflection.set" }, error);

	const std::array setLayouts{ setLayout };
	// The range the shader reported, so the layout reserves exactly what it asks for and not a size written here.
	const std::array pushConstants{
		rhi::PushConstantRange{ .stages = rhi::ShaderStage::eCompute, .offset = 0, .size = program.pushConstantSize },
	};

	const rhi::PipelineLayoutHandle layout =
		dev.CreatePipelineLayout(rhi::PipelineLayoutDesc{ .sets = setLayouts, .pushConstants = pushConstants, .debugName = "reflection.layout" }, error);

	const rhi::ShaderBinary binary{
		.stage			 = rhi::ShaderStage::eCompute,
		.format			 = program.format,
		.data			 = program.code.data(),
		.size			 = program.code.size(),
		.entryPoint		 = program.metalNames ? program.entryPoint.c_str() : "main",
		.threadgroupSize = { .x = program.threadgroup.at(0), .y = program.threadgroup.at(1), .z = program.threadgroup.at(2) },
	};

	const rhi::ComputePipelineHandle pipeline =
		dev.CreateComputePipeline(rhi::ComputePipelineDesc{ .layout = layout, .shader = binary, .debugName = "reflection.pipeline" }, error);

	if (!setLayout.IsValid() || !layout.IsValid() || !pipeline.IsValid())
	{
		fw::ReportError("the reflected layout did not build a pipeline", error);
		return 1;
	}

	rhi::BufferBuilder inputDesc;
	inputDesc.Size(kBufferSize).Usage(rhi::BufferUsage::eStorage).CpuUpload().DebugName("reflection.input");
	const rhi::BufferHandle input = dev.CreateBuffer(inputDesc.Build(), error);

	rhi::BufferBuilder outputDesc;
	outputDesc.Size(kBufferSize).GpuOnly().DebugName("reflection.output");
	outputDesc.Usage(rhi::Flags(rhi::BufferUsage::eStorage) | rhi::BufferUsage::eCopySrc);
	const rhi::BufferHandle output = dev.CreateBuffer(outputDesc.Build(), error);

	rhi::BufferBuilder readDesc;
	readDesc.Size(kBufferSize).Usage(rhi::BufferUsage::eCopyDst).CpuReadback().DebugName("reflection.readback");
	const rhi::BufferHandle readback = dev.CreateBuffer(readDesc.Build(), error);

	if (!input.IsValid() || !output.IsValid() || !readback.IsValid())
	{
		fw::ReportError("the buffers were refused", error);
		return 1;
	}

	const rhi::MappedMemory mappedInput = dev.Map(input, rhi::MapDesc{ .mode = rhi::MapMode::eWrite }, error);
	if (mappedInput.data == nullptr)
	{
		fw::ReportError("the upload could not be mapped", error);
		return 1;
	}

	std::array<float, kElements> feed{};
	for (std::uint32_t index = 0; index < kElements; ++index)
	{
		feed.at(index) = static_cast<float>(index);
	}

	std::memcpy(mappedInput.data, feed.data(), kBufferSize);

	if (!dev.Unmap(input, error))
	{
		fw::ReportError("the upload could not be unmapped", error);
		return 1;
	}

	rhi::DescriptorArena arena = dev.CreateDescriptorArena(
		rhi::DescriptorArenaDesc{
			.type			= rhi::DescriptorArenaType::ePersistent,
			.maxSets		= 1,
			.maxDescriptors = static_cast<std::uint32_t>(bindings.size()),
			.debugName		= "reflection.arena",
		},
		error);

	const rhi::DescriptorSetHandle set = arena.Allocate(rhi::DescriptorSetAllocDesc{ .layout = setLayout, .debugName = "reflection.descriptors" }, error);

	// Written by the number the shader reported and not by a number written here, which is what keeps this from being the second copy again.
	std::vector<rhi::DescriptorWriteBuffer> writes;
	writes.reserve(program.bindings.size());
	for (const Reflected & reflected : program.bindings)
	{
		writes.push_back(rhi::DescriptorWriteBuffer{
			.set	 = set,
			.binding = reflected.binding.binding,
			.type	 = reflected.binding.type,
			.buffer	 = reflected.name == "output" ? output : input,
			.range	 = kBufferSize,
		});
	}

	if (!set.IsValid() || !dev.UpdateDescriptors(std::span(writes), error))
	{
		fw::ReportError("the descriptors were refused", error);
		return 1;
	}

	const rhi::TimelineHandle timeline = dev.CreateTimeline(rhi::TimelineDesc{ .debugName = "reflection.timeline" }, error);
	rhi::Queue queue				   = dev.GetQueue(rhi::QueueType::eCompute, 0, error);
	rhi::CommandPool pool = dev.CreateCommandPool(rhi::CommandPoolDesc{ .queueType = rhi::QueueType::eCompute, .debugName = "reflection.pool" }, error);
	rhi::CommandList list = pool.Allocate("reflection.dispatch", error);
	if (!timeline.IsValid() || !queue.IsValid() || !list.IsValid() || !list.Begin(error))
	{
		fw::ReportError("the submission objects were refused", error);
		return 1;
	}

	const std::array intoShaderWrite{
		rhi::BufferBarrier{
			.buffer = output,
			.before = { .stages = rhi::PipelineStage::eNone, .access = rhi::Access::eNone },
			.after	= { .stages = rhi::PipelineStage::eComputeShader, .access = rhi::Access::eShaderWrite },
		},
	};

	const std::array afterDispatch{
		rhi::BufferBarrier{
			.buffer = output,
			.before = { .stages = rhi::PipelineStage::eComputeShader, .access = rhi::Access::eShaderWrite },
			.after	= { .stages = rhi::PipelineStage::eCopy, .access = rhi::Access::eCopyRead },
		},
	};

	// One group, since the reflected size covers the whole run.
	const bool recorded =
		list.Barriers(rhi::BarrierBatch{ .buffers = intoShaderWrite }, error) && list.SetComputePipeline(pipeline, error) &&
		list.BindDescriptorSet(layout, 0, set, {}, error) && list.PushConstants(layout, rhi::ShaderStage::eCompute, 0, sizeof(kParams), &kParams, error) &&
		list.Dispatch(kElements / program.threadgroup.at(0), 1, 1, error) && list.Barriers(rhi::BarrierBatch{ .buffers = afterDispatch }, error) &&
		list.CopyBuffer(readback, 0, output, 0, kBufferSize, error) && list.End(error);

	if (!recorded)
	{
		fw::ReportError("recording failed", error);
		return 1;
	}

	std::array<const rhi::CommandList *, 1> lists{ &list };
	const std::array signals{ rhi::TimelinePoint{ .timeline = timeline, .value = 1 } };
	constexpr std::uint64_t kNoTimeout = std::numeric_limits<std::uint64_t>::max();
	if (!queue.Submit(rhi::SubmitDesc{ .commandLists = lists, .signals = signals, .debugName = "reflection.submit" }, error) ||
		!queue.Wait(timeline, 1, kNoTimeout, error))
	{
		fw::ReportError("the dispatch did not complete", error);
		return 1;
	}

	const rhi::MappedMemory mapped = dev.Map(readback, rhi::MapDesc{ .mode = rhi::MapMode::eRead }, error);
	if (mapped.data == nullptr)
	{
		fw::ReportError("the readback could not be mapped", error);
		return 1;
	}

	std::array<float, kElements> results{};
	std::memcpy(results.data(), mapped.data, kBufferSize);
	static_cast<void>(dev.Unmap(readback, error));

	// The arithmetic the shader was told to do, checked so a layout that bound the right count of the wrong things still fails.
	std::uint32_t wrong = 0;
	for (std::uint32_t index = 0; index < kElements; ++index)
	{
		const float expected = (static_cast<float>(index) * kParams.scale) + kParams.bias;
		if (std::abs(results.at(index) - expected) > 0.001f)
		{
			wrong += 1;
		}
	}

	if (wrong != 0)
	{
		LOG_ERROR(fw::Log(), "{} of {} elements came back wrong, so the reflected layout does not describe what ran", wrong, kElements);
		return 1;
	}

	LOG_INFO(fw::Log(), "all {} elements match, so the layout read off the shader is the one the shader wanted", kElements);
	return 0;
}

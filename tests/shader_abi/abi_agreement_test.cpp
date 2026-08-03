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

#include "azoth/rhi/builders/device_builder.hpp"
#include "azoth/rhi/core/result.hpp"
#include "azoth/rhi/device/api_tags.hpp"
#include "azoth/rhi/device/device.hpp"
#include "azoth/rhi/device/selection.hpp"
#include "azoth/rhi/resources/binding_abi.hpp"
#include "azoth/rhi/resources/native_slot.hpp"

#include <gtest/gtest.h>
#include <slang-com-ptr.h>
#include <slang.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace rhi = azo::rhi;

namespace
{

	constexpr const char * kShaderSource = R"SLANG(
struct Push { float4 tint; };
[[vk::push_constant]] ConstantBuffer<Push> gPush;

struct FrameSet
{
    ConstantBuffer<float4> frame;
    Texture2D<float4> albedo;
    SamplerState samp;
};

struct MaterialSet
{
    ConstantBuffer<float4> material;
    Texture2D<float4> normal;
};

ParameterBlock<FrameSet> gFrameSet;
ParameterBlock<MaterialSet> gMaterialSet;

[shader("fragment")]
float4 fragmentMain() : SV_Target
{
    return gPush.tint * gFrameSet.frame * gMaterialSet.material
         + gFrameSet.albedo.Sample(gFrameSet.samp, float2(0, 0))
         + gMaterialSet.normal.Load(int3(0, 0, 0));
}
)SLANG";

	constexpr const char * kVertexSource = R"SLANG(
struct VertexInput
{
    float3 position : TEXCOORD0;
    float3 normal   : TEXCOORD1;
    float2 uv       : TEXCOORD2;
};

[shader("vertex")]
float4 vertexMain(VertexInput input) : SV_Position
{
    return float4(input.position + input.normal, input.uv.x);
}

[shader("fragment")]
float4 fragmentMain() : SV_Target
{
    return float4(1, 1, 1, 1);
}
)SLANG";

	constexpr const char * kComputeBlockSource = R"SLANG(
struct Params { uint size; };

[[vk::push_constant]]
ConstantBuffer<Params> gParams;

struct WorkSet
{
    RWTexture2DArray<float4> output;
    TextureCube environment;
    SamplerState samp;
};

ParameterBlock<WorkSet> gWork;

[shader("compute")]
[numthreads(8, 8, 1)]
void skyMain(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= gParams.size) { return; }
    gWork.output[id] = gWork.environment.SampleLevel(gWork.samp, float3(1, 0, 0), 0.0);
}
)SLANG";

	struct Layout final
	{
		std::array<rhi::PushConstantRange, 1> pushConstants{
			rhi::PushConstantRange{ .stages = rhi::ShaderStage::eFragment, .offset = 0, .size = 16 },
		};

		std::array<rhi::DescriptorBinding, 3> set0{
			rhi::DescriptorBinding{ .binding = 0, .type = rhi::DescriptorType::eUniformBuffer },
			rhi::DescriptorBinding{ .binding = 1, .type = rhi::DescriptorType::eTextureSRV },
			rhi::DescriptorBinding{ .binding = 2, .type = rhi::DescriptorType::eSampler },
		};

		std::array<rhi::DescriptorBinding, 2> set1{
			rhi::DescriptorBinding{ .binding = 0, .type = rhi::DescriptorType::eUniformBuffer },
			rhi::DescriptorBinding{ .binding = 1, .type = rhi::DescriptorType::eTextureSRV },
		};

		std::array<rhi::DescriptorSetLayoutDesc, 2> sets{
			rhi::DescriptorSetLayoutDesc{ .bindings = set0 },
			rhi::DescriptorSetLayoutDesc{ .bindings = set1 },
		};

		[[nodiscard]] rhi::ShaderAbiLayout Abi() const noexcept
		{
			return rhi::ShaderAbiLayout{ .sets = sets, .pushConstants = pushConstants };
		}
	};

	struct Reflected final
	{
		std::uint32_t space = 0;
		std::uint32_t index = 0;
		bool found			= false;
	};

	class SlangSession final
	{
	public:
		[[nodiscard]] bool Open(const SlangCompileTarget target, const char * profile, std::string & error)
		{
			if (SLANG_FAILED(slang::createGlobalSession(m_global.writeRef())))
			{
				error = "could not start Slang";
				return false;
			}

			slang::TargetDesc targetDesc{};
			targetDesc.format  = target;
			targetDesc.profile = m_global->findProfile(profile);

			slang::SessionDesc sessionDesc{};
			sessionDesc.targets		= &targetDesc;
			sessionDesc.targetCount = 1;

			if (SLANG_FAILED(m_global->createSession(sessionDesc, m_session.writeRef())))
			{
				error = std::string("could not open a Slang session for ") + profile;
				return false;
			}

			return true;
		}

		[[nodiscard]] Reflected Locate(const std::uint32_t blockIndex, const std::uint32_t memberIndex, std::string & error)
		{
			Slang::ComPtr<slang::IBlob> diagnostics;
			slang::IModule * module = m_session->loadModuleFromSourceString("abi", "abi", kShaderSource, diagnostics.writeRef());
			if (module == nullptr)
			{
				error = diagnostics != nullptr ? static_cast<const char *>(diagnostics->getBufferPointer()) : "the shader did not compile";
				return {};
			}

			Slang::ComPtr<slang::IEntryPoint> entry;
			if (SLANG_FAILED(module->findEntryPointByName("fragmentMain", entry.writeRef())))
			{
				error = "the shader has no fragmentMain";
				return {};
			}

			const std::array<slang::IComponentType *, 2> parts{ module, entry.get() };

			Slang::ComPtr<slang::IComponentType> composed;
			Slang::ComPtr<slang::IComponentType> linked;
			if (SLANG_FAILED(
					m_session->createCompositeComponentType(parts.data(), static_cast<SlangInt>(parts.size()), composed.writeRef(), diagnostics.writeRef())) ||
				SLANG_FAILED(composed->link(linked.writeRef(), diagnostics.writeRef())))
			{
				error = "the shader did not link";
				return {};
			}

			slang::ProgramLayout * layout = linked->getLayout(0, diagnostics.writeRef());
			if (layout == nullptr)
			{
				error = "no reflection came back";
				return {};
			}

			if (blockIndex >= layout->getParameterCount())
			{
				error = "the shader declared fewer parameters than the test expects";
				return {};
			}

			slang::VariableLayoutReflection * block = layout->getParameterByIndex(blockIndex);
			slang::TypeLayoutReflection * blockType = block->getTypeLayout();
			slang::TypeLayoutReflection * inner		= blockType->getElementTypeLayout();
			if (inner == nullptr || memberIndex >= inner->getFieldCount())
			{
				error = "the block has no such member";
				return {};
			}

			slang::VariableLayoutReflection * member = inner->getFieldByIndex(memberIndex);

			return Reflected{
				.space = static_cast<std::uint32_t>(block->getBindingIndex()),
				.index = static_cast<std::uint32_t>(member->getBindingIndex()),
				.found = true,
			};
		}

		[[nodiscard]] bool LocateVertexSemantic(const std::uint32_t inputIndex, std::string & name, std::uint32_t & index, std::string & error)
		{
			Slang::ComPtr<slang::IBlob> diagnostics;
			slang::IModule * module = m_session->loadModuleFromSourceString("vertexAbi", "vertexAbi", kVertexSource, diagnostics.writeRef());
			if (module == nullptr)
			{
				error = diagnostics != nullptr ? static_cast<const char *>(diagnostics->getBufferPointer()) : "the shader did not compile";
				return false;
			}

			Slang::ComPtr<slang::IEntryPoint> entry;
			if (SLANG_FAILED(module->findEntryPointByName("vertexMain", entry.writeRef())))
			{
				error = "the shader has no vertexMain";
				return false;
			}

			const std::array<slang::IComponentType *, 2> parts{ module, entry.get() };

			Slang::ComPtr<slang::IComponentType> composed;
			Slang::ComPtr<slang::IComponentType> linked;
			if (SLANG_FAILED(
					m_session->createCompositeComponentType(parts.data(), static_cast<SlangInt>(parts.size()), composed.writeRef(), diagnostics.writeRef())) ||
				SLANG_FAILED(composed->link(linked.writeRef(), diagnostics.writeRef())))
			{
				error = "the shader did not link";
				return false;
			}

			slang::ProgramLayout * layout = linked->getLayout(0, diagnostics.writeRef());
			if (layout == nullptr)
			{
				error = "no reflection came back";
				return false;
			}

			slang::EntryPointReflection * reflected = layout->getEntryPointByIndex(0);
			if (reflected == nullptr || reflected->getParameterCount() == 0)
			{
				error = "the entry point reflected no parameters";
				return false;
			}

			slang::TypeLayoutReflection * inputs = reflected->getParameterByIndex(0)->getTypeLayout();
			if (inputs == nullptr || inputIndex >= inputs->getFieldCount())
			{
				error = "the vertex input struct has no such field";
				return false;
			}

			slang::VariableLayoutReflection * field = inputs->getFieldByIndex(inputIndex);
			const char * semantic					= field->getSemanticName();
			if (semantic == nullptr)
			{
				error = "the field carries no semantic";
				return false;
			}

			name  = semantic;
			index = static_cast<std::uint32_t>(field->getSemanticIndex());
			return true;
		}

		[[nodiscard]] bool CompileEntryPoint(
			const char * source, const char * moduleName, const char * entryPointName, std::vector<std::uint8_t> & out, std::string & error)
		{
			Slang::ComPtr<slang::IBlob> diagnostics;
			slang::IModule * module = m_session->loadModuleFromSourceString(moduleName, moduleName, source, diagnostics.writeRef());
			if (module == nullptr)
			{
				error = diagnostics != nullptr ? static_cast<const char *>(diagnostics->getBufferPointer()) : "the shader did not compile";
				return false;
			}

			Slang::ComPtr<slang::IEntryPoint> entry;
			if (SLANG_FAILED(module->findEntryPointByName(entryPointName, entry.writeRef())))
			{
				error = std::string("the shader has no ") + entryPointName;
				return false;
			}

			const std::array<slang::IComponentType *, 2> parts{ module, entry.get() };

			Slang::ComPtr<slang::IComponentType> composed;
			Slang::ComPtr<slang::IComponentType> linked;
			Slang::ComPtr<slang::IBlob> code;
			if (SLANG_FAILED(
					m_session->createCompositeComponentType(parts.data(), static_cast<SlangInt>(parts.size()), composed.writeRef(), diagnostics.writeRef())) ||
				SLANG_FAILED(composed->link(linked.writeRef(), diagnostics.writeRef())) ||
				SLANG_FAILED(linked->getEntryPointCode(0, 0, code.writeRef(), diagnostics.writeRef())))
			{
				error = diagnostics != nullptr ? static_cast<const char *>(diagnostics->getBufferPointer()) : "the shader did not compile";
				return false;
			}

			const auto * bytes = static_cast<const std::uint8_t *>(code->getBufferPointer());
			out.assign(bytes, bytes + code->getBufferSize());
			return true;
		}

	private:
		Slang::ComPtr<slang::IGlobalSession> m_global;
		Slang::ComPtr<slang::ISession> m_session;
	};

	constexpr std::uint32_t kFrameBlock	   = 1;
	constexpr std::uint32_t kMaterialBlock = 2;

	struct Case final
	{
		const char * what			= "";
		std::uint32_t block			= 0;
		std::uint32_t member		= 0;
		std::uint32_t set			= 0;
		std::uint32_t binding		= 0;
		bool wantSamplerSlotInstead = false;
	};

	constexpr std::array kCases{
		Case{ .what = "set 0 uniform buffer", .block = kFrameBlock, .member = 0, .set = 0, .binding = 0 },
		Case{ .what = "set 0 texture", .block = kFrameBlock, .member = 1, .set = 0, .binding = 1 },
		Case{ .what = "set 0 sampler", .block = kFrameBlock, .member = 2, .set = 0, .binding = 2 },
		Case{ .what = "set 1 uniform buffer", .block = kMaterialBlock, .member = 0, .set = 1, .binding = 0 },
		Case{ .what = "set 1 texture", .block = kMaterialBlock, .member = 1, .set = 1, .binding = 1 },
	};

	TEST(ShaderAbiAgreement, Direct3D12RegistersMatchWhatSlangEmits)
	{
		SlangSession session;
		std::string error;
		ASSERT_TRUE(session.Open(SLANG_DXIL, "sm_6_0", error)) << error;

		const Layout layout;
		for (const Case & entry : kCases)
		{
			const Reflected actual = session.Locate(entry.block, entry.member, error);
			ASSERT_TRUE(actual.found) << entry.what << ": " << error;

			const rhi::NativeBinding expected = rhi::NativeBindingFor(rhi::D3D12Api::id, rhi::BindingTier::eBasic, layout.Abi(), entry.set, entry.binding);
			ASSERT_TRUE(expected.exists) << entry.what;

			EXPECT_EQ(actual.space, expected.resource.space) << entry.what << ": Slang and the published ABI disagree on the register space";
			EXPECT_EQ(actual.index, expected.resource.index) << entry.what << ": Slang and the published ABI disagree on the register";
		}
	}

	TEST(ShaderAbiAgreement, VulkanSetsAndBindingsMatchWhatSlangEmits)
	{
		SlangSession session;
		std::string error;
		ASSERT_TRUE(session.Open(SLANG_SPIRV, "spirv_1_5", error)) << error;

		const Layout layout;
		for (const Case & entry : kCases)
		{
			const Reflected actual = session.Locate(entry.block, entry.member, error);
			ASSERT_TRUE(actual.found) << entry.what << ": " << error;

			const rhi::NativeBinding expected = rhi::NativeBindingFor(rhi::VulkanApi::id, rhi::BindingTier::eBasic, layout.Abi(), entry.set, entry.binding);
			ASSERT_TRUE(expected.exists) << entry.what;

			EXPECT_EQ(actual.space, expected.resource.space) << entry.what << ": Slang and the published ABI disagree on the descriptor set";
			EXPECT_EQ(actual.index, expected.resource.index) << entry.what << ": Slang and the published ABI disagree on the binding";
		}
	}

	TEST(ShaderAbiAgreement, AComputeEntryPointSurvivesAParameterBlockThroughTheApiPath)
	{
		SlangSession session;
		std::string error;
		ASSERT_TRUE(session.Open(SLANG_METAL_LIB, "metallib_2_4", error)) << error;

		// A control compiled first, since emitting a metallib needs Metal's compiler downstream of Slang. Failing it means the host cannot answer, not no.
		constexpr const char * kControl = R"SLANG(
[[vk::binding(0, 0)]]
RWTexture2DArray<float4> gOutput;

[shader("compute")]
[numthreads(8, 8, 1)]
void skyMain(uint3 id : SV_DispatchThreadID)
{
    gOutput[id] = float4(1, 0, 0, 1);
}
)SLANG";

		std::vector<std::uint8_t> control;
		if (!session.CompileEntryPoint(kControl, "computeControl", "skyMain", control, error))
		{
			GTEST_SKIP() << "this host has no Metal compiler for Slang to hand a metallib off to: " << error;
		}

		std::vector<std::uint8_t> code;
		ASSERT_TRUE(session.CompileEntryPoint(kComputeBlockSource, "computeBlock", "skyMain", code, error))
			<< "a ParameterBlock compute shader would not build through the Slang API, while the same shape without one did: " << error;
		EXPECT_FALSE(code.empty()) << "the metallib came back empty";
	}

	TEST(ShaderAbiAgreement, MetalArgumentBufferMembersMatchWhatSlangEmits)
	{
		SlangSession session;
		std::string error;
		ASSERT_TRUE(session.Open(SLANG_METAL_LIB, "metallib_2_4", error)) << error;

		const Layout layout;
		for (const Case & entry : kCases)
		{
			const Reflected actual = session.Locate(entry.block, entry.member, error);
			ASSERT_TRUE(actual.found) << entry.what << ": " << error;

			const rhi::NativeBinding expected = rhi::NativeBindingFor(rhi::MetalApi::id, rhi::BindingTier::eUnbounded, layout.Abi(), entry.set, entry.binding);
			ASSERT_TRUE(expected.exists) << entry.what << ": the ABI has no answer for a set argument buffers can address";

			EXPECT_EQ(actual.space, expected.resource.space) << entry.what << ": Slang and the published ABI disagree on the buffer index the set binds at";
			EXPECT_EQ(actual.index, expected.resource.index) << entry.what << ": Slang and the published ABI disagree on the member's place in the struct";
		}
	}

	TEST(ShaderAbiAgreement, VertexSemanticsFollowTheLocationTheAbiNames)
	{
		SlangSession session;
		std::string error;
		ASSERT_TRUE(session.Open(SLANG_DXIL, "sm_6_0", error)) << error;

		for (std::uint32_t location = 0; location < 3; ++location)
		{
			std::string name;
			std::uint32_t index = 0;
			ASSERT_TRUE(session.LocateVertexSemantic(location, name, index, error)) << "location " << location << ": " << error;

			EXPECT_EQ(name, std::string(rhi::kVertexSemanticName)) << "location " << location << " does not carry the semantic the ABI names";
			EXPECT_EQ(index, location) << "the semantic index has to be the attribute location, which is the whole of the convention";
		}
	}

	[[nodiscard]] SlangCompileTarget TargetFor(const rhi::GraphicsApiId api, const char *& profile, rhi::ShaderBinaryFormat & format)
	{
		if (api == rhi::VulkanApi::id)
		{
			profile = "spirv_1_5";
			format	= rhi::ShaderBinaryFormat::eSpirV;
			return SLANG_SPIRV;
		}

		if (api == rhi::D3D12Api::id)
		{
			profile = "sm_6_0";
			format	= rhi::ShaderBinaryFormat::eDxil;
			return SLANG_DXIL;
		}

		profile = "metallib_2_4";
		format	= rhi::ShaderBinaryFormat::eBackendNative;
		return SLANG_METAL_LIB;
	}

	struct Drawable final
	{
		rhi::Result<rhi::UniqueDevice> owner = rhi::Error{};
		rhi::Device device;
		std::vector<std::uint8_t> vertexCode;
		std::vector<std::uint8_t> fragmentCode;
		std::array<rhi::ShaderBinary, 2> shaders{};
		std::string skip;
	};

	[[nodiscard]] Drawable BringUpDrawable(const char * debugName)
	{
		Drawable out;

		rhi::BackendSelection backends{ rhi::BackendPreference{ .includeNull = false } };
		if (backends.PreferredApis().empty())
		{
			out.skip = "this build has no backend that draws";
			return out;
		}

		out.owner = rhi::DeviceBuilder().DebugName(debugName).GraphicsQueue().Build(backends.Registry(), backends.PreferredApis().first(1));
		if (!out.owner)
		{
			out.skip = "no driver here for the backend this build prefers";
			return out;
		}

		out.device					 = out.owner.Value().Get();
		const rhi::GraphicsApiId api = out.device.GetGraphicsApiId();

		const char * profile = "";
		rhi::ShaderBinaryFormat format{};
		const SlangCompileTarget target = TargetFor(api, profile, format);

		SlangSession session;
		std::string error;
		if (!session.Open(target, profile, error))
		{
			out.skip = "the Slang session would not open: " + error;
			return out;
		}

		if (!session.CompileEntryPoint(kVertexSource, "vertexAbi", "vertexMain", out.vertexCode, error) ||
			!session.CompileEntryPoint(kVertexSource, "vertexAbi", "fragmentMain", out.fragmentCode, error))
		{
			out.skip = "the vertex ABI shader would not compile: " + error;
			return out;
		}

		// Slang renames an entry point to main for SPIR-V and DXIL and keeps it for a Metal library, so the name handed over is the one it emitted.
		const bool keepsName = target == SLANG_METAL_LIB;

		out.shaders = {
			rhi::ShaderBinary{ .stage = rhi::ShaderStage::eVertex,
				.format				  = format,
				.data				  = out.vertexCode.data(),
				.size				  = out.vertexCode.size(),
				.entryPoint			  = keepsName ? "vertexMain" : "main" },
			rhi::ShaderBinary{ .stage = rhi::ShaderStage::eFragment,
				.format				  = format,
				.data				  = out.fragmentCode.data(),
				.size				  = out.fragmentCode.size(),
				.entryPoint			  = keepsName ? "fragmentMain" : "main" },
		};

		return out;
	}

	constexpr std::array<rhi::VertexAttributeDesc, 3> kVertexAbiAttributes{
		rhi::VertexAttributeDesc{ .location = 0, .binding = 0, .format = rhi::Format::eRGB32Float, .offset = 0 },
		rhi::VertexAttributeDesc{ .location = 1, .binding = 0, .format = rhi::Format::eRGB32Float, .offset = 12 },
		rhi::VertexAttributeDesc{ .location = 2, .binding = 0, .format = rhi::Format::eRG32Float, .offset = 24 },
	};
	constexpr std::array<rhi::VertexBindingDesc, 1> kVertexAbiBindings{ rhi::VertexBindingDesc{ .binding = 0, .stride = 32 } };

	TEST(ShaderAbiAgreement, APipelineWithVertexAttributesCreatesAgainstTheDeclaredSemantics)
	{
		Drawable drawable = BringUpDrawable("shader_abi.vertexSemantics");
		if (!drawable.skip.empty())
		{
			GTEST_SKIP() << drawable.skip;
		}

		rhi::Device device								 = drawable.device;
		const std::array<rhi::ShaderBinary, 2> & shaders = drawable.shaders;

		rhi::Error rhiError{};
		const rhi::PipelineLayoutHandle layout = device.CreatePipelineLayout(rhi::PipelineLayoutDesc{ .debugName = "shader_abi.layout" }, rhiError);
		ASSERT_TRUE(layout.IsValid()) << (rhiError.message != nullptr ? rhiError.message : "no diagnostic");

		rhi::VertexInputDesc vertexInput{};
		vertexInput.bindings   = kVertexAbiBindings;
		vertexInput.attributes = kVertexAbiAttributes;

		rhi::GraphicsPipelineDesc pipelineDesc{};
		pipelineDesc.layout							 = layout;
		pipelineDesc.shaders						 = shaders;
		pipelineDesc.vertexInput					 = &vertexInput;
		pipelineDesc.raster.cullMode				 = rhi::CullMode::eNone;
		pipelineDesc.renderTarget.colorFormats.at(0) = rhi::Format::eRGBA8UNorm;
		pipelineDesc.renderTarget.colorFormatCount	 = 1;
		pipelineDesc.blend.attachmentCount			 = 1;
		pipelineDesc.debugName						 = "shader_abi.vertexSemantics";

		const rhi::GraphicsPipelineHandle pipeline = device.CreateGraphicsPipeline(pipelineDesc, rhiError);
		EXPECT_TRUE(pipeline.IsValid()) << "a pipeline whose shader follows the vertex ABI was refused: "
										<< (rhiError.message != nullptr ? rhiError.message : "no diagnostic");

		if (pipeline.IsValid())
		{
			static_cast<void>(device.Destroy(pipeline, {}, rhiError));
		}
		static_cast<void>(device.Destroy(layout, {}, rhiError));
	}

	TEST(ShaderAbiAgreement, ViewportAndScissorAreDynamicWhetherOrNotThePipelineSaysSo)
	{
		Drawable drawable = BringUpDrawable("shader_abi.dynamicViewport");
		if (!drawable.skip.empty())
		{
			GTEST_SKIP() << drawable.skip;
		}

		rhi::Device device = drawable.device;
		rhi::Error rhiError{};

		const rhi::PipelineLayoutHandle layout = device.CreatePipelineLayout(rhi::PipelineLayoutDesc{ .debugName = "shader_abi.dynamicLayout" }, rhiError);
		ASSERT_TRUE(layout.IsValid()) << (rhiError.message != nullptr ? rhiError.message : "no diagnostic");

		rhi::VertexInputDesc vertexInput{};
		vertexInput.bindings   = kVertexAbiBindings;
		vertexInput.attributes = kVertexAbiAttributes;

		const auto build = [&](const rhi::Flags<rhi::DynamicState> states, const char * what)
		{
			rhi::GraphicsPipelineDesc desc{};
			desc.layout							 = layout;
			desc.shaders						 = drawable.shaders;
			desc.vertexInput					 = &vertexInput;
			desc.raster.cullMode				 = rhi::CullMode::eNone;
			desc.renderTarget.colorFormats.at(0) = rhi::Format::eRGBA8UNorm;
			desc.renderTarget.colorFormatCount	 = 1;
			desc.blend.attachmentCount			 = 1;
			desc.dynamicStates					 = states;
			desc.debugName						 = what;

			rhi::Error buildError{};
			const rhi::GraphicsPipelineHandle pipeline = device.CreateGraphicsPipeline(desc, buildError);
			EXPECT_TRUE(pipeline.IsValid()) << what << " was refused: " << (buildError.message != nullptr ? buildError.message : "no diagnostic");

			if (pipeline.IsValid())
			{
				static_cast<void>(device.Destroy(pipeline, {}, buildError));
			}
		};

		build({}, "a pipeline naming no dynamic state");
		build(rhi::Flags<rhi::DynamicState>(rhi::DynamicState::eViewport) | rhi::DynamicState::eScissor, "a pipeline naming viewport and scissor");

		static_cast<void>(device.Destroy(layout, {}, rhiError));
	}

	/*
	 * A shader that numbers its sets where this ABI does not put them is refused, not bound one index off and read as zeros.
	 *
	 * Slang leaves Metal's buffer 0 free only when a shader declares a push constant, while this ABI reserves it either way so adding a push constant range
	 * does not move every set. A shader declaring none therefore wants its set one index below where it will be bound.
	 *
	 * The refusal is under test, not the numbering.
	 */
	TEST(ShaderAbiAgreement, AShaderNumberingItsSetsElsewhereIsRefusedRatherThanBoundWrong)
	{
		constexpr const char * kNoPushConstant = R"SLANG(
struct WorkSet
{
    RWStructuredBuffer<float> values;
};

ParameterBlock<WorkSet> gWork;

[shader("compute")]
[numthreads(32, 1, 1)]
void computeMain(uint3 thread : SV_DispatchThreadID)
{
    gWork.values[thread.x] = 1.0;
}
)SLANG";

		// Metal by name, because it is the only backend where a set's place is a buffer index the RHI and the compiler have to agree on and not a number
		// carried in the binary. On the other two there is nothing here for them to disagree about.
		rhi::BackendSelection backends{ rhi::BackendPreference{ .requested = "metal", .includeNull = false } };
		if (backends.PreferredApis().empty())
		{
			GTEST_SKIP() << "this build has no Metal backend";
		}

		const rhi::Result<rhi::UniqueDevice> owner =
			rhi::DeviceBuilder().DebugName("shader_abi.setNumbering").GraphicsQueue().Build(backends.Registry(), backends.PreferredApis().first(1));
		if (!owner)
		{
			GTEST_SKIP() << "no Metal driver here";
		}

		rhi::Device device			 = owner.Value().Get();
		const rhi::GraphicsApiId api = device.GetGraphicsApiId();
		if (api != rhi::MetalApi::id)
		{
			GTEST_SKIP() << "the preferred backend is not Metal";
		}

		const char * profile = "";
		rhi::ShaderBinaryFormat format{};
		const SlangCompileTarget target = TargetFor(api, profile, format);

		SlangSession session;
		std::string error;
		ASSERT_TRUE(session.Open(target, profile, error)) << error;

		std::vector<std::uint8_t> code;
		if (!session.CompileEntryPoint(kNoPushConstant, "setNumbering", "computeMain", code, error))
		{
			GTEST_SKIP() << "this host has no Metal compiler for Slang to hand a metallib off to: " << error;
		}

		rhi::Error rhiError{};
		const std::array bindings{
			rhi::DescriptorBinding{ .binding = 0, .type = rhi::DescriptorType::eStorageBuffer, .stages = rhi::ShaderStage::eCompute },
		};

		const rhi::DescriptorSetLayoutHandle setLayout =
			device.CreateDescriptorSetLayout(rhi::DescriptorSetLayoutDesc{ .bindings = bindings, .debugName = "abi.setNumbering.set" }, rhiError);
		ASSERT_TRUE(setLayout.IsValid());

		const std::array setLayouts{ setLayout };
		const rhi::PipelineLayoutHandle layout =
			device.CreatePipelineLayout(rhi::PipelineLayoutDesc{ .sets = setLayouts, .debugName = "abi.setNumbering.layout" }, rhiError);
		ASSERT_TRUE(layout.IsValid());

		const rhi::ShaderBinary shader{
			.stage			 = rhi::ShaderStage::eCompute,
			.format			 = format,
			.data			 = code.data(),
			.size			 = code.size(),
			.entryPoint		 = "computeMain",
			.threadgroupSize = { .x = 32, .y = 1, .z = 1 },
		};

		rhi::Error buildError{};
		const rhi::ComputePipelineHandle pipeline =
			device.CreateComputePipeline(rhi::ComputePipelineDesc{ .layout = layout, .shader = shader, .debugName = "abi.setNumbering.pipeline" }, buildError);

		EXPECT_FALSE(pipeline.IsValid()) << "a shader wanting its set at an index this layout never binds was accepted, which dispatches and reads zeros";
		EXPECT_TRUE(buildError.message != nullptr) << "the refusal carried no diagnostic";

		if (pipeline.IsValid())
		{
			static_cast<void>(device.Destroy(pipeline, {}, rhiError));
		}

		static_cast<void>(device.Destroy(layout, {}, rhiError));
		static_cast<void>(device.Destroy(setLayout, {}, rhiError));
	}

} // namespace

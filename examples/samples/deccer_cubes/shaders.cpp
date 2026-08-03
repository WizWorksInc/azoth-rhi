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

#include "shaders.hpp"

#include "azoth/rhi/device/api_tags.hpp"

#include "FW/utility/AssetPath.hpp"

#include <slang-com-ptr.h>
#include <slang.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <format>
#include <string>
#include <vector>

namespace rhi = azo::rhi;

namespace deccer
{
	std::string LoadShaderSource(const char * name, std::string & error)
	{
		return fw::util::LoadTextAsset(std::filesystem::path("deccer_cubes/shaders") / name, error);
	}

	namespace
	{

		// What each backend wants its shaders in. A backend of your own joins this table and not the registration code.
		struct Target final
		{
			rhi::GraphicsApiId api{};
			SlangCompileTarget slang			 = SLANG_TARGET_UNKNOWN;
			const char * profile				 = "";
			rhi::ShaderBinaryFormat binaryFormat = rhi::ShaderBinaryFormat::eBackendNative;

			/*
			 * Whether Slang keeps the entry point's name for this target or renames it. It renames to main for SPIR-V and DXIL, where the container carries one entry
			 * point. It keeps the name for a Metal library. The RHI selects by name everywhere and needs the name the compiler emitted.
			 */
			bool keepsEntryPointName = false;
		};

		[[nodiscard]] const Target * TargetFor(const rhi::GraphicsApiId api)
		{
			static constexpr std::array targets{
				Target{ .api = rhi::VulkanApi::id, .slang = SLANG_SPIRV, .profile = "spirv_1_5", .binaryFormat = rhi::ShaderBinaryFormat::eSpirV },
				Target{ .api = rhi::D3D12Api::id, .slang = SLANG_DXIL, .profile = "sm_6_0", .binaryFormat = rhi::ShaderBinaryFormat::eDxil },
				Target{ .api			 = rhi::MetalApi::id,
					.slang				 = SLANG_METAL_LIB,
					.profile			 = "metallib_2_4",
					.binaryFormat		 = rhi::ShaderBinaryFormat::eBackendNative,
					.keepsEntryPointName = true },
			};

			// NOLINTNEXTLINE(readability-qualified-auto): libc++ makes this array iterator a raw pointer and MSVC does not, so auto * here builds on one and not the other.

			/*
			 * Both Metal backends take the same binary, so Metal 4 looks its target up under the Metal 3 id instead of duplicating the row. The binding ABI
			 * does not change with the generation: a metallib compiled once is bound the same way whether an encoder or an argument table does the binding.
			 */
			const rhi::GraphicsApiId target = api == rhi::Metal4Api::id ? rhi::MetalApi::id : api;

			const auto found = std::ranges::find(targets, target, &Target::api);
			return found != targets.end() ? &*found : nullptr;
		}

	} // namespace

	bool CanCompileFor(const rhi::GraphicsApiId api)
	{
		return TargetFor(api) != nullptr;
	}

	struct ShaderCompiler::Session final
	{
		Slang::ComPtr<slang::IGlobalSession> global;
		Slang::ComPtr<slang::ISession> session;

		// Every blob this compiler handed out, held because a ShaderBinary only borrows the bytes.
		std::vector<Slang::ComPtr<slang::IBlob>> blobs;
	};

	ShaderCompiler::ShaderCompiler()  = default;
	ShaderCompiler::~ShaderCompiler() = default;

	bool ShaderCompiler::Open(const rhi::GraphicsApiId api, std::string & error)
	{
		const Target * target = TargetFor(api);
		if (target == nullptr)
		{
			error = "this build has no backend Slang can emit for";
			return false;
		}

		auto session = std::make_unique<Session>();
		if (SLANG_FAILED(slang::createGlobalSession(session->global.writeRef())))
		{
			error = "could not start Slang";
			return false;
		}

		slang::TargetDesc targetDesc{};
		targetDesc.format  = target->slang;
		targetDesc.profile = session->global->findProfile(target->profile);

		slang::SessionDesc sessionDesc{};
		sessionDesc.targets		= &targetDesc;
		sessionDesc.targetCount = 1;

		if (SLANG_FAILED(session->global->createSession(sessionDesc, session->session.writeRef())))
		{
			error = std::format("could not open a Slang session for {}", target->profile);
			return false;
		}

		m_session			  = std::move(session);
		m_format			  = target->binaryFormat;
		m_keepsEntryPointName = target->keepsEntryPointName;
		return true;
	}

	rhi::ShaderBinary ShaderCompiler::Compile(
		const char * moduleName, const char * source, const char * entryPoint, const rhi::ShaderStage stage, std::string & error, const Threadgroup threadgroup)
	{
		Slang::ComPtr<slang::IBlob> diagnostics;
		slang::IModule * module = m_session->session->loadModuleFromSourceString(moduleName, moduleName, source, diagnostics.writeRef());
		if (module == nullptr)
		{
			error = diagnostics != nullptr ? static_cast<const char *>(diagnostics->getBufferPointer()) : "the shader did not compile";
			return {};
		}

		Slang::ComPtr<slang::IEntryPoint> entry;
		if (SLANG_FAILED(module->findEntryPointByName(entryPoint, entry.writeRef())))
		{
			error = std::format("{} has no entry point called {}", moduleName, entryPoint);
			return {};
		}

		// The module goes in beside the entry point, which on its own does not carry the file's constants.
		const std::array<slang::IComponentType *, 2> parts{ module, entry.get() };

		Slang::ComPtr<slang::IComponentType> composed;
		Slang::ComPtr<slang::IComponentType> linked;
		Slang::ComPtr<slang::IBlob> code;
		if (SLANG_FAILED(m_session->session->createCompositeComponentType(
				parts.data(), static_cast<SlangInt>(parts.size()), composed.writeRef(), diagnostics.writeRef())) ||
			SLANG_FAILED(composed->link(linked.writeRef(), diagnostics.writeRef())) ||
			SLANG_FAILED(linked->getEntryPointCode(0, 0, code.writeRef(), diagnostics.writeRef())))
		{
			error = diagnostics != nullptr ? static_cast<const char *>(diagnostics->getBufferPointer()) : "the shader did not compile";
			return {};
		}

		/*
		 * The threadgroup size travels beside the binary, not inside it. Slang drops the HLSL numthreads attribute from its MSL output and no backend can recover it
		 * from a compiled binary, so every one of them requires it stated here. Passing it on every target, not only on Metal is what keeps a run on Vulkan from
		 * hiding a size this example forgot to give.
		 */
		const rhi::ShaderBinary binary{
			.stage	= stage,
			.format = m_format,
			.data	= code->getBufferPointer(),
			.size	= code->getBufferSize(),
			// The caller's own literal, which outlives the binary. A backend selecting by name needs this and the default of "main" is never right here.
			.entryPoint		 = m_keepsEntryPointName ? entryPoint : "main",
			.threadgroupSize = stage == rhi::ShaderStage::eCompute ? rhi::ThreadgroupSize{ .x = threadgroup.x, .y = threadgroup.y, .z = threadgroup.z }
																   : rhi::ThreadgroupSize{},
		};

		m_session->blobs.push_back(std::move(code));
		return binary;
	}

} // namespace deccer

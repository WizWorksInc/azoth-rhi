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

#include "compiler.hpp"

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

namespace langs
{
	namespace
	{

		[[nodiscard]] std::string LoadShaderSource(const char * name, std::string & error)
		{
			return fw::util::LoadTextAsset(std::filesystem::path("shader_languages/shaders") / name, error);
		}

		// What each backend wants its bytecode in. A backend of your own joins this table and not the code below.
		struct Target final
		{
			rhi::GraphicsApiId api{};
			SlangCompileTarget slang = SLANG_TARGET_UNKNOWN;
			const char * profile	 = nullptr;
			rhi::ShaderBinaryFormat binaryFormat{};

			// What dxc, glslc or metal write for this backend, which is the extension a prebuilt kernel is looked for under.
			const char * binaryExtension = nullptr;

			bool keepsEntryPointName = false;
		};

		[[nodiscard]] const Target * TargetFor(const rhi::GraphicsApiId api)
		{
			static constexpr std::array targets{
				Target{ .api		 = rhi::VulkanApi::id,
					.slang			 = SLANG_SPIRV,
					.profile		 = "spirv_1_5",
					.binaryFormat	 = rhi::ShaderBinaryFormat::eSpirV,
					.binaryExtension = "spv" },
				Target{ .api		 = rhi::D3D12Api::id,
					.slang			 = SLANG_DXIL,
					.profile		 = "sm_6_0",
					.binaryFormat	 = rhi::ShaderBinaryFormat::eDxil,
					.binaryExtension = "dxil" },
				Target{ .api			 = rhi::MetalApi::id,
					.slang				 = SLANG_METAL_LIB,
					.profile			 = "metallib_2_4",
					.binaryFormat		 = rhi::ShaderBinaryFormat::eBackendNative,
					.binaryExtension	 = "metallib",
					.keepsEntryPointName = true },
			};


			/*
			 * Both Metal backends take the same binary, so Metal 4 looks its target up under the Metal 3 id instead of duplicating the row. The binding ABI
			 * does not change with the generation: a metallib compiled once is bound the same way whether an encoder or an argument table does the binding.
			 */
			const rhi::GraphicsApiId target = api == rhi::Metal4Api::id ? rhi::MetalApi::id : api;

			const auto found = std::ranges::find(targets, target, &Target::api);
			return found != targets.end() ? &*found : nullptr;
		}

	} // namespace

	struct ShaderCompiler::Session final
	{
		Slang::ComPtr<slang::IGlobalSession> global;
		Slang::ComPtr<slang::ISession> session;

		// Every blob this compiler handed out, held because a ShaderBinary only borrows the bytes.
		std::vector<Slang::ComPtr<slang::IBlob>> blobs;

		// The same, for the containers read off disk, not compiled here.
		std::vector<std::vector<std::uint8_t>> files;
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

		/*
		 * GLSL support is a property of the global session, not of a compile, because it decides whether the glsl module is there to import at all. A session without
		 * it reads a GLSL file as Slang and fails on vec2.
		 */
		const SlangGlobalSessionDesc globalDesc{ .enableGLSL = true };

		auto session = std::make_unique<Session>();
		if (SLANG_FAILED(slang::createGlobalSession(&globalDesc, session->global.writeRef())))
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
		m_binaryExtension	  = target->binaryExtension;
		m_keepsEntryPointName = target->keepsEntryPointName;
		return true;
	}

	rhi::ShaderBinary ShaderCompiler::LoadPrebuilt(const char * fileName, const char * entryPoint, const Threadgroup threadgroup, std::string & error)
	{
		const std::string container = std::format("{}.{}", fileName, m_binaryExtension);

		// An empty path is the compiler for that language not being installed, or having no way to reach this backend. Either way the build skipped it.
		const std::filesystem::path path = fw::util::AssetPath(std::filesystem::path("shader_languages/bin") / container);
		if (path.empty())
		{
			error = std::format("{} was not built, nothing here compiles that language for this backend", container);
			return {};
		}

		std::vector<std::uint8_t> bytes = fw::util::ReadFile(path);
		if (bytes.empty())
		{
			error = std::format("{} could not be read", path.string());
			return {};
		}

		m_session->files.push_back(std::move(bytes));
		const std::vector<std::uint8_t> & stored = m_session->files.back();

		return rhi::ShaderBinary{
			.stage	= rhi::ShaderStage::eCompute,
			.format = m_format,
			.data	= stored.data(),
			.size	= stored.size(),
			// What the ahead-of-time compiler was told to call it, which for glslc is main because GLSL has no way to say otherwise.
			.entryPoint		 = entryPoint,
			.threadgroupSize = rhi::ThreadgroupSize{ .x = threadgroup.x, .y = threadgroup.y, .z = threadgroup.z },
		};
	}

	rhi::ShaderBinary ShaderCompiler::Compile(
		const char * fileName, const char * entryPoint, const SourceLanguage language, const Threadgroup threadgroup, std::string & error)
	{
		if (language != SourceLanguage::eSlang)
		{
			return LoadPrebuilt(fileName, entryPoint, threadgroup, error);
		}

		const std::string source = LoadShaderSource(fileName, error);
		if (source.empty())
		{
			return {};
		}

		// The path carries the extension, which is what Slang reads the front end off. Nothing else here selects the language.
		Slang::ComPtr<slang::IBlob> diagnostics;
		slang::IModule * module = m_session->session->loadModuleFromSourceString(fileName, fileName, source.c_str(), diagnostics.writeRef());
		if (module == nullptr)
		{
			error = diagnostics != nullptr ? static_cast<const char *>(diagnostics->getBufferPointer()) : "the shader did not compile";
			return {};
		}

		/*
		 * Whatever the module declared, before anything asked by name.
		 *
		 * GLSL carries no attribute saying what main is, so a by-name lookup misses it, and asking with the stage forced synthesizes an entry point that has lost the
		 * file's local_size qualifier and dispatches one thread. Taking what the module defines keeps the size the shader asked for.
		 */
		Slang::ComPtr<slang::IEntryPoint> entry;
		const bool found = (module->getDefinedEntryPointCount() > 0 && SLANG_SUCCEEDED(module->getDefinedEntryPoint(0, entry.writeRef()))) ||
						   SLANG_SUCCEEDED(module->findEntryPointByName(entryPoint, entry.writeRef())) ||
						   SLANG_SUCCEEDED(module->findAndCheckEntryPoint(entryPoint, SLANG_STAGE_COMPUTE, entry.writeRef(), diagnostics.writeRef()));
		if (!found)
		{
			error = std::format("{} defines no entry point this can compile", fileName);
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

		const rhi::ShaderBinary binary{
			.stage	= rhi::ShaderStage::eCompute,
			.format = m_format,
			.data	= code->getBufferPointer(),
			.size	= code->getBufferSize(),
			// The caller's own literal, which outlives the binary. Slang renames on the way to some targets, so a backend selecting by name is given what it emitted,
			// not what was asked for.
			.entryPoint		 = m_keepsEntryPointName ? entryPoint : "main",
			.threadgroupSize = rhi::ThreadgroupSize{ .x = threadgroup.x, .y = threadgroup.y, .z = threadgroup.z },
		};

		m_session->blobs.push_back(std::move(code));
		return binary;
	}

} // namespace langs

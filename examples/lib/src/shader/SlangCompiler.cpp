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

#include "FW/shader/SlangCompiler.hpp"

#include "FW/utility/AssetPath.hpp"

#include <slang-com-ptr.h>
#include <slang.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace rhi = azo::rhi;

namespace fw::shader
{
	namespace
	{
		/*
		 * What one backend takes and whether it keeps the name the source gave an entry point.
		 *
		 * Slang renames to main for SPIR-V and DXIL, where the container holds one entry point and the name is spare. A Metal library holds several and keeps
		 * them, so the RHI, which selects by name everywhere, has to be handed the name that was actually emitted.
		 */
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

			/*
			 * Both Metal backends take the same binary, so Metal 4 looks its target up under the Metal 3 id instead of duplicating the row. The binding ABI
			 * does not change with the generation: a metallib compiled once is bound the same way whether an encoder or an argument table does the binding.
			 */
			const rhi::GraphicsApiId target = api == rhi::Metal4Api::id ? rhi::MetalApi::id : api;

			// NOLINTNEXTLINE(readability-qualified-auto): this is a pointer on libc++ and a class iterator on MSVC, and the check's fix only builds on the first.
			const auto found = std::ranges::find(targets, target, &Target::api);
			return found != targets.end() ? &*found : nullptr;
		}

		[[nodiscard]] std::string DiagnosticText(slang::IBlob * diagnostics, const char * fallback)
		{
			return diagnostics != nullptr ? static_cast<const char *>(diagnostics->getBufferPointer()) : fallback;
		}
	} // namespace

	/*
	 * The Slang objects and everything handed out that points into them.
	 *
	 * Modules are cached by the path they were read from so a file naming several entry points is parsed once. The blobs and the entry point names are kept
	 * because a returned ShaderBinary is a pointer into both.
	 */
	struct SlangCompiler::Session final
	{
		Slang::ComPtr<slang::IGlobalSession> global;
		Slang::ComPtr<slang::ISession> session;
		Target target{};

		// Owned by session, which outlives them. Cached so two entry points out of one file cost one parse.
		std::vector<std::pair<std::string, slang::IModule *>> modules;

		std::vector<Slang::ComPtr<slang::IBlob>> code;

		// Indirected so the strings do not move when the vector grows, a returned binary holding a pointer into one.
		std::vector<std::unique_ptr<std::string>> names;
	};

	SlangCompiler::SlangCompiler() : m_session(std::make_unique<Session>()) {}

	SlangCompiler::~SlangCompiler() = default;

	bool SlangCompiler::Open(const rhi::GraphicsApiId api, std::string & error)
	{
		const Target * target = TargetFor(api);
		if (target == nullptr)
		{
			error = "no Slang target is known for this backend";
			return false;
		}

		if (SLANG_FAILED(slang::createGlobalSession(m_session->global.writeRef())))
		{
			error = "the Slang global session did not open";
			return false;
		}

		slang::TargetDesc targetDesc{};
		targetDesc.format  = target->slang;
		targetDesc.profile = m_session->global->findProfile(target->profile);

		slang::SessionDesc sessionDesc{};
		sessionDesc.targets		= &targetDesc;
		sessionDesc.targetCount = 1;

		if (SLANG_FAILED(m_session->global->createSession(sessionDesc, m_session->session.writeRef())))
		{
			error = "the Slang session did not open";
			return false;
		}

		m_session->target = *target;
		return true;
	}

	rhi::ShaderBinary SlangCompiler::Compile(const std::filesystem::path & relative, const char * entryPoint, const rhi::ShaderStage stage, std::string & error)
	{
		if (m_session->session == nullptr)
		{
			error = "the compiler was used before Open succeeded";
			return {};
		}

		const std::string key = relative.generic_string();

		slang::IModule * module = nullptr;
		// NOLINTNEXTLINE(readability-qualified-auto): a pointer on libc++ and a class iterator on MSVC, as in TargetFor above.
		const auto cached = std::ranges::find(m_session->modules, key, &std::pair<std::string, slang::IModule *>::first);
		if (cached != m_session->modules.end())
		{
			module = cached->second;
		}
		else
		{
			const std::string source = util::LoadTextAsset(relative, error);
			if (source.empty())
			{
				return {};
			}

			Slang::ComPtr<slang::IBlob> diagnostics;
			module = m_session->session->loadModuleFromSourceString(key.c_str(), key.c_str(), source.c_str(), diagnostics.writeRef());
			if (module == nullptr)
			{
				error = DiagnosticText(diagnostics.get(), "the shader did not compile");
				return {};
			}

			m_session->modules.emplace_back(key, module);
		}

		Slang::ComPtr<slang::IEntryPoint> entry;
		if (SLANG_FAILED(module->findEntryPointByName(entryPoint, entry.writeRef())))
		{
			error = std::format("{} declares no {}", key, entryPoint);
			return {};
		}

		// The module goes in beside the entry point, which on its own does not carry the file's global scope.
		const std::array<slang::IComponentType *, 2> parts{ module, entry.get() };

		Slang::ComPtr<slang::IBlob> diagnostics;
		Slang::ComPtr<slang::IComponentType> composed;
		Slang::ComPtr<slang::IComponentType> linked;
		Slang::ComPtr<slang::IBlob> code;
		if (SLANG_FAILED(m_session->session->createCompositeComponentType(
				parts.data(), static_cast<SlangInt>(parts.size()), composed.writeRef(), diagnostics.writeRef())) ||
			SLANG_FAILED(composed->link(linked.writeRef(), diagnostics.writeRef())) ||
			SLANG_FAILED(linked->getEntryPointCode(0, 0, code.writeRef(), diagnostics.writeRef())))
		{
			error = DiagnosticText(diagnostics.get(), "the shader did not link");
			return {};
		}

		rhi::ShaderBinary binary{
			.stage		= stage,
			.format		= m_session->target.format,
			.data		= code->getBufferPointer(),
			.size		= code->getBufferSize(),
			.entryPoint = "main",
		};

		/*
		 * Read off the linked program and not the source, so it is the size the compiler settled on. Only a compute stage has one, and every backend refuses
		 * a compute stage that leaves it zero.
		 */
		if (stage == rhi::ShaderStage::eCompute)
		{
			slang::ProgramLayout * layout = linked->getLayout(0, diagnostics.writeRef());
			if (layout == nullptr)
			{
				error = DiagnosticText(diagnostics.get(), "the compiled program carried no reflection");
				return {};
			}

			if (slang::EntryPointReflection * reflected = layout->getEntryPointByIndex(0); reflected != nullptr)
			{
				std::array<SlangUInt, 3> sizes{ 1, 1, 1 };
				reflected->getComputeThreadGroupSize(sizes.size(), sizes.data());
				binary.threadgroupSize = {
					.x = static_cast<std::uint32_t>(sizes.at(0)),
					.y = static_cast<std::uint32_t>(sizes.at(1)),
					.z = static_cast<std::uint32_t>(sizes.at(2)),
				};
			}
		}

		if (m_session->target.keepsEntryPointName)
		{
			binary.entryPoint = m_session->names.emplace_back(std::make_unique<std::string>(entryPoint))->c_str();
		}

		m_session->code.push_back(std::move(code));
		return binary;
	}
} // namespace fw::shader

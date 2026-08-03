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
 * \brief Slang compiled to whatever the backend that came up takes.
 */

#include <azoth/rhi/core/enums.hpp>
#include <azoth/rhi/device/api_tags.hpp>
#include <azoth/rhi/resources/pipeline.hpp>

#include <filesystem>
#include <memory>
#include <string>

namespace fw::shader
{
	/**
	 * \brief One Slang session compiling for one backend, holding every binary it produced.
	 *
	 * The per-backend table this needs is the same in every sample that compiles a shader: SPIR-V for Vulkan, DXIL for Direct3D 12, a Metal library for Metal,
	 * and which of the three keeps the entry point's name. Written once here, and not in the RHI.
	 *
	 * \attention Every ShaderBinary points into storage this object owns, so it has to outlive the last pipeline built from one.
	 */
	class SlangCompiler final
	{
	public:
		SlangCompiler();
		SlangCompiler(const SlangCompiler &)			 = delete;
		SlangCompiler & operator=(const SlangCompiler &) = delete;
		SlangCompiler(SlangCompiler &&)					 = delete;
		SlangCompiler & operator=(SlangCompiler &&)		 = delete;
		~SlangCompiler();

		/**
		 * \brief Opens a session targeting one backend.
		 *
		 * \param api The backend that came up, which is what selects the target and the profile.
		 * \param error Set to why it failed, and left alone otherwise.
		 * \return False for a backend this has no Slang target for, or for a session Slang declined to open.
		 */
		[[nodiscard]] bool Open(azo::rhi::GraphicsApiId api, std::string & error);

		/**
		 * \brief Compiles one entry point out of a staged source, parsing a file once however many entry points it names.
		 *
		 * The threadgroup size of a compute entry point is read off the compiled program, since Metal needs it stated beside the binary and the shader already
		 * said it.
		 *
		 * \param relative Path below the assets root, such as "gpu_timing/shaders/timed.slang".
		 * \return A binary the RHI can build a pipeline from, or one whose data is null when it did not compile.
		 */
		[[nodiscard]] azo::rhi::ShaderBinary Compile(
			const std::filesystem::path & relative, const char * entryPoint, azo::rhi::ShaderStage stage, std::string & error);

	private:
		struct Session;

		std::unique_ptr<Session> m_session;
	};
} // namespace fw::shader

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

#include "azoth/rhi/core/resource_handles.hpp"
#include "azoth/rhi/device/api_tags.hpp"
#include "azoth/rhi/resources/pipeline.hpp"

#include <memory>
#include <string>
#include <vector>

namespace deccer
{

	// The [numthreads] of a compute entry point, which has to be passed, not read back. See ShaderCompiler::Compile.
	struct Threadgroup final
	{
		std::uint32_t x = 1;
		std::uint32_t y = 1;
		std::uint32_t z = 1;
	};

	/**
	 * \brief Reads one of this sample's Slang sources from the shaders staged beside the executable.
	 *
	 * The sources are files without string literals in here so they can be read, diffed and edited as shaders, and so a compile error names a line in a .slang
	 * file and not a line in a C++ raw string.
	 *
	 * \param name The file below the sample's shaders directory, such as "scene.slang".
	 * \return The source, or an empty string with the reason in error.
	 */
	[[nodiscard]] std::string LoadShaderSource(const char * name, std::string & error);

	/*
	 * One Slang session, shared by everything this sample compiles.
	 *
	 * Both the render passes and the environment build below want shaders and there is no reason for either to know which backend came up or what that backend
	 * wants its bytecode in, so that lives here once.
	 */
	class ShaderCompiler final
	{
	public:
		ShaderCompiler();
		ShaderCompiler(const ShaderCompiler &)			   = delete;
		ShaderCompiler & operator=(const ShaderCompiler &) = delete;
		ShaderCompiler(ShaderCompiler &&)				   = delete;
		ShaderCompiler & operator=(ShaderCompiler &&)	   = delete;
		~ShaderCompiler();

		// Opens a session for the backend that came up. False leaves the reason in error.
		[[nodiscard]] bool Open(azo::rhi::GraphicsApiId api, std::string & error);

		/*
		 * Compiles one entry point out of one source string. The returned ShaderBinary points into storage this compiler owns. A null data pointer means it did not
		 * compile and the reason is in error.
		 *
		 * threadgroup has to match the [numthreads] on a compute entry point and is ignored for the rest. Slang drops that attribute from the Metal source it emits.
		 * The backend reads it out of a marker comment which this prepends.
		 */
		[[nodiscard]] azo::rhi::ShaderBinary Compile(const char * moduleName, const char * source, const char * entryPoint, azo::rhi::ShaderStage stage,
			std::string & error, Threadgroup threadgroup = {});

	private:
		struct Session;

		std::unique_ptr<Session> m_session;
		bool m_keepsEntryPointName			  = false;
		azo::rhi::ShaderBinaryFormat m_format = azo::rhi::ShaderBinaryFormat::eBackendNative;
	};

	// True when this build has a backend that can draw and Slang can emit for it.
	[[nodiscard]] bool CanCompileFor(azo::rhi::GraphicsApiId api);

} // namespace deccer

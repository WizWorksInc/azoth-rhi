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

#include "azoth/rhi/device/api_tags.hpp"
#include "azoth/rhi/resources/pipeline.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace langs
{

	/*
	 * Which compiler builds a source, which is also what decides when it is built.
	 *
	 * Slang reads its own language at run time, from the path it is handed, so the extension a source is stored under is what selects its front end. The other
	 * three were built ahead of time by the compiler that language actually belongs to, and reach this as bytecode.
	 */
	enum class SourceLanguage : std::uint8_t
	{
		eSlang,
		eHlsl,
		eGlsl,
		eMsl,
	};

	// The [numthreads] of a compute entry point, which travels beside the binary, not inside it. See ShaderCompiler::Compile.
	struct Threadgroup final
	{
		std::uint32_t x = 1;
		std::uint32_t y = 1;
		std::uint32_t z = 1;
	};

	/*
	 * Where a kernel's bytecode comes from, whichever of the two ways it was produced.
	 *
	 * Slang compiles here, in process, from the source staged beside the executable. The rest were compiled by dxc, glslc or Apple's metal when the sample was
	 * built and this only reads the container that came out. Both hand back a ShaderBinary the RHI cannot tell apart: it takes bytecode and does not care which
	 * toolchain made it.
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
		 * Produces one entry point's bytecode. The returned ShaderBinary points into storage this compiler owns, and a null data pointer means it did not come out
		 * with the reason in error.
		 *
		 * A language with no compiler on this machine, or none that reaches this backend, fails here, not earlier, which is what lets the sample report the gap per
		 * language instead of refusing to start.
		 */
		[[nodiscard]] azo::rhi::ShaderBinary Compile(
			const char * fileName, const char * entryPoint, SourceLanguage language, Threadgroup threadgroup, std::string & error);

	private:
		// Reads what was compiled ahead of time, for every language that is not Slang.
		[[nodiscard]] azo::rhi::ShaderBinary LoadPrebuilt(const char * fileName, const char * entryPoint, Threadgroup threadgroup, std::string & error);

		struct Session;

		std::unique_ptr<Session> m_session;

		// What this backend's ahead-of-time container is named, which is how a prebuilt kernel is found.
		const char * m_binaryExtension = nullptr;

		bool m_keepsEntryPointName			  = false;
		azo::rhi::ShaderBinaryFormat m_format = azo::rhi::ShaderBinaryFormat::eBackendNative;
	};

} // namespace langs

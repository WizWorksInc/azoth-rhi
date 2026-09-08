// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
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

	enum class SourceLanguage : std::uint8_t
	{
		eSlang,
		eHlsl,
		eGlsl,
		eMsl,
	};

	struct Threadgroup final
	{
		std::uint32_t x = 1;
		std::uint32_t y = 1;
		std::uint32_t z = 1;
	};

	class ShaderCompiler final
	{
	public:
		ShaderCompiler();
		ShaderCompiler(const ShaderCompiler &)			   = delete;
		ShaderCompiler & operator=(const ShaderCompiler &) = delete;
		ShaderCompiler(ShaderCompiler &&)				   = delete;
		ShaderCompiler & operator=(ShaderCompiler &&)	   = delete;
		~ShaderCompiler();

		[[nodiscard]] bool Open(azo::rhi::GraphicsApiId api, std::string & error);

		[[nodiscard]] azo::rhi::ShaderBinary Compile(
			const char * fileName, const char * entryPoint, SourceLanguage language, Threadgroup threadgroup, std::string & error);

	private:
		[[nodiscard]] azo::rhi::ShaderBinary LoadPrebuilt(const char * fileName, const char * entryPoint, Threadgroup threadgroup, std::string & error);

		struct Session;

		std::unique_ptr<Session> m_session;

		const char * m_binaryExtension = nullptr;

		bool m_keepsEntryPointName			  = false;
		azo::rhi::ShaderBinaryFormat m_format = azo::rhi::ShaderBinaryFormat::eBackendNative;
	};

}

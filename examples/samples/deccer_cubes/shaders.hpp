// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
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

	struct Threadgroup final
	{
		std::uint32_t x = 1;
		std::uint32_t y = 1;
		std::uint32_t z = 1;
	};

	[[nodiscard]] std::string LoadShaderSource(const char * name, std::string & error);

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

		[[nodiscard]] azo::rhi::ShaderBinary Compile(const char * moduleName, const char * source, const char * entryPoint, azo::rhi::ShaderStage stage,
			std::string & error, Threadgroup threadgroup = {});

	private:
		struct Session;

		std::unique_ptr<Session> m_session;
		bool m_keepsEntryPointName			  = false;
		azo::rhi::ShaderBinaryFormat m_format = azo::rhi::ShaderBinaryFormat::eBackendNative;
	};

	[[nodiscard]] bool CanCompileFor(azo::rhi::GraphicsApiId api);

}

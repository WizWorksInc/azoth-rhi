// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <azoth/rhi/core/enums.hpp>
#include <azoth/rhi/device/api_tags.hpp>
#include <azoth/rhi/resources/pipeline.hpp>

#include <filesystem>
#include <memory>
#include <string>

namespace fw::shader
{
	class SlangCompiler final
	{
	public:
		SlangCompiler();
		SlangCompiler(const SlangCompiler &)			 = delete;
		SlangCompiler & operator=(const SlangCompiler &) = delete;
		SlangCompiler(SlangCompiler &&)					 = delete;
		SlangCompiler & operator=(SlangCompiler &&)		 = delete;
		~SlangCompiler();

		[[nodiscard]] bool Open(azo::rhi::GraphicsApiId api, std::string & error);

		[[nodiscard]] azo::rhi::ShaderBinary Compile(
			const std::filesystem::path & relative, const char * entryPoint, azo::rhi::ShaderStage stage, std::string & error);

	private:
		struct Session;

		std::unique_ptr<Session> m_session;
	};
}

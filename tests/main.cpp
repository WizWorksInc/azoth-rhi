// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#include "azoth/rhi/core/version.hpp"

#include "conformance/matchers.hpp"
#include "harness/backends.hpp"
#include "harness/environment.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <array>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>
#include <typeinfo>

#if defined(_WIN32) && defined(_DEBUG)
	#include <crtdbg.h>
#endif

namespace azo::rhi::test
{

	DeviceDesc DefaultDeviceDesc() noexcept
	{
		DeviceDesc desc{};
		desc.validation					= kValidationMode;
		desc.nativeValidation.onMessage = &RecordValidationMessage;

		desc.requireSwapchain = false;

		desc.allowSoftwareAdapter = true;

		static constexpr std::array kExercised{
			DeviceFeature::eTimestampQueries,
			DeviceFeature::eSamplerAnisotropy,
			DeviceFeature::eIndependentBlend,
			DeviceFeature::eDepthBounds,
			DeviceFeature::ePipelineStatisticsQueries,
			DeviceFeature::eMultiDrawIndirect,
			DeviceFeature::eDrawIndirectFirstInstance,
			DeviceFeature::eShaderDrawParameters,
			DeviceFeature::eSparseResources,
			DeviceFeature::eSparseBuffers,
			DeviceFeature::eSparseTextures,
			DeviceFeature::eSparseVolumes,
			DeviceFeature::eTextureViewSwizzle,
			DeviceFeature::eMultiPlanarFormats,
			DeviceFeature::eSamplerYcbcrConversion,
		};
		desc.preferredFeatures = kExercised;

		desc.debugName = "azoth.rhi.test.device";
		return desc;
	}

}

namespace
{

	void PrintConfiguration()
	{
		namespace rhi  = azo::rhi;
		namespace test = azo::rhi::test;

		constexpr rhi::BuildInfo build = rhi::GetBuildInfo();

		std::cout << "AzothRHI " << build.versionMajor << '.' << build.versionMinor << '.' << build.versionPatch << " test suite\n";

		std::cout << "  validation mode: " << AZOTH_RHI_TEST_CONFIGURATION_NAME << '\n';
		std::cout << "  profiling: " << (build.profilingEnabled ? "compiled in" : "compiled out") << (build.tracyEnabled ? ", Tracy sink built" : "") << '\n';

		std::cout << "  backends under test:";
		for (const test::Backend & backend : test::SelectedBackends())
		{
			std::cout << ' ' << backend.shortName;
		}
		if (test::SelectedBackends().empty())
		{
			std::cout << " none";
		}
		std::cout << '\n';

		if (const std::uint32_t scale = test::StressScale(); scale != 1)
		{
			std::cout << "  stress scale: " << scale << '\n';
		}

		std::cout << std::flush;
	}

	[[noreturn]] void ReportTerminate()
	{
		if (std::current_exception())
		{
			try
			{
				std::rethrow_exception(std::current_exception());
			}
			catch (const std::exception & failure)
			{
				std::cerr << "terminate: " << typeid(failure).name() << ": " << failure.what() << '\n';
			}
			catch (...)
			{
				std::cerr << "terminate: an exception that does not derive from std::exception\n";
			}
		}
		else
		{
			std::cerr << "terminate: no active exception, so a joinable thread was destroyed or a noexcept function threw\n";
		}

		std::cerr << std::flush;
		std::abort();
	}
}

int main(int argc, char ** argv)
{
	static_cast<void>(std::set_terminate(&ReportTerminate));

#if defined(_WIN32) && defined(_DEBUG)
	for (const int report : { _CRT_ASSERT, _CRT_ERROR })
	{
		static_cast<void>(::_CrtSetReportMode(report, _CRTDBG_MODE_FILE));
		static_cast<void>(::_CrtSetReportFile(report, _CRTDBG_FILE_STDERR));
	}
#endif

	// NOLINTBEGIN(concurrency-mt-unsafe): startup environment edit, before any test or worker thread exists.
#ifdef _WIN32
	static_cast<void>(::_putenv_s("AZOTH_RHI_BACKEND", ""));
	static_cast<void>(::_putenv_s("AZOTH_RHI_BACKEND_FORCE", ""));
#else
	static_cast<void>(::unsetenv("AZOTH_RHI_BACKEND"));
	static_cast<void>(::unsetenv("AZOTH_RHI_BACKEND_FORCE"));
#endif
	// NOLINTEND(concurrency-mt-unsafe)

	::testing::InitGoogleMock(&argc, argv);

	if (const std::string problem = azo::rhi::test::ValidateBackendSelection(); !problem.empty())
	{
		std::cerr << problem << '\n';
		return 1;
	}

	PrintConfiguration();

	return RUN_ALL_TESTS();
}

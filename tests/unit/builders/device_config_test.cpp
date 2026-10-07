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

#include "azoth/rhi/backend/dispatch.hpp"
#include "azoth/rhi/builders/device_builder.hpp"

#include "conformance/matchers.hpp"

#include <gtest/gtest.h>

#include <array>
#include <utility>

namespace rhi  = azo::rhi;
namespace test = azo::rhi::test;

namespace builder_config_test
{
	struct Api final : rhi::GraphicsApiTagRoot
	{
		static constexpr std::string_view kCanonicalName = "test.rhi.builder-config";
		static constexpr std::string_view kDisplayName	 = "Builder config fixture";
		static constexpr rhi::GraphicsApiId kId			 = rhi::make_graphics_api_id(kCanonicalName);
	};

	struct OtherApi final : rhi::GraphicsApiTagRoot
	{
		static constexpr std::string_view kCanonicalName = "test.rhi.other-builder-config";
		static constexpr std::string_view kDisplayName	 = "Other builder config fixture";
		static constexpr rhi::GraphicsApiId kId			 = rhi::make_graphics_api_id(kCanonicalName);
	};

	struct DeviceConfig final
	{
		rhi::InterfaceHeader header{ .byteSize = sizeof(DeviceConfig), .version = 1 };
		std::uint32_t queueLimit   = 0;
		std::uint32_t memoryBudget = 0;
	};

	struct InstanceConfig final
	{
		rhi::InterfaceHeader header{ .byteSize = sizeof(InstanceConfig), .version = 1 };
		std::uint32_t adapterLimit = 0;
	};

	struct Observations final
	{
		std::size_t deviceBlocks   = 0;
		std::size_t instanceBlocks = 0;
		std::uint32_t queueLimit   = 0;
		std::uint32_t memoryBudget = 0;
		std::uint32_t adapterLimit = 0;
		std::uint32_t deviceCalls  = 0;
	};

	Observations observed{};
	void RecordInstance(const rhi::InstanceDesc & desc) noexcept;
	void * RecordDevice(const rhi::DeviceDesc & desc, rhi::Error * error) noexcept;
}

namespace azo::rhi::native
{
	template <>
	struct DeviceConfigFor<builder_config_test::Api> final
	{
		using Config = builder_config_test::DeviceConfig;
	};

	template <>
	struct DeviceConfigFor<builder_config_test::OtherApi> final
	{
		using Config = builder_config_test::DeviceConfig;
	};

	template <>
	struct InstanceConfigFor<builder_config_test::Api> final
	{
		using Config = builder_config_test::InstanceConfig;
	};
}

namespace builder_config_test
{
	void RecordInstance(const rhi::InstanceDesc & desc) noexcept
	{
		observed.instanceBlocks = desc.backendConfigs.size();
		const auto config		= rhi::native::find_instance_config<Api>(desc.backendConfigs);
		if (config.block != nullptr)
		{
			observed.adapterLimit = config.block->adapterLimit;
		}
	}

	void * RecordDevice(const rhi::DeviceDesc & desc, rhi::Error * error) noexcept
	{
		++observed.deviceCalls;
		observed.deviceBlocks = desc.backendConfigs.size();
		const auto config	  = rhi::native::find_device_config<Api>(desc.backendConfigs);
		if (config.block != nullptr)
		{
			observed.queueLimit	  = config.block->queueLimit;
			observed.memoryBudget = config.block->memoryBudget;
		}
		*error = rhi::Error{ .code = rhi::ErrorCode::eUnsupportedFeature, .message = "the configuration fixture only observes device creation" };
		return nullptr;
	}

	const rhi::InstanceApi & InstanceBlock() noexcept
	{
		static const rhi::InstanceApi block{
			.getGraphicsApiId =
				[](void *) noexcept
			{
				return Api::kId;
			},
			.enumerateAdapters =
				[](void *, std::span<rhi::AdapterInfo>, std::uint32_t * out, rhi::Error *) noexcept
			{
				*out = 0;
				return true;
			},
			.createDevice =
				[](void *, const rhi::DeviceDesc & desc, rhi::Error * error) noexcept
			{
				return RecordDevice(desc, error);
			},
			.destroyInstance = [](void *) noexcept {},
		};
		return block;
	}

	const void * QueryInterface(void *, rhi::InterfaceId id, std::uint32_t version) noexcept
	{
		return id == rhi::InterfaceTraits<rhi::InstanceApi>::kId && version <= 1 ? &InstanceBlock() : nullptr;
	}

	struct Instance final
	{
		const rhi::BackendObject * object;
	};

	const rhi::BackendObject kObject{ .queryInterface = &QueryInterface };
	Instance instance{ .object = &kObject };

	rhi::BackendCreateInfo CreateInfo()
	{
		rhi::BackendCreateInfo info{};
		info.info.canonicalName = Api::kCanonicalName;
		info.info.displayName	= Api::kDisplayName;
		info.createInstance		= [](const void * desc, rhi::Error *) noexcept
		{
			RecordInstance(*static_cast<const rhi::InstanceDesc *>(desc));
			return static_cast<void *>(&instance);
		};
		return info;
	}
}

namespace azo::rhi
{
	template <>
	Result<UniqueDevice> create_device<builder_config_test::Api>(const DeviceDesc & desc)
	{
		builder_config_test::RecordInstance(instance_desc_for_device(desc));
		Error refusal{};
		builder_config_test::RecordDevice(desc, &refusal);
		return refusal;
	}
}

namespace
{
	using builder_config_test::Api;
	using builder_config_test::observed;

	class DeviceBuilderConfig : public ::testing::Test
	{
	protected:
		void SetUp() override
		{
			observed = {};
			ASSERT_TRUE(test::Ok(registry.Register<Api>(builder_config_test::CreateInfo())));
		}

		void Observe(const rhi::DeviceBuilder & builder)
		{
			observed = {};
			const std::array preferred{ Api::kId };
			EXPECT_TRUE(test::Failed(builder.build(registry, preferred), rhi::ErrorCode::eUnsupportedFeature));
			EXPECT_EQ(observed.deviceCalls, 1u);
		}

		rhi::GraphicsApiRegistry registry;
	};

	TEST_F(DeviceBuilderConfig, RoutesCustomDeviceAndInstanceBlocksSeparately)
	{
		rhi::DeviceBuilder builder;
		builder
			.configure<Api>(
				[](auto & config)
				{
					config.queueLimit = 7;
				}
			)
			.configure_instance<Api>(
				[](auto & config)
				{
					config.adapterLimit = 11;
				}
			);
		Observe(builder);
		EXPECT_EQ(observed.deviceBlocks, 1u);
		EXPECT_EQ(observed.instanceBlocks, 1u);
		EXPECT_EQ(observed.queueLimit, 7u);
		EXPECT_EQ(observed.adapterLimit, 11u);
	}

	TEST_F(DeviceBuilderConfig, StaticBuildCarriesBothScopes)
	{
		rhi::DeviceBuilder builder;
		builder
			.configure<Api>(
				[](auto & config)
				{
					config.queueLimit = 7;
				}
			)
			.configure_instance<Api>(
				[](auto & config)
				{
					config.adapterLimit = 11;
				}
			);
		EXPECT_TRUE(test::Failed(builder.build<Api>(), rhi::ErrorCode::eUnsupportedFeature));
		EXPECT_EQ(observed.deviceCalls, 1u);
		EXPECT_EQ(observed.queueLimit, 7u);
		EXPECT_EQ(observed.adapterLimit, 11u);
	}

	TEST_F(DeviceBuilderConfig, ReconfiguringPreservesFieldsAndReplacesTheEntry)
	{
		rhi::DeviceBuilder builder;
		builder
			.configure<Api>(
				[](auto & config)
				{
					config.queueLimit	= 7;
					config.memoryBudget = 11;
				}
			)
			.configure<Api>(
				[](auto & config)
				{
					config.queueLimit = 9;
				}
			);
		Observe(builder);
		EXPECT_EQ(observed.deviceBlocks, 1u);
		EXPECT_EQ(observed.queueLimit, 9u);
		EXPECT_EQ(observed.memoryBudget, 11u);
	}

	TEST_F(DeviceBuilderConfig, ACopyCanReconfigureWithoutChangingTheOriginal)
	{
		rhi::DeviceBuilder original;
		original
			.configure<Api>(
				[](auto & config)
				{
					config.queueLimit = 7;
				}
			)
			.configure_instance<Api>(
				[](auto & config)
				{
					config.adapterLimit = 11;
				}
			);
		rhi::DeviceBuilder copy = original;
		copy.configure<Api>(
				[](auto & config)
				{
					config.queueLimit = 9;
				}
		)
			.configure_instance<Api>(
				[](auto & config)
				{
					config.adapterLimit = 13;
				}
			);
		Observe(original);
		EXPECT_EQ(observed.queueLimit, 7u);
		EXPECT_EQ(observed.adapterLimit, 11u);
		Observe(copy);
		EXPECT_EQ(observed.queueLimit, 9u);
		EXPECT_EQ(observed.adapterLimit, 13u);
	}

	TEST_F(DeviceBuilderConfig, ACopyKeepsBlocksAfterTheOriginalIsDestroyed)
	{
		rhi::DeviceBuilder surviving;
		{
			rhi::DeviceBuilder original;
			original.configure<Api>(
				[](auto & config)
				{
					config.queueLimit = 7;
				}
			);
			surviving = original;
		}
		rhi::DeviceBuilder moved = std::move(surviving);
		Observe(moved);
		EXPECT_EQ(observed.queueLimit, 7u);
	}

	TEST_F(DeviceBuilderConfig, CarriesSeveralApisWithoutMixingTheirBlocks)
	{
		rhi::DeviceBuilder builder;
		builder
			.configure<Api>(
				[](auto & config)
				{
					config.queueLimit = 7;
				}
			)
			.configure<builder_config_test::OtherApi>(
				[](auto & config)
				{
					config.queueLimit = 31;
				}
			);
		Observe(builder);
		EXPECT_EQ(observed.deviceBlocks, 2u);
		EXPECT_EQ(observed.queueLimit, 7u);
	}
}

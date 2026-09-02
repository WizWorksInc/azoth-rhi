// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include "azoth/rhi/core/handle.hpp"

namespace azo::rhi
{

	struct BufferTag final
	{
	};

	struct TextureTag final
	{
	};

	struct TextureViewTag final
	{
	};

	struct SamplerTag final
	{
	};

	struct HeapTag final
	{
	};

	struct DescriptorSetLayoutTag final
	{
	};

	struct DescriptorSetTag final
	{
	};

	struct DescriptorArenaTag final
	{
	};

	struct PipelineLayoutTag final
	{
	};

	struct GraphicsPipelineTag final
	{
	};

	struct ComputePipelineTag final
	{
	};

	struct RayTracingPipelineTag final
	{
	};

	struct PipelineCacheTag final
	{
	};

	struct AccelerationStructureTag final
	{
	};

	struct QueryPoolTag final
	{
	};

	struct TimelineTag final
	{
	};

	struct BinarySemaphoreTag final
	{
	};

	using BufferHandle				  = Handle<BufferTag>;
	using TextureHandle				  = Handle<TextureTag>;
	using TextureViewHandle			  = Handle<TextureViewTag>;
	using SamplerHandle				  = Handle<SamplerTag>;
	using HeapHandle				  = Handle<HeapTag>;
	using DescriptorSetLayoutHandle	  = Handle<DescriptorSetLayoutTag>;
	using DescriptorSetHandle		  = Handle<DescriptorSetTag>;
	using DescriptorArenaHandle		  = Handle<DescriptorArenaTag>;
	using PipelineLayoutHandle		  = Handle<PipelineLayoutTag>;
	using GraphicsPipelineHandle	  = Handle<GraphicsPipelineTag>;
	using ComputePipelineHandle		  = Handle<ComputePipelineTag>;
	using RayTracingPipelineHandle	  = Handle<RayTracingPipelineTag>;
	using PipelineCacheHandle		  = Handle<PipelineCacheTag>;
	using AccelerationStructureHandle = Handle<AccelerationStructureTag>;
	using QueryPoolHandle			  = Handle<QueryPoolTag>;
	using TimelineHandle			  = Handle<TimelineTag>;
	using BinarySemaphoreHandle		  = Handle<BinarySemaphoreTag>;

}

// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

struct Params
{
	uint iterations;
};

[[vk::push_constant]] ConstantBuffer<Params> gParams;

struct Work
{
	RWStructuredBuffer<float> accumulator;
};

ParameterBlock<Work> gWork;

[shader("compute")][numthreads(64, 1, 1)] void integrateMain(uint3 thread : SV_DispatchThreadID)
{
	float value = float(thread.x) * 0.001;
	for (uint step = 0; step < gParams.iterations; ++step)
	{
		value = sin(value) + cos(value * 0.5);
	}

	gWork.accumulator[thread.x] = value;
}

struct VertexOutput
{
	float4 position : SV_Position;
};

static const float2 kPositions[3] = { float2(-1.0, -3.0), float2(-1.0, 1.0), float2(3.0, 1.0) };

[shader("vertex")] VertexOutput vertexMain(uint vertexId : SV_VertexID)
{
	VertexOutput output;
	output.position = float4(kPositions[vertexId], 0.0, 1.0);
	return output;
}

	[shader("fragment")] float4 fragmentMain(VertexOutput input)
	: SV_Target
{
	return float4(0.25, 0.5, 0.75, 1.0);
}

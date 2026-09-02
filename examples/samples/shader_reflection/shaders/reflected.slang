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
	float scale;
	float bias;
};

[[vk::push_constant]] ConstantBuffer<Params> gParams;

struct Resources
{
	StructuredBuffer<float> input;
	RWStructuredBuffer<float> output;
};

ParameterBlock<Resources> gSet;

[shader("compute")][numthreads(32, 1, 1)] void transformMain(uint3 thread : SV_DispatchThreadID)
{
	gSet.output[thread.x] = (gSet.input[thread.x] * gParams.scale) + gParams.bias;
}

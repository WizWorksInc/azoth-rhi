// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

struct VertexOutput
{
	float4 position : SV_Position;
	float3 color : COLOR;
};

static const float2 kPositions[3] = { float2(0.0, 0.6), float2(0.6, -0.6), float2(-0.6, -0.6) };
static const float3 kColors[3]	  = { float3(1, 0, 0), float3(0, 1, 0), float3(0, 0, 1) };

[shader("vertex")] VertexOutput vertexMain(uint vertexId : SV_VertexID)
{
	VertexOutput output;
	output.position = float4(kPositions[vertexId], 0.0, 1.0);
	output.color	= kColors[vertexId];
	return output;
}

	[shader("fragment")] float4 fragmentMain(VertexOutput input)
	: SV_Target
{
	return float4(input.color, 1.0);
}

// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#include <metal_stdlib>

using namespace metal;

struct Transform
{
	float2 scale;
	float2 translate;

	uint srgbTarget;
};

struct TextureSet
{
	texture2d<float> image [[id(0)]];
	sampler samp [[id(1)]];
};

struct VertexInput
{
	float2 position [[attribute(0)]];
	float2 uv [[attribute(1)]];
	float4 color [[attribute(2)]];
};

struct VertexOutput
{
	float4 position [[position]];
	float2 uv;
	float4 color;
};

vertex VertexOutput vertexMain(VertexInput input [[stage_in]], constant Transform & transform [[buffer(0)]])
{
	VertexOutput output;
	output.position = float4((input.position * transform.scale) + transform.translate, 0.0, 1.0);
	output.uv		= input.uv;
	output.color	= input.color;
	return output;
}

static float3 SrgbToLinear(float3 colour)
{
	const float3 low  = colour / 12.92f;
	const float3 high = pow(max((colour + 0.055f) / 1.055f, float3(0.0f)), float3(2.4f));
	return mix(low, high, step(float3(0.04045f), colour));
}

fragment float4 fragmentMain(
	VertexOutput input [[stage_in]], constant Transform & transform [[buffer(0)]], device TextureSet & set [[buffer(1)]])
{
	float4 tint = input.color;
	if (transform.srgbTarget != 0)
	{
		tint.rgb = SrgbToLinear(tint.rgb);
	}

	return tint * set.image.sample(set.samp, input.uv);
}

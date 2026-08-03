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

#include <metal_stdlib>

using namespace metal;

/*
 * What imgui.slang says, in the language Metal is the only one that speaks. Compiled by Apple's own toolchain, there being no cross compiler for it.
 *
 * The buffer indices are the RHI's Metal ABI by hand. Buffer 0 is the push constant, a descriptor set is an argument buffer at its set index plus one, and each
 * binding takes an id in declaration order. The argument buffer form needs binding tier eUnbounded, the same requirement the Slang path carries.
 */

struct Transform
{
	// ImGui works in pixels with the origin top left, so this turns that into clip space. Two multiplies and two adds and not a matrix, since a projection with
	// no rotation in it is exactly that much arithmetic.
	float2 scale;
	float2 translate;

	// Non-zero when the target is an sRGB format, which decides whether the vertex colour below is linearized first.
	uint srgbTarget;
};

struct TextureSet
{
	texture2d<float> image [[id(0)]];
	sampler samp [[id(1)]];
};

// Attribute numbers follow the RHI's vertex ABI, the attribute at location N being the Nth the input layout declares.
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

/*
 * ImGui authors its style colours as the values it wants to see on screen, a display referred number.
 *
 * On an sRGB target the hardware encodes whatever the shader writes, so handing it 0.26 stores something near 0.54 and the interface comes out washed. Undoing
 * the encode here means the hardware's own encode puts the number back. On a plain unorm target nothing is encoded either way.
 */
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

	// Alpha is never sRGB encoded, on any target, so it is left alone above and multiplies through unchanged here.
	return tint * set.image.sample(set.samp, input.uv);
}

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

/*
 * What imgui.slang says, in the language Direct3D grew up with. Compiled for DXIL, and readable as SPIR-V by a dxc that emits it.
 *
 * Every slot is named twice, register() for Direct3D and vk:: for SPIR-V, one file reaching two APIs being the whole reason HLSL carries both. The numbers are
 * the RHI's: push constants at b0 in space 0, and descriptor set N one space above, so set 0 lands in space 1.
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

// Root constants reach a shader as a constant buffer, which is what a push constant is on this API.
[[vk::push_constant]]
ConstantBuffer<Transform> gTransform : register(b0, space0);

[[vk::binding(0, 0)]]
Texture2D gImage : register(t0, space1);

[[vk::binding(1, 0)]]
SamplerState gSampler : register(s0, space1);

// Semantics follow the RHI's vertex ABI: attribute at location N is TEXCOORDN, which is what the input layout declares.
struct VertexInput
{
    float2 position : TEXCOORD0;
    float2 uv       : TEXCOORD1;
    float4 color    : TEXCOORD2;
};

struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv       : TEXCOORD0;
    float4 color    : COLOR;
};

VertexOutput vertexMain(VertexInput input)
{
    VertexOutput output;
    output.position = float4((input.position * gTransform.scale) + gTransform.translate, 0.0, 1.0);
    output.uv       = input.uv;
    output.color    = input.color;
    return output;
}

/*
 * ImGui authors its style colours as the values it wants to see on screen, a display referred number.
 *
 * On an sRGB target the hardware encodes whatever the shader writes, so handing it 0.26 stores something near 0.54 and the interface comes out washed. Undoing
 * the encode here means the hardware's own encode puts the number back. On a plain unorm target nothing is encoded either way.
 */
float3 SrgbToLinear(float3 colour)
{
    const float3 low  = colour / 12.92;
    const float3 high = pow(max((colour + 0.055) / 1.055, 0.0), 2.4);
    return lerp(low, high, step(0.04045, colour));
}

float4 fragmentMain(VertexOutput input) : SV_Target
{
    float4 tint = input.color;
    if (gTransform.srgbTarget != 0)
    {
        tint.rgb = SrgbToLinear(tint.rgb);
    }

    // Alpha is never sRGB encoded, on any target, so it is left alone above and multiplies through unchanged here.
    return tint * gImage.Sample(gSampler, input.uv);
}

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
 * What Dear ImGui draws with. It emits one vertex format and one draw call shape, so this is the whole of the shading it needs.
 *
 * Compiled ahead of time by the build and embedded, so azoth::rhi-imgui links no shader compiler.
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

[[vk::push_constant]]
ConstantBuffer<Transform> gTransform;

// One ParameterBlock per descriptor set, which is what makes this conform to the RHI's binding ABI on both targets from one source.
struct TextureSet
{
    Texture2D image;
    SamplerState samp;
};

ParameterBlock<TextureSet> gTextureSet;

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

[shader("vertex")]
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

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target
{
    /*
     * The vertex colour arrives already decoded from the packed byte per channel ImGui stores. Multiplying is all the blending the geometry needs, the
     * rest being the alpha blend the pipeline is created with. The texture is whichever one the draw command named, which is the font atlas for text and
     * whatever the caller registered for an image.
     */
    float4 tint = input.color;
    if (gTransform.srgbTarget != 0)
    {
        tint.rgb = SrgbToLinear(tint.rgb);
    }

    // Alpha is never sRGB encoded, on any target, so it is left alone above and multiplies through unchanged here.
    return tint * gTextureSet.image.Sample(gTextureSet.samp, input.uv);
}

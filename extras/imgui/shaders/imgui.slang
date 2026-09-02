// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

struct Transform
{
    float2 scale;
    float2 translate;

    uint srgbTarget;
};

[[vk::push_constant]]
ConstantBuffer<Transform> gTransform;

struct TextureSet
{
    Texture2D image;
    SamplerState samp;
};

ParameterBlock<TextureSet> gTextureSet;

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

float3 SrgbToLinear(float3 colour)
{
    const float3 low  = colour / 12.92;
    const float3 high = pow(max((colour + 0.055) / 1.055, 0.0), 2.4);
    return lerp(low, high, step(0.04045, colour));
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target
{
    float4 tint = input.color;
    if (gTransform.srgbTarget != 0)
    {
        tint.rgb = SrgbToLinear(tint.rgb);
    }

    return tint * gTextureSet.image.Sample(gTextureSet.samp, input.uv);
}

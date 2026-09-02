// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

struct Constants
{
    uint2 dstSize;
    uint layerCount;

    uint encodeSrgb;
};

[[vk::push_constant]]
ConstantBuffer<Constants> gConstants;

struct ResampleSet
{
    Texture2DArray<float4> source;
    SamplerState sourceSampler;
    RWTexture2DArray<float4> destination;
};

ParameterBlock<ResampleSet> gResample;

float3 EncodeSrgb(float3 linearColour)
{
    const float3 lo = linearColour * 12.92;
    const float3 hi = (1.055 * pow(max(linearColour, 0.0), 1.0 / 2.4)) - 0.055;
    return select(linearColour <= 0.0031308, lo, hi);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void resampleMain(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= gConstants.dstSize.x || id.y >= gConstants.dstSize.y || id.z >= gConstants.layerCount)
    {
        return;
    }

    const float2 uv = (float2(id.xy) + 0.5) / float2(gConstants.dstSize);

    float4 texel = gResample.source.SampleLevel(gResample.sourceSampler, float3(uv, float(id.z)), 0.0);
    if (gConstants.encodeSrgb != 0)
    {
        texel.rgb = EncodeSrgb(texel.rgb);
    }

    gResample.destination[int3(id)] = texel;
}

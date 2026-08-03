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
 * One resample, from one level of a texture into another.
 *
 * A ParameterBlock, not flat vk::binding globals, which is the RHI's binding ABI: a block claims descriptor set 0 on spirv and register space 1 on dxil. Flat
 * globals share one counter per register class in space 0, which the root signature does not bind.
 *
 * Every view is an array so one entry point serves 2D, array and cube sources. A plain 2D texture is an array of one layer.
 */

struct Constants
{
    // Destination extent in texels, and how many layers to walk. The dispatch is sized from this too, so a thread past it has no texel to own.
    uint2 dstSize;
    uint layerCount;

    /*
     * No API allows an sRGB format as a storage image, so a caller resampling an sRGB texture binds a non-sRGB view and the encode the hardware would have
     * done on write happens here instead. The source keeps its sRGB view, so the sample below is already linear.
     */
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

// Declared after the push constant on purpose: slangc assigns Metal buffer indices in declaration order and the ABI reserves buffer 0 for the push constant,
// so a block declared ahead of it would take the slot the RHI binds constants at.
ParameterBlock<ResampleSet> gResample;

// The IEC 61966-2-1 transfer function, which is what an sRGB image view applies on write.
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

    /*
     * Sampled at the centre of the destination texel, in the source's normalised space. A linear sampler over the level above gives a box filter for the
     * halving case and a bilinear resize for every other, matching what vkCmdBlitImage with eLinear does.
     */
    const float2 uv = (float2(id.xy) + 0.5) / float2(gConstants.dstSize);

    float4 texel = gResample.source.SampleLevel(gResample.sourceSampler, float3(uv, float(id.z)), 0.0);
    if (gConstants.encodeSrgb != 0)
    {
        texel.rgb = EncodeSrgb(texel.rgb);
    }

    gResample.destination[int3(id)] = texel;
}

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
 * One binding holding every texture, indexed by a number the shader computed.
 *
 * This is the whole of what bindless buys. A bound renderer would need one set per material and a rebind between draws. Here the array is declared without a
 * size, the host writes as many entries as it has, and the index is just data.
 *
 * The array is last in the block on purpose: a binding sized when the set is allocated has to be the trailing one.
 */

struct Params
{
    uint count;
};

// Declared first and as a push constant, which leaves Metal's buffer 0 free and numbers the block below where the RHI's ABI binds it.
[[vk::push_constant]]
ConstantBuffer<Params> gParams;

struct Bindless
{
    SamplerState sampler;
    RWStructuredBuffer<float4> output;

    // The layout's upper bound, not the count the set was allocated with. Entries past that count are never read and are legal to leave unwritten.
    Texture2D textures[1024];
};

ParameterBlock<Bindless> gSet;

[shader("compute")]
[numthreads(16, 1, 1)]
void gatherMain(uint3 thread : SV_DispatchThreadID)
{
    if (thread.x >= gParams.count)
    {
        return;
    }

    // The index is the thread id, so every invocation reaches a different texture through the one binding.
    gSet.output[thread.x] = gSet.textures[thread.x].SampleLevel(gSet.sampler, float2(0.5, 0.5), 0.0);
}

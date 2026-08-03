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
 * Work worth timing, in the two places a timestamp can bracket.
 *
 * The compute kernel is deliberately slow. A dispatch that finishes in nanoseconds reads back as a few ticks, indistinguishable from the cost of the timestamp
 * write itself. The loop count arrives as a push constant so the host can raise it above the noise.
 *
 * The triangle covers the whole target so the occlusion count is the pixel count, which the sample can check instead of printing and hoping.
 */

struct Params
{
    uint iterations;
};

// Declared first and as a push constant, which is what leaves Metal's buffer 0 free and numbers the set below at the index the RHI's ABI binds it to.
[[vk::push_constant]]
ConstantBuffer<Params> gParams;

struct Work
{
    RWStructuredBuffer<float> accumulator;
};

ParameterBlock<Work> gWork;

[shader("compute")]
[numthreads(64, 1, 1)]
void integrateMain(uint3 thread : SV_DispatchThreadID)
{
    // A dependent chain, so the compiler cannot fold the loop away and the dispatch actually takes the time it is being measured for.
    float value = float(thread.x) * 0.001;
    for (uint step = 0; step < gParams.iterations; ++step)
    {
        value = sin(value) + cos(value * 0.5);
    }

    gWork.accumulator[thread.x] = value;
}

struct VertexOutput
{
    float4 position : SV_Position;
};

// The oversized triangle that covers the viewport from three vertices, so nothing is bound and every pixel is written.
static const float2 kPositions[3] = { float2(-1.0, -3.0), float2(-1.0, 1.0), float2(3.0, 1.0) };

[shader("vertex")]
VertexOutput vertexMain(uint vertexId : SV_VertexID)
{
    VertexOutput output;
    output.position = float4(kPositions[vertexId], 0.0, 1.0);
    return output;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target
{
    return float4(0.25, 0.5, 0.75, 1.0);
}

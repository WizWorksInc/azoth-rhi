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
 * One shader, built into many pipelines.
 *
 * A cache is worth having because a real renderer builds a permutation for every combination of state a material asks for, and pays a compile for each. The
 * host here varies the fixed function state and not the source, the cheaper half of the same problem: the driver still compiles one per permutation and still
 * skips it on a second run.
 *
 * The fragment body is more than a constant so there is work.
 */

struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv       : TEXCOORD0;
};

static const float2 kPositions[3] = { float2(-1.0, -3.0), float2(-1.0, 1.0), float2(3.0, 1.0) };

[shader("vertex")]
VertexOutput vertexMain(uint vertexId : SV_VertexID)
{
    VertexOutput output;
    output.position = float4(kPositions[vertexId], 0.0, 1.0);
    output.uv       = kPositions[vertexId] * 0.5 + 0.5;
    return output;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target
{
    float3 color = float3(0.0);
    float2 point = input.uv * 8.0;

    // A handful of octaves, so the pipeline this ends up in takes long enough to compile that skipping the compile is visible.
    float amplitude = 1.0;
    for (uint octave = 0; octave < 6; ++octave)
    {
        color += amplitude * float3(sin(point.x), cos(point.y), sin(point.x + point.y));
        point *= 2.0;
        amplitude *= 0.5;
    }

    return float4(color * 0.25 + 0.5, 1.0);
}

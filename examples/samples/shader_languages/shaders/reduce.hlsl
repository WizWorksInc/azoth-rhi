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
 * The shape HLSL compute has looked like since Direct3D 11: a groupshared array, a barrier and a halving loop.
 *
 * HLSL names a slot twice when it has to reach two APIs: register(u0) for Direct3D and vk::binding for SPIR-V. Both name the slot the RHI's ABI reports for set
 * 0 binding 0. Leaving the second off compiles with a warning and binds somewhere of Slang's choosing.
 *
 * Slang reads this file as HLSL because of its extension. Nothing here is Slang syntax.
 */

[[vk::binding(0, 0)]]
RWStructuredBuffer<float> gOutput : register(u0);

groupshared float gPartial[64];

[numthreads(64, 1, 1)]
void computeMain(uint3 group : SV_GroupThreadID, uint3 thread : SV_DispatchThreadID)
{
    gPartial[group.x] = float(thread.x);
    GroupMemoryBarrierWithGroupSync();

    // Each pass folds the upper half onto the lower one, so after six the whole group has landed in slot zero.
    for (uint stride = 32; stride > 0; stride >>= 1)
    {
        if (group.x < stride)
        {
            gPartial[group.x] += gPartial[group.x + stride];
        }

        GroupMemoryBarrierWithGroupSync();
    }

    // Every lane writes so the buffer carries the running total beside the answer, which makes a partial reduction visible, not silent.
    gOutput[thread.x] = gPartial[group.x];
}

// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

[[vk::binding(0, 0)]] RWStructuredBuffer<float> gOutput : register(u0);

groupshared float gPartial[64];

[numthreads(64, 1, 1)] void computeMain(uint3 group : SV_GroupThreadID, uint3 thread : SV_DispatchThreadID)
{
	gPartial[group.x] = float(thread.x);
	GroupMemoryBarrierWithGroupSync();

	for (uint stride = 32; stride > 0; stride >>= 1)
	{
		if (group.x < stride)
		{
			gPartial[group.x] += gPartial[group.x + stride];
		}

		GroupMemoryBarrierWithGroupSync();
	}

	gOutput[thread.x] = gPartial[group.x];
}

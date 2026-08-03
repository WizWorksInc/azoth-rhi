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

#include <metal_stdlib>

using namespace metal;

/*
 * The same total the HLSL kernel reduces, reached through the SIMD group instead of through shared memory.
 *
 * MSL is the only language here that names the SIMD group, so most of the reduction happens in registers and the barrier ladder shrinks from six rounds over 64
 * lanes to one over the group leaders. The leaders are counted and not assumed, which keeps the answer the same where they group differently.
 *
 * The struct is the RHI's Metal ABI by hand.
 */
struct Bindings
{
	device float * values [[id(0)]];
};

// A threadgroup of 64 cannot hold more leaders than it holds threads, so this is past any width the hardware will report.
constant uint kMaxGroups = 64;

kernel void computeMain(device Bindings & set [[buffer(1)]],
	uint index [[thread_position_in_grid]],
	uint lane [[thread_index_in_simdgroup]],
	uint group [[simdgroup_index_in_threadgroup]],
	uint groups [[simdgroups_per_threadgroup]])
{
	threadgroup float partials[kMaxGroups];

	// One instruction where the other kernel walks a halving loop. Every lane comes out holding its group's total, not just the first.
	const float total = simd_sum(float(index));

	if (lane == 0)
	{
		partials[group] = total;
	}

	threadgroup_barrier(mem_flags::mem_threadgroup);

	if (index != 0)
	{
		set.values[index] = float(index);
		return;
	}

	float sum = 0.0f;
	for (uint i = 0; i < groups; ++i)
	{
		sum += partials[i];
	}

	set.values[0] = sum;
}

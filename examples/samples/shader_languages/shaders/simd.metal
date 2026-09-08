// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#include <metal_stdlib>

using namespace metal;

struct Bindings
{
	device float * values [[id(0)]];
};

constant uint kMaxGroups = 64;

kernel void computeMain(device Bindings & set [[buffer(1)]], uint index [[thread_position_in_grid]], uint lane [[thread_index_in_simdgroup]],
	uint group [[simdgroup_index_in_threadgroup]], uint groups [[simdgroups_per_threadgroup]])
{
	threadgroup float partials[kMaxGroups];

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

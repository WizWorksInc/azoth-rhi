// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#version 450

layout(local_size_x = 1, local_size_y = 1, local_size_z = 1) in;

layout(set = 0, binding = 0, std430) buffer Output
{
	float values[];
}

gOutput;

float DiscDistance(vec2 point, float radius)
{
	return length(point) - radius;
}

const uint kGrid = 8;

void main()
{
	vec2 uv	   = (vec2(gl_GlobalInvocationID.xy) + 0.5) / float(kGrid);
	uint index = (gl_GlobalInvocationID.y * kGrid) + gl_GlobalInvocationID.x;

	gOutput.values[index] = DiscDistance(uv - 0.5, 0.35);
}

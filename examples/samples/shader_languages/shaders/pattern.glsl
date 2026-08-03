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

#version 450

/*
 * The signed distance field every GLSL sketch starts from, evaluated over an 8 by 8 grid instead of a screen. layout(set, binding) is GLSL's spelling of where
 * a resource lives. The numbers here are the ones the RHI's layout declares.
 *
 * Slang reads this file with its GLSL front end, which wants -allow-glsl.
 */

// One invocation per group, because the host dispatches the grid and not the group. Slang's API does not carry a GLSL local_size through to the binary
// the way slangc does, so a kernel relying on it here would run a single thread.
layout(local_size_x = 1, local_size_y = 1, local_size_z = 1) in;

layout(set = 0, binding = 0, std430) buffer Output
{
    float values[];
}
gOutput;

// Negative inside the disc, positive outside, zero on it. Distance, not coverage, so the value stays useful under a transform.
float DiscDistance(vec2 point, float radius)
{
    return length(point) - radius;
}

// Stated, not read back off gl_WorkGroupSize, which Slang's GLSL front end leaves at zero and which would divide the grid away.
const uint kGrid = 8;

void main()
{
    // Not const. GLSL reserves that for constant expressions, and Slang's front end takes it at its word: mark these const and the invocation id folds
    // away to zero, leaving every thread computing cell zero.
    vec2 uv = (vec2(gl_GlobalInvocationID.xy) + 0.5) / float(kGrid);
    uint index = (gl_GlobalInvocationID.y * kGrid) + gl_GlobalInvocationID.x;

    gOutput.values[index] = DiscDistance(uv - 0.5, 0.35);
}

// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#version 450

layout(push_constant) uniform TransformBlock
{
    vec2 scale;
    vec2 translate;
    uint srgbTarget;
}
gTransform;

#ifdef AZOTH_RHI_IMGUI_VERTEX_STAGE

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec2 inUv;
layout(location = 2) in vec4 inColor;

layout(location = 0) out vec2 outUv;
layout(location = 1) out vec4 outColor;

void main()
{
    gl_Position = vec4((inPosition * gTransform.scale) + gTransform.translate, 0.0, 1.0);
    outUv       = inUv;
    outColor    = inColor;
}

#else

layout(location = 0) in vec2 inUv;
layout(location = 1) in vec4 inColor;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform texture2D gImage;
layout(set = 0, binding = 1) uniform sampler gSampler;

vec3 SrgbToLinear(vec3 colour)
{
    const vec3 low  = colour / 12.92;
    const vec3 high = pow(max((colour + 0.055) / 1.055, vec3(0.0)), vec3(2.4));
    return mix(low, high, step(vec3(0.04045), colour));
}

void main()
{
    vec4 tint = inColor;
    if (gTransform.srgbTarget != 0u)
    {
        tint.rgb = SrgbToLinear(tint.rgb);
    }

    outColor = tint * texture(sampler2D(gImage, gSampler), inUv);
}

#endif

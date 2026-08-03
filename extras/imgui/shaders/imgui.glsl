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
 * What imgui.slang says, in the language Vulkan grew up with. Compiled only for SPIR-V, GLSL having no way to reach the other two targets.
 *
 * Both stages live here and the build picks one with a define, since glslc compiles a stage at a time and names every entry point main. The set and binding
 * numbers are the ones the RHI's ABI reports, which on Vulkan is the pair the layout was declared with.
 */

// The push constant, matching Transform in renderer.cpp. Both stages read it, the vertex one for the projection and the fragment one for srgbTarget.
layout(push_constant) uniform TransformBlock
{
    vec2 scale;
    vec2 translate;
    uint srgbTarget;
}
gTransform;

#ifdef AZOTH_RHI_IMGUI_VERTEX_STAGE

// Locations follow the RHI's vertex ABI, the attribute at location N being the Nth the input layout declares.
layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec2 inUv;
layout(location = 2) in vec4 inColor;

layout(location = 0) out vec2 outUv;
layout(location = 1) out vec4 outColor;

void main()
{
    // ImGui works in pixels with the origin top left, so this turns that into clip space. Two multiplies and two adds and not a matrix, since a projection with
    // no rotation in it is exactly that much arithmetic.
    gl_Position = vec4((inPosition * gTransform.scale) + gTransform.translate, 0.0, 1.0);
    outUv       = inUv;
    outColor    = inColor;
}

#else

layout(location = 0) in vec2 inUv;
layout(location = 1) in vec4 inColor;

layout(location = 0) out vec4 outColor;

// Separate image and sampler and not a combined one, because that is what the two bindings the layout declares are.
layout(set = 0, binding = 0) uniform texture2D gImage;
layout(set = 0, binding = 1) uniform sampler gSampler;

/*
 * ImGui authors its style colours as the values it wants to see on screen, a display referred number.
 *
 * On an sRGB target the hardware encodes whatever the shader writes, so handing it 0.26 stores something near 0.54 and the interface comes out washed. Undoing
 * the encode here means the hardware's own encode puts the number back. On a plain unorm target nothing is encoded either way.
 */
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

    // Alpha is never sRGB encoded, on any target, so it is left alone above and multiplies through unchanged here.
    outColor = tint * texture(sampler2D(gImage, gSampler), inUv);
}

#endif

#version 450

layout(set = 0, binding = 0) uniform Globals
{
    mat4 viewProjection;
    vec4 lightDirection;
    vec4 cameraPosition;
} globals;

layout(push_constant) uniform Push
{
    mat4 model;
} push;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUv;

layout(location = 0) out vec3 outWorld;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec2 outUv;

void main()
{
    vec4 world = push.model * vec4(inPosition, 1.0);
    outWorld = world.xyz;
    outNormal = mat3(push.model) * inNormal;
    outUv = inUv;
    gl_Position = globals.viewProjection * world;
}

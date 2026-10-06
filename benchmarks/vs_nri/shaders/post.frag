#version 450

layout(location = 0) in vec2 inUv;

layout(set = 0, binding = 0) uniform texture2D sceneColor;
layout(set = 0, binding = 1) uniform sampler sceneSampler;

layout(location = 0) out vec4 outColor;

void main()
{
	outColor = vec4(1.0) - texture(sampler2D(sceneColor, sceneSampler), inUv);
}

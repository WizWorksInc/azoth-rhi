#version 450

layout(location = 0) in vec4 inTint;

layout(set = 1, binding = 0) uniform Material
{
	vec4 color;
} material;

layout(set = 2, binding = 0) uniform texture2D shadowMap;
layout(set = 2, binding = 1) uniform sampler shadowSampler;

layout(location = 0) out vec4 outColor;

void main()
{
	const float shadow = texture(sampler2D(shadowMap, shadowSampler), gl_FragCoord.xy / 512.0).r;
	outColor		   = material.color * inTint * shadow;
}

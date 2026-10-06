#version 450

layout(location = 0) in vec3 inPosition;

layout(set = 0, binding = 0) readonly buffer Objects
{
	vec4 offsets[];
} objects;

layout(push_constant) uniform Push
{
	uint object;
	uint material;
	uint pad0;
	uint pad1;
	vec4 tint;
	vec4 extra[2];
} push;

layout(location = 0) out vec4 outTint;

void main()
{
	const vec4 offset = objects.offsets[push.object];
	gl_Position		  = vec4(inPosition * offset.w + offset.xyz, 1.0);
	outTint			  = push.tint;
}

#version 450

layout(set = 0, binding = 0) uniform Globals
{
    mat4 viewProjection;
    vec4 lightDirection;
    vec4 cameraPosition;
} globals;

layout(set = 0, binding = 1) uniform sampler materialSampler;

// Base color, normal and metallic-roughness.
layout(set = 1, binding = 0) uniform texture2D materialTextures[3];

layout(location = 0) in vec3 inWorld;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUv;

layout(location = 0) out vec4 outColor;

void main()
{
    vec4 baseColor = texture(sampler2D(materialTextures[0], materialSampler), inUv);
    vec3 detail = texture(sampler2D(materialTextures[1], materialSampler), inUv).xyz * 2.0 - 1.0;
    vec2 roughnessMetal = texture(sampler2D(materialTextures[2], materialSampler), inUv).gb;

    vec3 normal = normalize(normalize(inNormal) + 0.25 * detail);
    vec3 toLight = -normalize(globals.lightDirection.xyz);
    vec3 toCamera = normalize(globals.cameraPosition.xyz - inWorld);
    vec3 halfway = normalize(toLight + toCamera);

    float diffuse = max(dot(normal, toLight), 0.0);
    float shininess = mix(96.0, 4.0, roughnessMetal.x);
    float specular = pow(max(dot(normal, halfway), 0.0), shininess);
    vec3 tint = mix(vec3(1.0), baseColor.rgb, roughnessMetal.y);

    outColor = vec4(baseColor.rgb * (0.08 + diffuse) + specular * tint, 1.0);
}

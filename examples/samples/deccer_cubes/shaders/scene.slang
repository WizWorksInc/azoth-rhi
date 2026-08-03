struct DrawConstants
{
    float4x4 model;
    float4   normalRow0;
    float4   normalRow1;
    float4   normalRow2;
    float    metallic;
    float    roughness;

    // Stated, not left to each target. The members align the struct to sixteen, so the two floats above leave eight bytes at the end and a push of the unpadded
    // size would be a block shorter than the shader declares.
    float2   pad;
};

struct Frame
{
    float4x4 viewProjection;
    float4   cameraPosition;

    // The camera's axes, with the tangent of half the field of view in w. The sky below turns a screen corner into a direction from these.
    float4   cameraRight;
    float4   cameraUp;
    float4   cameraForward;

    // How many levels the specular cube has, which is what roughness is scaled by to pick one.
    float4   specularMips;
};

[[vk::push_constant]]
ConstantBuffer<DrawConstants> gDraw;

/*
 * One ParameterBlock per descriptor set, which is the RHI's binding ABI. A block claims a descriptor set on spirv and a register space on dxil, and its
 * members take bindings in declaration order.
 *
 * Flat globals with vk::binding(index, set) instead lay every set into register space 0 on dxil with one counter per register class, which the root signature
 * does not bind and which reads the wrong descriptor without failing anywhere.
 */
struct FrameSet
{
    ConstantBuffer<Frame> frame;
};

struct MaterialSet
{
    Texture2D baseColor;
    TextureCube irradiance;
    TextureCube specular;
    SamplerState samp;
};

ParameterBlock<FrameSet> gFrameSet;
ParameterBlock<MaterialSet> gMaterialSet;

/*
 * Narkowicz's fit of the ACES curve. The environment has a sun in it, so the shading below runs well past one and would clip on the way into the back buffer.
 */
float3 Tonemap(float3 colour)
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return saturate((colour * ((a * colour) + b)) / ((colour * ((c * colour) + d)) + e));
}

// Semantics follow the RHI's vertex ABI: attribute at location N is TEXCOORDN, which is what the input layout below declares.
struct VertexInput
{
    float3 position : TEXCOORD0;
    float3 normal   : TEXCOORD1;
    float2 uv       : TEXCOORD2;
};

struct VertexOutput
{
    float4 position      : SV_Position;
    float3 worldPosition : POSITION1;
    float3 worldNormal   : NORMAL;
    float2 uv            : TEXCOORD0;
};

[shader("vertex")]
VertexOutput vertexMain(VertexInput input)
{
    const float4 world = mul(gDraw.model, float4(input.position, 1.0));

    VertexOutput output;
    output.position      = mul(gFrameSet.frame.viewProjection, world);
    output.worldPosition = world.xyz;
    // The rows of the inverse transpose, so a non-uniformly scaled mesh lights as its shape, not its scale.
    output.worldNormal   = float3(dot(gDraw.normalRow0.xyz, input.normal),
                                  dot(gDraw.normalRow1.xyz, input.normal),
                                  dot(gDraw.normalRow2.xyz, input.normal));
    output.uv            = input.uv;
    return output;
}

/*
 * The half of the split-sum approximation that depends on nothing but roughness and viewing angle, normally baked into a small lookup texture. This is
 * Karis's mobile fit of that table, which lands within a percent of it and saves the sample a fourth compute pass.
 */
float2 EnvironmentBrdf(float roughness, float ndotv)
{
    const float4 c0 = float4(-1.0, -0.0275, -0.572, 0.022);
    const float4 c1 = float4(1.0, 0.0425, 1.04, -0.04);
    const float4 r = (roughness * c0) + c1;
    const float scaled = (min(r.x * r.x, exp2(-9.28 * ndotv)) * r.x) + r.y;
    return (float2(-1.04, 1.04) * scaled) + r.zw;
}

// Reflectance rises toward edge-on views. Rough surfaces do it less, hence the roughness term.
float3 FresnelRoughness(float ndotv, float3 f0, float roughness)
{
    const float3 ceiling = max(float3(1.0 - roughness, 1.0 - roughness, 1.0 - roughness), f0);
    return f0 + ((ceiling - f0) * pow(saturate(1.0 - ndotv), 5.0));
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target
{
    const float3 normal   = normalize(input.worldNormal);
    const float3 toCamera = normalize(gFrameSet.frame.cameraPosition.xyz - input.worldPosition);
    const float ndotv     = saturate(dot(normal, toCamera));

    // Sampling an sRGB texture decodes, and writing an sRGB back buffer encodes, so the arithmetic between them is in linear light.
    const float3 baseColor = gMaterialSet.baseColor.Sample(gMaterialSet.samp, input.uv).rgb;
    const float metallic   = saturate(gDraw.metallic);

    // Nothing is perfectly smooth and a roughness of zero makes the reflection a single direction, which no amount of prefiltering resolves.
    const float roughness = clamp(gDraw.roughness, 0.045, 1.0);

    // Anything that is not a metal reflects a few percent of what hits it whatever colour it is. A metal reflects its own colour and diffuses none.
    const float3 f0 = lerp(float3(0.04, 0.04, 0.04), baseColor, metallic);

    const float3 irradiance = gMaterialSet.irradiance.Sample(gMaterialSet.samp, normal).rgb;
    const float3 fresnel    = FresnelRoughness(ndotv, f0, roughness);
    const float3 diffuse    = (1.0 - fresnel) * (1.0 - metallic) * baseColor * irradiance;

    // Out of the mip that was convolved for this roughness.
    const float3 reflection  = reflect(-toCamera, normal);
    const float3 prefiltered = gMaterialSet.specular.SampleLevel(gMaterialSet.samp, reflection, roughness * (gFrameSet.frame.specularMips.x - 1.0)).rgb;
    const float2 brdf        = EnvironmentBrdf(roughness, ndotv);
    const float3 specular    = prefiltered * ((f0 * brdf.x) + brdf.y);

    return float4(Tonemap(diffuse + specular), 1.0);
}

/*
 * The sky, drawn as one screen-covering triangle before anything else with no depth state, so the scene lands on top of it. It reads level zero of the
 * specular chain and not a cube of its own: at roughness zero the prefilter copies the environment straight through, so that level is the environment
 * exactly. That holds only while the chain starts at the environment's own size, which is why ibl.cpp derives one from the other.
 */
struct SkyOutput
{
    float4 position : SV_Position;
    float3 ray      : POSITION1;
};

[shader("vertex")]
SkyOutput skyVertexMain(uint id : SV_VertexID)
{
    const float2 corner = float2(float((id << 1) & 2), float(id & 2));
    const float2 ndc    = (corner * 2.0) - 1.0;

    SkyOutput output;
    output.position = float4(ndc, 1.0, 1.0);
    output.ray      = gFrameSet.frame.cameraForward.xyz
                    + (gFrameSet.frame.cameraRight.xyz * ndc.x * gFrameSet.frame.cameraRight.w)
                    + (gFrameSet.frame.cameraUp.xyz * ndc.y * gFrameSet.frame.cameraUp.w);
    return output;
}

[shader("fragment")]
float4 skyFragmentMain(SkyOutput input) : SV_Target
{
    return float4(Tonemap(gMaterialSet.specular.SampleLevel(gMaterialSet.samp, normalize(input.ray), 0.0).rgb), 1.0);
}

struct DrawConstants
{
	float4x4 model;
	float4 normalRow0;
	float4 normalRow1;
	float4 normalRow2;
	float metallic;
	float roughness;

	float2 pad;
};

struct Frame
{
	float4x4 viewProjection;
	float4 cameraPosition;

	float4 cameraRight;
	float4 cameraUp;
	float4 cameraForward;

	float4 specularMips;
};

[[vk::push_constant]] ConstantBuffer<DrawConstants> gDraw;

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

float3 Tonemap(float3 colour)
{
	const float a = 2.51;
	const float b = 0.03;
	const float c = 2.43;
	const float d = 0.59;
	const float e = 0.14;
	return saturate((colour * ((a * colour) + b)) / ((colour * ((c * colour) + d)) + e));
}

struct VertexInput
{
	float3 position : TEXCOORD0;
	float3 normal : TEXCOORD1;
	float2 uv : TEXCOORD2;
};

struct VertexOutput
{
	float4 position : SV_Position;
	float3 worldPosition : POSITION1;
	float3 worldNormal : NORMAL;
	float2 uv : TEXCOORD0;
};

[shader("vertex")] VertexOutput vertexMain(VertexInput input)
{
	const float4 world = mul(gDraw.model, float4(input.position, 1.0));

	VertexOutput output;
	output.position		 = mul(gFrameSet.frame.viewProjection, world);
	output.worldPosition = world.xyz;
	output.worldNormal = float3(dot(gDraw.normalRow0.xyz, input.normal), dot(gDraw.normalRow1.xyz, input.normal), dot(gDraw.normalRow2.xyz, input.normal));
	output.uv		   = input.uv;
	return output;
}

float2 EnvironmentBrdf(float roughness, float ndotv)
{
	const float4 c0	   = float4(-1.0, -0.0275, -0.572, 0.022);
	const float4 c1	   = float4(1.0, 0.0425, 1.04, -0.04);
	const float4 r	   = (roughness * c0) + c1;
	const float scaled = (min(r.x * r.x, exp2(-9.28 * ndotv)) * r.x) + r.y;
	return (float2(-1.04, 1.04) * scaled) + r.zw;
}

float3 FresnelRoughness(float ndotv, float3 f0, float roughness)
{
	const float3 ceiling = max(float3(1.0 - roughness, 1.0 - roughness, 1.0 - roughness), f0);
	return f0 + ((ceiling - f0) * pow(saturate(1.0 - ndotv), 5.0));
}

[shader("fragment")] float4 fragmentMain(VertexOutput input) : SV_Target
{
	const float3 normal	  = normalize(input.worldNormal);
	const float3 toCamera = normalize(gFrameSet.frame.cameraPosition.xyz - input.worldPosition);
	const float ndotv	  = saturate(dot(normal, toCamera));

	const float3 baseColor = gMaterialSet.baseColor.Sample(gMaterialSet.samp, input.uv).rgb;
	const float metallic   = saturate(gDraw.metallic);

	const float roughness = clamp(gDraw.roughness, 0.045, 1.0);

	const float3 f0 = lerp(float3(0.04, 0.04, 0.04), baseColor, metallic);

	const float3 irradiance = gMaterialSet.irradiance.Sample(gMaterialSet.samp, normal).rgb;
	const float3 fresnel	= FresnelRoughness(ndotv, f0, roughness);
	const float3 diffuse	= (1.0 - fresnel) * (1.0 - metallic) * baseColor * irradiance;

	const float3 reflection	 = reflect(-toCamera, normal);
	const float3 prefiltered = gMaterialSet.specular.SampleLevel(gMaterialSet.samp, reflection, roughness * (gFrameSet.frame.specularMips.x - 1.0)).rgb;
	const float2 brdf		 = EnvironmentBrdf(roughness, ndotv);
	const float3 specular	 = prefiltered * ((f0 * brdf.x) + brdf.y);

	return float4(Tonemap(diffuse + specular), 1.0);
}

struct SkyOutput
{
	float4 position : SV_Position;
	float3 ray : POSITION1;
};

[shader("vertex")] SkyOutput skyVertexMain(uint id : SV_VertexID)
{
	const float2 corner = float2(float((id << 1) & 2), float(id & 2));
	const float2 ndc	= (corner * 2.0) - 1.0;

	SkyOutput output;
	output.position = float4(ndc, 1.0, 1.0);
	output.ray		= gFrameSet.frame.cameraForward.xyz + (gFrameSet.frame.cameraRight.xyz * ndc.x * gFrameSet.frame.cameraRight.w) +
					  (gFrameSet.frame.cameraUp.xyz * ndc.y * gFrameSet.frame.cameraUp.w);
	return output;
}

	[shader("fragment")] float4 skyFragmentMain(SkyOutput input)
	: SV_Target
{
	return float4(Tonemap(gMaterialSet.specular.SampleLevel(gMaterialSet.samp, normalize(input.ray), 0.0).rgb), 1.0);
}

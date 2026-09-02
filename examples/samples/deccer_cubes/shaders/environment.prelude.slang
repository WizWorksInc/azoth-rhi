static const float kPi = 3.14159265;

static const float3 kSunDirection = normalize(float3(0.663, 0.574, -0.481));

float3 Sky(float3 direction)
{
	const float3 ground	 = float3(0.030, 0.028, 0.025);
	const float3 horizon = float3(0.190, 0.205, 0.235);
	const float3 zenith	 = float3(0.045, 0.085, 0.200);

	float3 sky = lerp(horizon, zenith, pow(saturate(direction.y), 0.55));
	sky		   = lerp(ground, sky, saturate((direction.y * 24.0) + 0.5));

	const float sun = saturate(dot(direction, kSunDirection));
	sky += pow(sun, 96.0) * 60.0;
	sky += pow(sun, 4.0) * 0.30;

	return sky;
}

float3 CubeDirection(uint face, float2 uv)
{
	if (face == 0)
	{
		return normalize(float3(1.0, -uv.y, -uv.x));
	}
	if (face == 1)
	{
		return normalize(float3(-1.0, -uv.y, uv.x));
	}
	if (face == 2)
	{
		return normalize(float3(uv.x, 1.0, uv.y));
	}
	if (face == 3)
	{
		return normalize(float3(uv.x, -1.0, -uv.y));
	}
	if (face == 4)
	{
		return normalize(float3(uv.x, -uv.y, 1.0));
	}
	return normalize(float3(-uv.x, -uv.y, -1.0));
}

struct Constants
{
	uint size;
	float roughness;

	uint sourceSize;
};

[[vk::push_constant]] ConstantBuffer<Constants> gConstants;

bool FaceTexel(uint3 id, out float3 direction)
{
	direction = float3(0.0, 0.0, 0.0);
	if (id.x >= gConstants.size || id.y >= gConstants.size)
	{
		return false;
	}

	const float2 uv = ((float2(id.xy) + 0.5) / float(gConstants.size) * 2.0) - 1.0;
	direction		= CubeDirection(id.z, uv);
	return true;
}

struct ConvolveSet
{
	RWTexture2DArray<float4> output;
	TextureCube environment;
	SamplerState samp;
};

ParameterBlock<ConvolveSet> gSet;

[shader("compute")][numthreads(8, 8, 1)] void irradianceMain(uint3 id : SV_DispatchThreadID)
{
	float3 normal;
	if (!FaceTexel(id, normal))
	{
		return;
	}

	const float3 up		 = abs(normal.y) < 0.999 ? float3(0.0, 1.0, 0.0) : float3(0.0, 0.0, 1.0);
	const float3 right	 = normalize(cross(up, normal));
	const float3 forward = cross(normal, right);

	float3 sum		 = float3(0.0, 0.0, 0.0);
	float samples	 = 0.0;
	const float step = 0.045;
	for (float phi = 0.0; phi < 2.0 * kPi; phi += step)
	{
		for (float theta = 0.0; theta < 0.5 * kPi; theta += step)
		{
			const float3 tangent   = float3(sin(theta) * cos(phi), sin(theta) * sin(phi), cos(theta));
			const float3 direction = (tangent.x * right) + (tangent.y * forward) + (tangent.z * normal);
			sum += gSet.environment.SampleLevel(gSet.samp, direction, 0.0).rgb * cos(theta) * sin(theta);
			samples += 1.0;
		}
	}

	gSet.output[id] = float4(kPi * sum / max(samples, 1.0), 1.0);
}

float RadicalInverse(uint bits)
{
	bits = (bits << 16u) | (bits >> 16u);
	bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
	bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
	bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
	bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
	return float(bits) * 2.3283064365386963e-10;
}

float3 ImportanceSampleGgx(float2 random, float3 normal, float roughness)
{
	const float alpha	 = roughness * roughness;
	const float phi		 = 2.0 * kPi * random.x;
	const float cosTheta = sqrt((1.0 - random.y) / (1.0 + (((alpha * alpha) - 1.0) * random.y)));
	const float sinTheta = sqrt(1.0 - (cosTheta * cosTheta));

	const float3 halfway   = float3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
	const float3 up		   = abs(normal.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
	const float3 tangent   = normalize(cross(up, normal));
	const float3 bitangent = cross(normal, tangent);

	return normalize((tangent * halfway.x) + (bitangent * halfway.y) + (normal * halfway.z));
}

[shader("compute")][numthreads(8, 8, 1)] void prefilterMain(uint3 id : SV_DispatchThreadID)
{
	float3 normal;
	if (!FaceTexel(id, normal))
	{
		return;
	}

	if (gConstants.roughness <= 0.0)
	{
		gSet.output[id] = float4(gSet.environment.SampleLevel(gSet.samp, normal, 0.0).rgb, 1.0);
		return;
	}

	const uint kSamples = 128;

	const float alpha	  = gConstants.roughness * gConstants.roughness;
	const float texelArea = (4.0 * kPi) / (6.0 * float(gConstants.sourceSize) * float(gConstants.sourceSize));

	float3 sum	 = float3(0.0, 0.0, 0.0);
	float weight = 0.0;
	for (uint i = 0; i < kSamples; ++i)
	{
		const float2 random	 = float2(float(i) / float(kSamples), RadicalInverse(i));
		const float3 halfway = ImportanceSampleGgx(random, normal, gConstants.roughness);
		const float3 light	 = (2.0 * dot(normal, halfway) * halfway) - normal;

		const float ndotl = dot(normal, light);
		if (ndotl > 0.0)
		{
			const float ndoth	   = max(dot(normal, halfway), 0.0);
			const float denom	   = ((ndoth * ndoth) * ((alpha * alpha) - 1.0)) + 1.0;
			const float ggx		   = (alpha * alpha) / max(kPi * denom * denom, 0.0001);
			const float sampleArea = 4.0 / max(float(kSamples) * ggx, 0.0001);

			const float level = 0.5 * log2(sampleArea / texelArea);

			sum += gSet.environment.SampleLevel(gSet.samp, light, max(level, 0.0)).rgb * ndotl;
			weight += ndotl;
		}
	}

	gSet.output[id] = float4(sum / max(weight, 0.0001), 1.0);
}

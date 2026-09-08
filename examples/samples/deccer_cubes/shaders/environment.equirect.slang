struct EquirectSet
{
	RWTexture2DArray<float4> output;
	Texture2D<float4> source;
	SamplerState samp;
};

ParameterBlock<EquirectSet> gSet;

[shader("compute")][numthreads(8, 8, 1)] void equirectMain(uint3 id : SV_DispatchThreadID)
{
	float3 direction;
	if (!FaceTexel(id, direction))
	{
		return;
	}

	const float longitude = atan2(direction.z, direction.x);
	const float latitude  = asin(clamp(direction.y, -1.0, 1.0));

	const float2 uv = float2((longitude / (2.0 * kPi)) + 0.5, 0.5 - (latitude / kPi));

	gSet.output[id] = float4(gSet.source.SampleLevel(gSet.samp, uv, 0.0).rgb, 1.0);
}

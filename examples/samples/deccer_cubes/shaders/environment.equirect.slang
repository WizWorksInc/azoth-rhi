struct EquirectSet
{
    RWTexture2DArray<float4> output;
    Texture2D<float4> source;
    SamplerState samp;
};

ParameterBlock<EquirectSet> gSet;

/*
 * An equirectangular photograph projected onto the cube the two convolutions below read.
 *
 * The same first pass as the procedural sky, taking its radiance from an image instead of from a formula. Everything after this is unchanged, which is the
 * point: the convolutions do not care where the cube came from.
 */
[shader("compute")]
[numthreads(8, 8, 1)]
void equirectMain(uint3 id : SV_DispatchThreadID)
{
    float3 direction;
    if (!FaceTexel(id, direction))
    {
        return;
    }

    // Longitude runs the long way round and latitude from pole to pole, which is the mapping every equirectangular image is stored in.
    const float longitude = atan2(direction.z, direction.x);
    const float latitude = asin(clamp(direction.y, -1.0, 1.0));

    const float2 uv = float2((longitude / (2.0 * kPi)) + 0.5, 0.5 - (latitude / kPi));

    // Level zero and not a sampled mip: the source has none, and letting the hardware pick would read black outside the one level there is.
    gSet.output[id] = float4(gSet.source.SampleLevel(gSet.samp, uv, 0.0).rgb, 1.0);
}

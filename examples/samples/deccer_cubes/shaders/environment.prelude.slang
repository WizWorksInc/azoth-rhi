static const float kPi = 3.14159265;

static const float3 kSunDirection = normalize(float3(0.663, 0.574, -0.481));

/*
 * The sky in closed form, so this sample lights itself without an HDR image to fetch, decode and ship. The sun is deliberately broad: a small bright one
 * would need thousands more samples in each of the two convolutions below before the reflections stopped sparkling.
 */
float3 Sky(float3 direction)
{
    const float3 ground  = float3(0.030, 0.028, 0.025);
    const float3 horizon = float3(0.190, 0.205, 0.235);
    const float3 zenith  = float3(0.045, 0.085, 0.200);

    float3 sky = lerp(horizon, zenith, pow(saturate(direction.y), 0.55));
    sky = lerp(ground, sky, saturate((direction.y * 24.0) + 0.5));

    // The sun has to outweigh the rest of the sky or a scene of plain boxes reads as evenly lit from every side. A tight lobe carrying a large number does it.
    const float sun = saturate(dot(direction, kSunDirection));
    sky += pow(sun, 96.0) * 60.0;
    sky += pow(sun, 4.0) * 0.30;

    return sky;
}

// Which way a texel of a cube face looks, in the face order every one of these APIs agrees on. The uv runs minus one to one.
float3 CubeDirection(uint face, float2 uv)
{
    if (face == 0) { return normalize(float3( 1.0, -uv.y, -uv.x)); }
    if (face == 1) { return normalize(float3(-1.0, -uv.y,  uv.x)); }
    if (face == 2) { return normalize(float3( uv.x,  1.0,  uv.y)); }
    if (face == 3) { return normalize(float3( uv.x, -1.0, -uv.y)); }
    if (face == 4) { return normalize(float3( uv.x, -uv.y,  1.0)); }
    return normalize(float3(-uv.x, -uv.y, -1.0));
}

struct Constants
{
    uint  size;
    float roughness;

    // The environment cube's own face size, which the prefilter needs to know how much of it one texel covers.
    uint  sourceSize;
};

[[vk::push_constant]]
ConstantBuffer<Constants> gConstants;

// The texel this thread owns, or false when the group ran off the edge of the face.
bool FaceTexel(uint3 id, out float3 direction)
{
    direction = float3(0.0, 0.0, 0.0);
    if (id.x >= gConstants.size || id.y >= gConstants.size)
    {
        return false;
    }

    const float2 uv = ((float2(id.xy) + 0.5) / float(gConstants.size) * 2.0) - 1.0;
    direction = CubeDirection(id.z, uv);
    return true;
}


#if !defined( CLOUD_LIGHTING_H )
#define CLOUD_LIGHTING_H

// Sun-transmittance field cache sampling + scattering model (docs/PLAN.md 4.2,
// 4.4). The cache is a 3D texture of optical depth toward the sun, built
// analytically in CloudLightCache-c.hlsl. Sampling is one (or, for six-way, two)
// trilinear fetches; there is no per-pixel shadow ray.
//
// lightMode 0 (sun cache):  g_lightCache.r = optical depth toward the current sun.
// lightMode 1 (six-way):    g_lightCache = (+X,-X,+Y,-Y), g_lightCache1 = (+Z,-Z),
//                           blended by squared-cosine of the sun direction. Stores
//                           tau (not transmittance) and blends-then-exponentiates so
//                           the diagonal-sun error stays bounded.

#include "CloudKernels.hlsli"

Texture3D<float4> g_lightCache  : register(t8);   // sun: .r=tau; six-way: +X,-X,+Y,-Y
Texture3D<float4> g_lightCache1 : register(t3);   // six-way: +Z,-Z (,_,_)

#define CLOUD_CACHE_X 128
#define CLOUD_CACHE_Y 32
#define CLOUD_CACHE_Z 128

// worldPos -> cache UVW in [0,1]. cacheOriginWS.xyz is the world corner of
// voxel (0,0,0); cacheOriginWS.w = 1/voxelSize (cubic voxels).
float3 CacheUVW(float3 worldPos)
{
    float3 dims = float3(CLOUD_CACHE_X, CLOUD_CACHE_Y, CLOUD_CACHE_Z);
    return (worldPos - g_cacheOriginWS.xyz) * g_cacheOriginWS.w / dims;
}

// Sun optical depth at a world point (0 outside the cache box).
float SampleSunTau(float3 worldPos)
{
    float3 uvw = CacheUVW(worldPos);
    if (any(uvw < 0.0) || any(uvw > 1.0))
        return 0.0;

    if (g_mode.y == CLOUD_LIGHT_SIXWAY)
    {
        float4 a = g_lightCache.SampleLevel(linearClampSampler, uvw, 0);    // +X,-X,+Y,-Y
        float2 z = g_lightCache1.SampleLevel(linearClampSampler, uvw, 0).rg; // +Z,-Z
        float3 s = normalize(g_sunDirWS.xyz);
        float wpx = max(s.x, 0.0), wnx = max(-s.x, 0.0);
        float wpy = max(s.y, 0.0), wny = max(-s.y, 0.0);
        float wpz = max(s.z, 0.0), wnz = max(-s.z, 0.0);
        wpx *= wpx; wnx *= wnx; wpy *= wpy; wny *= wny; wpz *= wpz; wnz *= wnz;
        float wsum = wpx + wnx + wpy + wny + wpz + wnz + 1e-4;
        return (a.x * wpx + a.y * wnx + a.z * wpy + a.w * wny + z.x * wpz + z.y * wnz) / wsum;
    }
    return g_lightCache.SampleLevel(linearClampSampler, uvw, 0).r;
}

// Wrenninge multi-octave multiple-scattering approximation of the sun term.
float3 CloudSunScatter(float tauSun, float cosVS)
{
    int taps = (int)g_scatterParams.w;      // msOctaves (e.g. 3)
    float a = 1.0, b = 1.0, c = 1.0;
    float3 sum = 0.0;
    for (int i = 0; i < taps; ++i)
    {
        float phase = lerp(PhaseHG(cosVS, g_scatterParams.y * b), PhaseHG(cosVS, g_scatterParams.x * b), g_scatterParams.z);
        sum += c * exp(-tauSun * a) * phase;
        a *= 0.5; b *= 0.5; c *= 0.5;
    }
    return g_sunRadiance.rgb * sum;
}

#endif

#if !defined( CLOUD_LIGHTING_H )
#define CLOUD_LIGHTING_H

// Sun-transmittance field cache sampling + scattering model (docs/PLAN.md 4.2,
// 4.4). The cache is a 3D texture of optical depth toward the sun, built
// analytically in CloudLightCache-c.hlsl. Sampling is one trilinear fetch;
// there is no per-pixel shadow ray.

#include "CloudKernels.hlsli"

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
float SampleSunTau(Texture3D<float> cacheTex, float3 worldPos)
{
    float3 uvw = CacheUVW(worldPos);
    if (any(uvw < 0.0) || any(uvw > 1.0))
        return 0.0;
    return cacheTex.SampleLevel(linearClampSampler, uvw, 0).r;
}

// Wrenninge multi-octave multiple-scattering approximation of the sun term.
// tauSun is the single-scatter optical depth toward the sun at the sample.
float3 CloudSunScatter(float tauSun, float cosVS)
{
    int taps = (int)g_scatterParams.w;      // msOctaves (e.g. 3)
    float a = 1.0, b = 1.0, c = 1.0;        // (extinction, eccentricity, contribution) scales
    float3 sum = 0.0;
    for (int i = 0; i < taps; ++i)
    {
        // dual-lobe HG with per-octave eccentricity scaling
        float phase = lerp(PhaseHG(cosVS, g_scatterParams.y * b), PhaseHG(cosVS, g_scatterParams.x * b), g_scatterParams.z);
        sum += c * exp(-tauSun * a) * phase;
        a *= 0.5; b *= 0.5; c *= 0.5;
    }
    return g_sunRadiance.rgb * sum;
}

#endif

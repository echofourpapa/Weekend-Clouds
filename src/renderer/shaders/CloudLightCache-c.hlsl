// Sun-transmittance field cache build (docs/PLAN.md 4.2, step P4.2 / six-way P4.3).
// Per voxel: analytic optical depth toward the sun (lightMode 0) or along the six
// cardinal axes (lightMode 1), summed over macros with a cheap perpendicular-
// distance reject (no marching, no macro grid). Time-sliced by Z-slab.

#include "CloudLighting.hlsli"

StructuredBuffer<CloudMacro> g_macros  : register(t1);
RWTexture3D<float4>          g_cacheOut  : register(u6);   // sun: .r; six-way: +X,-X,+Y,-Y
RWTexture3D<float4>          g_cacheOut1 : register(u3);   // six-way: +Z,-Z

#define CACHE_SLAB_Z 8
#define CACHE_SLABS  (CLOUD_CACHE_Z / CACHE_SLAB_Z)   // 16

// Analytic optical depth from a world point along a direction, over all macros.
float SunTauAt(float3 worldPos, float3 dir, uint macroCount)
{
    float tau = 0.0;
    for (uint i = 0; i < macroCount; ++i)
    {
        CloudMacro m = g_macros[i];
        float3 c = float3(m.position[0], m.position[1], m.position[2]);
        float3 rel = c - worldPos;
        float along = dot(rel, dir);
        if (along < -m.boundRadius) continue;
        float3 perp = rel - along * dir;
        if (dot(perp, perp) > m.boundRadius * m.boundRadius) continue;

        CloudKernel k = KernelFromMacro(m);
        KernelRayTerms t = KernelRaySetup(k, worldPos, dir);
        tau += max(TauKernelClamped(k.amplitude, t, 0.0, 0.0, CLOUD_BIG_T), 0.0);
    }
    return tau;
}

[numthreads(4, 4, 4)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    uint slab = g_genParams.w % CACHE_SLABS;
    uint3 voxel = uint3(dtid.x, dtid.y, dtid.z + slab * CACHE_SLAB_Z);
    if (voxel.x >= CLOUD_CACHE_X || voxel.y >= CLOUD_CACHE_Y || voxel.z >= CLOUD_CACHE_Z)
        return;

    float voxelSize = 1.0 / g_cacheOriginWS.w;
    float3 worldPos = g_cacheOriginWS.xyz + (float3(voxel) + 0.5) * voxelSize;
    uint macroCount = g_counts.x;

    if (g_mode.y == CLOUD_LIGHT_SIXWAY)
    {
        g_cacheOut[voxel] = float4(
            SunTauAt(worldPos, float3( 1, 0, 0), macroCount),
            SunTauAt(worldPos, float3(-1, 0, 0), macroCount),
            SunTauAt(worldPos, float3( 0, 1, 0), macroCount),
            SunTauAt(worldPos, float3( 0,-1, 0), macroCount));
        g_cacheOut1[voxel] = float4(
            SunTauAt(worldPos, float3( 0, 0, 1), macroCount),
            SunTauAt(worldPos, float3( 0, 0,-1), macroCount), 0.0, 0.0);
    }
    else
    {
        g_cacheOut[voxel] = float4(SunTauAt(worldPos, normalize(g_sunDirWS.xyz), macroCount), 0, 0, 0);
    }
}

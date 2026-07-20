// Sun-transmittance field cache build (docs/PLAN.md 4.2, step P4.2).
// Per voxel: analytic optical depth toward the sun, summed over macros with a
// cheap perpendicular-distance reject (no marching, no macro grid - the
// primitives integrate in closed form). Time-sliced by Z-slab: g_genParams.w
// (cacheSliceIndex) selects one of CACHE_SLABS slabs per frame, so the whole
// volume refreshes over CACHE_SLABS frames as the sun/cloudscape change.

#include "CloudLighting.hlsli"

StructuredBuffer<CloudMacro> g_macros    : register(t1);
RWTexture3D<float>           g_cacheOut   : register(u6);

#define CACHE_SLAB_Z 8
#define CACHE_SLABS  (CLOUD_CACHE_Z / CACHE_SLAB_Z)   // 16

[numthreads(4, 4, 4)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    uint slab = g_genParams.w % CACHE_SLABS;
    uint3 voxel = uint3(dtid.x, dtid.y, dtid.z + slab * CACHE_SLAB_Z);
    if (voxel.x >= CLOUD_CACHE_X || voxel.y >= CLOUD_CACHE_Y || voxel.z >= CLOUD_CACHE_Z)
        return;

    float voxelSize = 1.0 / g_cacheOriginWS.w;
    float3 worldPos = g_cacheOriginWS.xyz + (float3(voxel) + 0.5) * voxelSize;
    float3 sunDir = normalize(g_sunDirWS.xyz);

    uint macroCount = g_counts.x;
    float tau = 0.0;
    for (uint i = 0; i < macroCount; ++i)
    {
        CloudMacro m = g_macros[i];
        float3 c = float3(m.position[0], m.position[1], m.position[2]);
        float3 rel = c - worldPos;
        float along = dot(rel, sunDir);
        if (along < -m.boundRadius) continue;                 // fully behind the sample
        float3 perp = rel - along * sunDir;
        if (dot(perp, perp) > m.boundRadius * m.boundRadius) continue;   // sun ray misses it

        CloudKernel k = KernelFromMacro(m);
        KernelRayTerms t = KernelRaySetup(k, worldPos, sunDir);
        tau += max(TauKernelClamped(k.amplitude, t, 0.0, 0.0, CLOUD_BIG_T), 0.0);
    }

    g_cacheOut[voxel] = tau;
}

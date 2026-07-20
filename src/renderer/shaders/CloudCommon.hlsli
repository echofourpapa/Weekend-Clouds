#if !defined( CLOUD_COMMON_H )
#define CLOUD_COMMON_H

// Shared constants + helpers for the cloud renderer.
// Layout contract: docs/PLAN.md 3.4 — the C++ mirror lives in CloudSystem.h.
// Keep field order/packing identical or the GPU reads garbage.

#include "Compute_Header.hlsli"

#define CLOUD_BIG_T        1e30
#define CLOUD_MAX_TILE_MACROS 64
#define CLOUD_TILE_PX      16

// mode.x — debug views (docs/PLAN.md A2)
#define CLOUD_DBG_OFF          0
#define CLOUD_DBG_HEATMAP      1
#define CLOUD_DBG_FREQBANDS    2
#define CLOUD_DBG_TRANSMIT     3
#define CLOUD_DBG_DEPTH        4
#define CLOUD_DBG_MARCH_DIFF   5
#define CLOUD_DBG_LIGHT_DIFF   6
#define CLOUD_DBG_MASK_RATE    7
#define CLOUD_DBG_HISTORY_REJ  8
#define CLOUD_DBG_MIN_TAU      9
#define CLOUD_DBG_TILE_COUNT   10
#define CLOUD_DBG_CACHE_SLICE  11

// mode.y — lighting mode
#define CLOUD_LIGHT_SUNCACHE   0
#define CLOUD_LIGHT_SIXWAY     1
#define CLOUD_LIGHT_REFERENCE  2

// mode.z — traversal mode
#define CLOUD_TRAV_TILED       0
#define CLOUD_TRAV_RQ_MACRO    1
#define CLOUD_TRAV_RQ_KERNEL   2
#define CLOUD_TRAV_BRUTE       3

cbuffer CloudConstants : register(b0)
{
    row_major float4x4 g_invViewProj;    // unjittered, current frame
    row_major float4x4 g_prevViewProj;   // unjittered, previous frame
    float4   g_camPosWS;       // xyz cam pos, w = time (s)
    float4   g_sunDirWS;       // xyz normalized TOWARD sun, w = sun angular radius (rad)
    float4   g_sunRadiance;    // rgb, w = exposure hint
    float4   g_windOffset;     // xyz accumulated advection (m), w = wind speed
    float4   g_windPhaseVel;   // per-octave phase velocity (turns/s)
    float4   g_cacheOriginWS;  // xyz light-cache voxel(0,0,0) origin, w = 1/voxelSize
    float4   g_traceSize;      // x,y = trace res, z,w = 1/res
    float4   g_outputSize;     // x,y = output res, z,w = 1/res
    uint4    g_counts;         // x macroCount, y kernelCount, z tileCountX, w tileCountY
    float4   g_lodParams;      // x footprintScale, y lodSkipThreshold, z maskAggressiveness, w survivalFloor
    float4   g_scatterParams;  // x hgG0, y hgG1, z hgBlend, w msOctaves
    float4   g_ambientParams;  // x ambientStrength, y groundAlbedo, z cloudBaseY, w cloudTopY
    uint4    g_mode;           // x debugView, y lightMode, z traversalMode, w frameIndex
    float4   g_temporal;       // x alphaBase, y disocclusionTauDelta, z accumCount, w histBlendMax
    float4   g_skyParams;      // x turbidity, y groundOffsetKm, z lutPass, w flags (bit0 accumulate)
    uint4    g_genParams;      // x seed, y kernelsPerMacroPerOctave, z octaveCount, w cacheSliceIndex
    float4   g_erosionParams;  // x erosionBoundK, y detailPosClampSigma, z minSigmaM, w maxSigmaM
};

// ---------------------------------------------------------------------------
// Ray setup (reverse-Z engine; depth SRV is R32F cast of D32; depth 0 == far)
// ---------------------------------------------------------------------------

float3 CloudRayDir(float2 pixel, float2 invRes)
{
    float2 uv = (pixel + 0.5) * invRes;
    float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    float4 pFar = mul(float4(ndc, 0.0, 1.0), g_invViewProj);  // reverse-Z: far plane at depth 0
    float3 world = pFar.xyz / pFar.w;
    return normalize(world - g_camPosWS.xyz);
}

float3 CloudWorldPosFromDepth(float2 uv, float depth)
{
    float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    float4 p = mul(float4(ndc, depth, 1.0), g_invViewProj);
    return p.xyz / p.w;
}

// Distance along rayDir to the opaque scene, or CLOUD_BIG_T for sky.
float CloudGeomT(float2 uv, float depth, float3 rayDir)
{
    if (depth <= 0.0)
        return CLOUD_BIG_T;
    float3 posWS = CloudWorldPosFromDepth(uv, depth);
    return dot(posWS - g_camPosWS.xyz, rayDir);
}

// ---------------------------------------------------------------------------
// Hashing — PCG3D (Jarzynski & Olano, JCGT 2020)
// ---------------------------------------------------------------------------

uint3 PCG3D(uint3 v)
{
    v = v * 1664525u + 1013904223u;
    v.x += v.y * v.z; v.y += v.z * v.x; v.z += v.x * v.y;
    v ^= v >> 16u;
    v.x += v.y * v.z; v.y += v.z * v.x; v.z += v.x * v.y;
    return v;
}

float Hash01(uint3 seed)
{
    return PCG3D(seed).x * (1.0 / 4294967296.0);
}

float3 Hash3_01(uint3 seed)
{
    return PCG3D(seed) * (1.0 / 4294967296.0);
}

// ---------------------------------------------------------------------------
// Phase function
// ---------------------------------------------------------------------------

float PhaseHG(float cosTheta, float g)
{
    float d = 1.0 + g * g - 2.0 * g * cosTheta;
    return (1.0 - g * g) / (4.0 * 3.14159265 * pow(max(d, 1e-4), 1.5));
}

float PhaseDualHG(float cosTheta)
{
    return lerp(PhaseHG(cosTheta, g_scatterParams.y), PhaseHG(cosTheta, g_scatterParams.x), g_scatterParams.z);
}

#endif

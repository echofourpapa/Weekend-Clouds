// Tiled analytic trace (docs/PLAN.md 4.8, step P2.2). Reads the per-tile macro
// list produced by CloudTileBin and integrates only those macros. In Phase 2
// this iterates macros only (pure Gaussians), so it must match the brute path
// pixel-for-pixel (regression gate). Detail Gabor kernels + groupshared staging
// arrive in Phase 3 / Phase 5. Shares the closed-form math in CloudKernels.hlsli.

#include "CloudKernels.hlsli"

StructuredBuffer<CloudMacro> g_macros : register(t1);

struct CloudTile { uint count; uint pad0, pad1, pad2; uint macroIdx[CLOUD_MAX_TILE_MACROS]; };
StructuredBuffer<CloudTile> g_tiles : register(t2);

Texture2D<float>    g_sceneDepth : register(t6);
RWTexture2D<float4> g_scatter    : register(u7);
RWTexture2D<float>  g_cloudDepth : register(u8);

float3 Inferno(float t)
{
    t = saturate(t);
    return saturate(float3(t * 1.6, t * t * 1.2, t * 0.35 + 0.05 * t));
}

COMPUTE_MAIN
{
    uint2 px = IN.DispatchThreadID.xy;
    if (px.x >= (uint)g_traceSize.x || px.y >= (uint)g_traceSize.y) return;

    float2 uv = (px + 0.5) * g_traceSize.zw;
    float3 o = g_camPosWS.xyz;
    float3 dir = CloudRayDir(float2(px), g_traceSize.zw);
    float t1 = CloudGeomT(uv, g_sceneDepth[px], dir);

    uint tileX = px.x / CLOUD_TILE_PX;
    uint tileY = px.y / CLOUD_TILE_PX;
    uint tileIndex = tileY * g_counts.z + tileX;
    CloudTile tile = g_tiles[tileIndex];
    uint n = min(tile.count, (uint)CLOUD_MAX_TILE_MACROS);

    float tau = 0.0;
    float bestTau = 0.0;
    float bestT = 0.0;
    for (uint i = 0; i < n; ++i)
    {
        CloudMacro m = g_macros[tile.macroIdx[i]];
        CloudKernel k = KernelFromMacro(m);
        KernelRayTerms t = KernelRaySetup(k, o, dir);
        float tk = TauKernelClamped(k.amplitude, t, 0.0, 0.0, t1);
        tau += max(tk, 0.0);
        if (tk > bestTau) { bestTau = tk; bestT = t.tbar; }
    }

    float T = exp(-max(tau, 0.0));
    float cosVS = dot(dir, normalize(g_sunDirWS.xyz));
    float phase = PhaseDualHG(cosVS);
    float3 ambient = float3(0.30, 0.45, 0.65) * g_ambientParams.x;
    float3 inscatter = (1.0 - T) * (g_sunRadiance.rgb * phase * 0.20 + ambient);

    uint dbg = g_mode.x;
    if (dbg == CLOUD_DBG_TRANSMIT)      inscatter = T.xxx;
    else if (dbg == CLOUD_DBG_HEATMAP)  inscatter = Inferno(tile.count / 32.0);
    else if (dbg == CLOUD_DBG_TILE_COUNT) inscatter = Inferno(tile.count / (float)CLOUD_MAX_TILE_MACROS);

    g_scatter[px] = float4(inscatter, T);
    g_cloudDepth[px] = (T > 0.995) ? 0.0 : bestT;
}

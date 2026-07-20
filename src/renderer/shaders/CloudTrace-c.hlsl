// Tiled analytic trace (docs/PLAN.md 4.8, step P2.2). Reads the per-tile macro
// list produced by CloudTileBin and integrates only those macros. In Phase 2
// this iterates macros only (pure Gaussians), so it must match the brute path
// pixel-for-pixel (regression gate). Detail Gabor kernels + groupshared staging
// arrive in Phase 3 / Phase 5. Shares the closed-form math in CloudKernels.hlsli.

#include "CloudKernels.hlsli"

StructuredBuffer<CloudKernelPacked> g_kernels : register(t0);
StructuredBuffer<CloudMacro>        g_macros  : register(t1);

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
    // Whole-field advection: +windOffset here == -windOffset on macro centres in
    // CloudTileBin, so binning and trace see the same drifted cloudscape.
    float3 o = g_camPosWS.xyz + g_windOffset.xyz;
    float3 dir = CloudRayDir(float2(px), g_traceSize.zw);
    float t1 = CloudGeomT(uv, g_sceneDepth[px], dir);

    uint tileX = px.x / CLOUD_TILE_PX;
    uint tileY = px.y / CLOUD_TILE_PX;
    uint tileIndex = tileY * g_counts.z + tileX;
    CloudTile tile = g_tiles[tileIndex];
    uint n = min(tile.count, (uint)CLOUD_MAX_TILE_MACROS);

    float tau = 0.0;          // signed accumulation; clamped once at the end
    float bestTau = 0.0;
    float bestT = 0.0;
    float bestHeight = 0.5;   // heightFrac01 of the dominant contributor (for the light gradient)
    for (uint i = 0; i < n; ++i)
    {
        CloudMacro m = g_macros[tile.macroIdx[i]];

        // Macro envelope (the base cloud mass), pure Gaussian.
        CloudKernel mk = KernelFromMacro(m);
        KernelRayTerms mt = KernelRaySetup(mk, o, dir);
        float envTau = TauKernelClamped(mk.amplitude, mt, 0.0, 0.0, t1);
        tau += max(envTau, 0.0);
        if (envTau > bestTau) { bestTau = envTau; bestT = mt.tbar; bestHeight = m.heightFrac01; }

        // Gabor detail kernels erode/build on top (signed).
        uint kbegin = m.detailBegin;
        uint kend = kbegin + m.detailCount;
        for (uint j = kbegin; j < kend; ++j)
        {
            CloudKernel dk = UnpackKernel(g_kernels[j], m);
            KernelRayTerms dt = KernelRaySetup(dk, o, dir);
            tau += TauKernelClamped(dk.amplitude, dt, 0.0, 0.0, t1);
        }
    }

    float tauC = max(tau, 0.0);
    float T = exp(-tauC);

    // Approximate lighting (P4.0, no shadow cache yet): Beer-powder edge
    // darkening + a vertical gradient (bright tops / dark bases) + sky ambient.
    // Physically-correct self-shadowing (analytic sun transmittance from a field
    // cache) is P4.2.
    float cosVS = dot(dir, normalize(g_sunDirWS.xyz));
    float phase = PhaseDualHG(cosVS);
    float powder = 1.0 - exp(-2.0 * tauC);                 // dark cores/edges
    float lightGrad = lerp(0.25, 1.0, saturate(bestHeight));
    float3 sunLit = g_sunRadiance.rgb * phase * powder * lightGrad;
    float3 skyAmb = float3(0.30, 0.45, 0.65) * g_ambientParams.x * (0.4 + 0.6 * saturate(bestHeight));
    float3 inscatter = (1.0 - T) * (sunLit + skyAmb);

    uint dbg = g_mode.x;
    if (dbg == CLOUD_DBG_TRANSMIT)      inscatter = T.xxx;
    else if (dbg == CLOUD_DBG_HEATMAP)  inscatter = Inferno(tile.count / 32.0);
    else if (dbg == CLOUD_DBG_TILE_COUNT) inscatter = Inferno(tile.count / (float)CLOUD_MAX_TILE_MACROS);

    g_scatter[px] = float4(inscatter, T);
    g_cloudDepth[px] = (T > 0.995) ? 0.0 : bestT;
}

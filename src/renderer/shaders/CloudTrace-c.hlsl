// Tiled analytic trace (docs/PLAN.md 4.8, step P2.2). Reads the per-tile macro
// list produced by CloudTileBin and integrates only those macros. In Phase 2
// this iterates macros only (pure Gaussians), so it must match the brute path
// pixel-for-pixel (regression gate). Detail Gabor kernels + groupshared staging
// arrive in Phase 3 / Phase 5. Shares the closed-form math in CloudKernels.hlsli.

#include "CloudLighting.hlsli"

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
    float3 bands = 0.0;       // debug: per-octave contribution (r=o0, g=o1, b=o2, +o3)
    float minTau = 0.0;       // debug: most negative single-kernel contribution (erosion overshoot)
    uint visited = 0, survived = 0;   // debug: masking survival
    for (uint i = 0; i < n; ++i)
    {
        CloudMacro m = g_macros[tile.macroIdx[i]];

        // Macro envelope (the base cloud mass), pure Gaussian.
        CloudKernel mk = KernelFromMacro(m);
        KernelRayTerms mt = KernelRaySetup(mk, o, dir);
        float envTau = TauKernelClamped(mk.amplitude, mt, 0.0, 0.0, t1);
        tau += max(envTau, 0.0);
        if (envTau > bestTau) { bestTau = envTau; bestT = mt.tbar; bestHeight = m.heightFrac01; }

        // Gabor detail kernels erode/build on top (signed), with continuous LOD
        // and bounded stochastic masking (docs/PLAN.md 4.8, C6). LOD attenuation
        // is folded into the kernel's single exp; sub-threshold kernels are
        // skipped; low-contribution kernels are randomly masked (reweighted 1/p)
        // to shorten the loop, trading variance for speed (temporal cleanup, P5.2).
        uint kbegin = m.detailBegin;
        uint kend = kbegin + m.detailCount;
        float maskAggr = g_lodParams.z;
        float survFloor = g_lodParams.w;
        bool synth = g_genParams.y != 0;                      // in-register synthesis (P6.1)
        uint oct = max(g_genParams.z, 1u);
        for (uint j = kbegin; j < kend; ++j)
        {
            CloudKernel dk = synth ? SynthKernel(m, j - kbegin, oct) : UnpackKernel(g_kernels[j], m);
            KernelRayTerms dt = KernelRaySetup(dk, o, dir);

            float lodExp = LodExponent(dk.freqWS, dt.tbar);
            if (lodExp < -3.912) continue;                    // LOD cull: contribution < 2%
            visited++;

            float w = 1.0;
            if (maskAggr > 1.0 && lodExp < -1.386)            // only mask already-small kernels
            {
                float p = clamp(exp(lodExp) / maskAggr, survFloor, 1.0);
                float h = Hash01(uint3(px, dk.seed16 ^ (g_mode.w * 2654435761u)));
                if (h > p) continue;
                w = 1.0 / p;
            }
            survived++;
            float contrib = w * TauKernelClamped(dk.amplitude, dt, lodExp, 0.0, t1);
            tau += contrib;
            minTau = min(minTau, contrib);
            float ac = abs(contrib);
            if (dk.octave == 0) bands.r += ac;
            else if (dk.octave == 1) bands.g += ac;
            else if (dk.octave == 2) bands.b += ac;
            else bands += ac;
        }
    }

    float tauC = max(tau, 0.0);
    float T = exp(-tauC);

    float cosVS = dot(dir, normalize(g_sunDirWS.xyz));
    float powder = 1.0 - exp(-2.0 * tauC);                 // dark cores/edges
    float3 sunLit;
    if (g_mode.y == CLOUD_LIGHT_SUNCACHE || g_mode.y == CLOUD_LIGHT_SIXWAY)
    {
        // Real self-shadowing: sample the field cache at the scatter point
        // (o already includes windOffset, i.e. macro space). SampleSunTau
        // handles both the sun-channel and six-way cache layouts.
        float3 scatterWS = o + dir * bestT;
        float tauSun = SampleSunTau(scatterWS);
        sunLit = CloudSunScatter(tauSun, cosVS) * powder;
    }
    else
    {
        // Fallback heuristic (no cache): height gradient + powder.
        float phase = PhaseDualHG(cosVS);
        sunLit = g_sunRadiance.rgb * phase * powder * lerp(0.25, 1.0, saturate(bestHeight));
    }
    float3 skyAmb = float3(0.30, 0.45, 0.65) * g_ambientParams.x * (0.4 + 0.6 * saturate(bestHeight));
    float3 inscatter = (1.0 - T) * (sunLit + skyAmb);

    uint dbg = g_mode.x;
    if (dbg == CLOUD_DBG_TRANSMIT)          inscatter = T.xxx;
    else if (dbg == CLOUD_DBG_HEATMAP)      inscatter = Inferno(visited / 64.0);
    else if (dbg == CLOUD_DBG_TILE_COUNT)   inscatter = Inferno(tile.count / (float)CLOUD_MAX_TILE_MACROS);
    else if (dbg == CLOUD_DBG_FREQBANDS)    inscatter = bands / max(max(bands.r, bands.g), max(bands.b, 0.02));
    else if (dbg == CLOUD_DBG_DEPTH)        inscatter = frac(bestT / 1000.0).xxx;
    else if (dbg == CLOUD_DBG_MASK_RATE)    inscatter = Inferno(survived / max((float)visited, 1.0));
    else if (dbg == CLOUD_DBG_MIN_TAU)      inscatter = float3(max(tau, 0.0) * 0.2, 0.0, -minTau * 4.0);  // blue = erosion overshoot
    else if (dbg == CLOUD_DBG_CACHE_SLICE)
    {
        // Visualise the light cache at this pixel's scatter altitude.
        float tauSun = SampleSunTau(o + dir * bestT);
        inscatter = Inferno(1.0 - exp(-tauSun));
    }

    g_scatter[px] = float4(inscatter, T);
    g_cloudDepth[px] = (T > 0.995) ? 0.0 : bestT;
}

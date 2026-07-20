// Brute-force analytic trace (docs/PLAN.md 4.8 TRACE_BRUTE, step P1.4).
// Loops every macro as a pure-Gaussian primitive, accumulates the closed-form
// optical depth, and writes (inscatter, transmittance) + cloud depth. No BVH,
// no tiles, no LOD/masking - this exists to prove the erf integral on screen
// against an in-shader reference march (debug view 5). Full resolution.

#include "CloudKernels.hlsli"

StructuredBuffer<CloudMacro> g_macros      : register(t1);
Texture2D<float>             g_sceneDepth   : register(t6);
RWTexture2D<float4>          g_scatter      : register(u7);   // rgb inscatter, a transmittance
RWTexture2D<float>           g_cloudDepth   : register(u8);

COMPUTE_MAIN
{
    uint2 px = IN.DispatchThreadID.xy;
    if (px.x >= (uint)g_traceSize.x || px.y >= (uint)g_traceSize.y) return;

    float2 uv = (px + 0.5) * g_traceSize.zw;
    float3 o = g_camPosWS.xyz;
    float3 dir = CloudRayDir(float2(px), g_traceSize.zw);
    float depth = g_sceneDepth[px];
    float t1 = CloudGeomT(uv, depth, dir);
    float t0 = 0.0;

    uint macroCount = g_counts.x;

    float tau = 0.0;
    float bestTau = 0.0;
    float bestT = 0.0;    // reservoir-lite: distance of the largest contributor

    for (uint i = 0; i < macroCount; ++i)
    {
        CloudMacro m = g_macros[i];
        CloudKernel k = KernelFromMacro(m);
        KernelRayTerms t = KernelRaySetup(k, o, dir);
        float tk = TauKernelClamped(k.amplitude, t, 0.0, t0, t1);
        tau += max(tk, 0.0);
        if (tk > bestTau) { bestTau = tk; bestT = t.tbar; }
    }

    float T = exp(-max(tau, 0.0));

    // Simple placeholder lighting (real scattering arrives in Phase 4).
    float cosVS = dot(dir, normalize(g_sunDirWS.xyz));
    float phase = PhaseDualHG(cosVS);
    float3 sunColor = g_sunRadiance.rgb;
    float3 ambient = float3(0.30, 0.45, 0.65) * g_ambientParams.x;
    float3 inscatter = (1.0 - T) * (sunColor * phase * 0.20 + ambient);

    // Debug views (uniform branch): 3 transmittance, 5 analytic-vs-march diff.
    uint dbg = g_mode.x;
    if (dbg == CLOUD_DBG_TRANSMIT)
    {
        inscatter = T.xxx;
    }
    else if (dbg == CLOUD_DBG_MARCH_DIFF)
    {
        const int STEPS = 256;
        float tEnd = min(t1, bestT > 0.0 ? bestT * 2.0 + 4000.0 : 8000.0);
        float dt = max((tEnd - t0) / STEPS, 1e-3);
        float march = 0.0;
        for (int s = 0; s < STEPS; ++s)
        {
            float t = t0 + (s + 0.5) * dt;
            float3 pos = o + dir * t;
            float dens = 0.0;
            for (uint j = 0; j < macroCount; ++j)
                dens += MacroDensity(g_macros[j], pos);
            march += dens * dt;
        }
        float Tmarch = exp(-max(march, 0.0));
        inscatter = (abs(T - Tmarch) * 32.0).xxx;   // near-black == analytic matches march
    }

    g_scatter[px] = float4(inscatter, T);
    g_cloudDepth[px] = (T > 0.995) ? 0.0 : bestT;
}

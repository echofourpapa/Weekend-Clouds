// DXR 1.1 inline-RayQuery A/B trace (docs/PLAN.md 4.8 / 4.13, step P2.3). Finds
// macros via a hardware BVH over their AABBs instead of the screen-tile list,
// then integrates the same closed-form macro envelope + Gabor detail as the
// tiled path. This is a comparison/validation path; the primary trace is tiled
// compute. Requires RaytracingTier >= 1.1 (guarded on the CPU side).

#include "CloudLighting.hlsli"

RaytracingAccelerationStructure     g_tlas    : register(t15);
StructuredBuffer<CloudKernelPacked> g_kernels : register(t0);
StructuredBuffer<CloudMacro>        g_macros  : register(t1);
Texture2D<float>    g_sceneDepth : register(t6);
RWTexture2D<float4> g_scatter    : register(u7);
RWTexture2D<float>  g_cloudDepth : register(u8);

// Exact sun optical depth from a scatter point: a second inline RayQuery toward
// the sun accumulating macro optical depth (the reference the field cache
// approximates). RT-only; used for lightMode REFERENCE and the diff debug view.
float SunTauReference(float3 ws)
{
    float3 sunDir = normalize(g_sunDirWS.xyz);
    RayDesc r;
    r.Origin = ws; r.Direction = sunDir; r.TMin = 1.0; r.TMax = 1.0e6;
    RayQuery<RAY_FLAG_NONE> sq;
    sq.TraceRayInline(g_tlas, RAY_FLAG_NONE, 0xFF, r);
    float t = 0.0;
    uint g = 0;
    while (sq.Proceed() && g < 2048u)
    {
        g++;
        if (sq.CandidateType() != CANDIDATE_PROCEDURAL_PRIMITIVE) continue;
        CloudMacro m = g_macros[sq.CandidatePrimitiveIndex()];
        CloudKernel k = KernelFromMacro(m);
        KernelRayTerms kt = KernelRaySetup(k, ws, sunDir);
        t += max(TauKernelClamped(k.amplitude, kt, 0.0, 0.0, 1.0e6), 0.0);
    }
    return t;
}

COMPUTE_MAIN
{
    uint2 px = IN.DispatchThreadID.xy;
    if (px.x >= (uint)g_traceSize.x || px.y >= (uint)g_traceSize.y) return;

    float2 uv = (px + 0.5) * g_traceSize.zw;
    float3 o = g_camPosWS.xyz + g_windOffset.xyz;   // macro space (matches AABBs)
    float3 dir = CloudRayDir(float2(px), g_traceSize.zw);
    float tGeom = CloudGeomT(uv, g_sceneDepth[px], dir);
    float tmax = min(tGeom, 1.0e6);

    float tau = 0.0;
    float bestTau = 0.0, bestT = 0.0, bestHeight = 0.5;

    RayDesc ray;
    ray.Origin = o;
    ray.Direction = dir;
    ray.TMin = 0.0;
    ray.TMax = tmax;

    RayQuery<RAY_FLAG_NONE> q;
    q.TraceRayInline(g_tlas, RAY_FLAG_NONE, 0xFF, ray);

    uint guard = 0;
    while (q.Proceed() && guard < 4096u)
    {
        guard++;
        if (q.CandidateType() != CANDIDATE_PROCEDURAL_PRIMITIVE)
            continue;

        CloudMacro m = g_macros[q.CandidatePrimitiveIndex()];

        CloudKernel mk = KernelFromMacro(m);
        KernelRayTerms mt = KernelRaySetup(mk, o, dir);
        float envTau = TauKernelClamped(mk.amplitude, mt, 0.0, 0.0, tmax);
        tau += max(envTau, 0.0);
        if (envTau > bestTau) { bestTau = envTau; bestT = mt.tbar; bestHeight = m.heightFrac01; }

        uint kend = m.detailBegin + m.detailCount;
        for (uint j = m.detailBegin; j < kend; ++j)
        {
            CloudKernel dk = UnpackKernel(g_kernels[j], m);
            KernelRayTerms dt = KernelRaySetup(dk, o, dir);
            float lodExp = LodExponent(dk.freqWS, dt.tbar);
            if (lodExp < -3.912) continue;
            tau += TauKernelClamped(dk.amplitude, dt, lodExp, 0.0, tmax);
        }
        // Never commit: we accumulate all intersected macros (order-independent
        // for transmittance), so traversal continues to the next AABB.
    }

    float tauC = max(tau, 0.0);
    float T = exp(-tauC);

    // Shader Execution Reordering hook (docs/PLAN.md P6.4). The lighting tail
    // below is highly divergent -- the REFERENCE path fires a second RayQuery,
    // the diff path fires two, and empty (T~=1) rays do neither. SER lets the
    // hardware regroup lanes by that outcome before the expensive tail so the
    // secondary traversals stay coherent. MaybeReorderThread is an SM 6.9
    // intrinsic; this block is compiled only when the shader is built as a
    // raygeneration variant under a 6.9-capable Agility runtime (see the
    // --agility-* premake options and D3D12SDK_VERSION_OVERRIDE). It is
    // #ifdef'd out of the shipping SM 6.8 compute build, so it can never break
    // the current pipeline.
#ifdef CLOUD_SER
    // Coherence class: bit0 = ray touched clouds, bit1 = will fire a sun ray.
    uint coh = (T < 0.995 ? 1u : 0u);
    coh |= ((g_mode.x == CLOUD_DBG_LIGHT_DIFF ||
             g_mode.y == CLOUD_LIGHT_REFERENCE) && T < 0.995) ? 2u : 0u;
    MaybeReorderThread(coh, 2u);
#endif

    float cosVS = dot(dir, normalize(g_sunDirWS.xyz));
    float powder = 1.0 - exp(-2.0 * tauC);
    float3 scatterWS = o + dir * bestT;

    // Baked-vs-reference lighting diff (debug 6): cache tau vs exact RayQuery tau.
    if (g_mode.x == CLOUD_DBG_LIGHT_DIFF)
    {
        float tc = SampleSunTau(scatterWS);
        float tr = SunTauReference(scatterWS);
        g_scatter[px] = float4((abs(tc - tr) * 0.5).xxx, T);
        g_cloudDepth[px] = (T > 0.995) ? 0.0 : bestT;
        return;
    }

    float tauSun;
    if (g_mode.y == CLOUD_LIGHT_REFERENCE)   tauSun = SunTauReference(scatterWS);   // exact
    else                                     tauSun = SampleSunTau(scatterWS);
    float3 sunLit = CloudSunScatter(tauSun, cosVS) * powder;
    float3 skyAmb = float3(0.30, 0.45, 0.65) * g_ambientParams.x * (0.4 + 0.6 * saturate(bestHeight));

    g_scatter[px] = float4((1.0 - T) * (sunLit + skyAmb), T);
    g_cloudDepth[px] = (T > 0.995) ? 0.0 : bestT;
}

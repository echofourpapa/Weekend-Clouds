// TEMPORARY compile probe for CloudCommon/CloudKernels (docs/PLAN.md P1.1).
// Exercises the header functions so the offline build type-checks them.
// Deleted in P1.3 when real cloud shaders include the headers.

#include "CloudKernels.hlsli"

RWTexture2D<float4> g_probeOut : register(u0);

COMPUTE_MAIN
{
    uint2 px = IN.DispatchThreadID.xy;

    float3 dir = CloudRayDir(float2(px), g_traceSize.zw);

    CloudMacro m = (CloudMacro)0;
    m.position = float3(0, 1500, 0);
    m.sigma = float3(800, 400, 800);
    m.amplitude = 0.05;
    m.quatZW = 0x7FFF0000 | 0;   // identity-ish quat (w = 1 in snorm16 high half)

    CloudKernelPacked pk;
    pk.a = uint4(0, 0, 0x0000007F, 0x38003800);  // origin, identity quat (w=127), sigma 0.5
    pk.b = uint4(0x38000000, 0, 0, 0);           // sigmaZ 0.5, amp 0, freq 0, phase 0

    CloudKernel k = UnpackKernel(pk, m);
    KernelRayTerms t = KernelRaySetup(k, g_camPosWS.xyz, dir);

    float tGeom = CloudGeomT(float2(px) * g_traceSize.zw, 0.0, dir);
    float lodExp = LodExponent(k.freqWS, t.tbar);
    float tau = TauKernelClamped(k.amplitude, t, lodExp, 0.0, tGeom);
    float tf = FreeflightTruncated(t, 0.0, tGeom, Hash01(uint3(px, g_mode.w)));
    bool fast = FullDomainEligible(tGeom, t);

    float T = exp(-max(tau, 0.0));
    float ph = PhaseDualHG(dot(dir, g_sunDirWS.xyz));

    g_probeOut[px] = float4(T, tf * 1e-6, ph, fast ? 1.0 : 0.0);
}

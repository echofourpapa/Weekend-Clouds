// Temporal reprojection (docs/PLAN.md 4.11, step P5.2b). Clouds carry their own
// half-res history (geometry motion vectors are useless for sky). Reproject the
// previous frame via the camera's prev view-proj, neighbourhood-clamp, detect
// disocclusion from the transmittance delta, and blend. Per-frame wind drift is
// sub-pixel at cloud distance, so camera-only reprojection is sufficient.

#include "CloudCommon.hlsli"

Texture2D<float4>   g_scatter   : register(t10);   // denoised current (rgb inscatter, a T)
Texture2D<float>    g_cloudDepth : register(t12);   // current reservoir-winner distance
Texture2D<float4>   g_histPrev  : register(t9);    // previous accumulated result
RWTexture2D<float4> g_histCur   : register(u10);   // new accumulated result

COMPUTE_MAIN
{
    uint2 px = IN.DispatchThreadID.xy;
    if (px.x >= (uint)g_traceSize.x || px.y >= (uint)g_traceSize.y) return;

    float4 cur = g_scatter[px];
    float4 rawScatter = cur;      // pre-blend (for debug passthrough)
    float curDepth = g_cloudDepth[px];

    // No cloud here: pass the current value straight through (no history smear).
    if (curDepth <= 0.0)
    {
        g_histCur[px] = cur;
        return;
    }

    float3 dir = CloudRayDir(float2(px), g_traceSize.zw);
    float3 worldCur = g_camPosWS.xyz + dir * curDepth;   // visual (advected) position
    float4 prevClip = mul(float4(worldCur, 1.0), g_prevViewProj);
    float2 prevUV = prevClip.xy / prevClip.w * float2(0.5, -0.5) + 0.5;

    // 3x3 neighbourhood clamp window of the current signal.
    float4 nmin = cur, nmax = cur;
    [unroll] for (int dy = -1; dy <= 1; ++dy)
    [unroll] for (int dx = -1; dx <= 1; ++dx)
    {
        int2 q = int2(px) + int2(dx, dy);
        q = clamp(q, int2(0, 0), int2((int)g_traceSize.x - 1, (int)g_traceSize.y - 1));
        float4 s = g_scatter[q];
        nmin = min(nmin, s); nmax = max(nmax, s);
    }

    float alpha = g_temporal.x;   // alphaBase
    bool disocc = false;
    bool accumulate = g_temporal.w > 0.5;   // static-camera progressive accumulation (P4.1)
    if (accumulate)
    {
        // Unbiased running average with no clamp; the CPU resets the count on any
        // camera/sun/param change, so this converges the lighting for validation.
        float nprev = g_temporal.z;
        alpha = (nprev < 0.5) ? 1.0 : 1.0 / (nprev + 1.0);
        if (any(prevUV < 0.0) || any(prevUV > 1.0))
            alpha = 1.0;
        else
        {
            float4 hist = g_histPrev.SampleLevel(linearClampSampler, prevUV, 0);
            cur = lerp(hist, cur, alpha);
        }
    }
    else if (any(prevUV < 0.0) || any(prevUV > 1.0))
    {
        alpha = 1.0;              // disoccluded (off-screen last frame)
        disocc = true;
    }
    else
    {
        float4 hist = g_histPrev.SampleLevel(linearClampSampler, prevUV, 0);
        if (abs(hist.a - cur.a) > g_temporal.y) { alpha = 1.0; disocc = true; }
        hist = clamp(hist, nmin, nmax);
        cur = lerp(hist, cur, alpha);
    }

    // Debug views bypass temporal blending so the visualization isn't smeared.
    if (g_mode.x == CLOUD_DBG_HISTORY_REJ)
        cur = float4(disocc ? 1.0 : 0.0, disocc ? 0.0 : 1.0, 0.0, cur.a);   // red=rejected, green=kept
    else if (g_mode.x != 0)
        cur = rawScatter;                                                    // show the trace's raw debug output

    g_histCur[px] = cur;
}

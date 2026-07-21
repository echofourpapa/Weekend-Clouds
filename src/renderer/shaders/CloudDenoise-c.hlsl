// Spatial denoise (docs/PLAN.md 4.10, step P5.2c). A 5x5 bilateral filter on the
// half-res scatter, edge-stopped by cloud depth and transmittance, run before
// temporal reprojection to clean up stochastic-masking noise. It is a no-op
// (passthrough) when masking is off or a debug view is active, so it costs
// almost nothing in the common case.

#include "CloudCommon.hlsli"

Texture2D<float4>   g_scatter    : register(t11);
Texture2D<float>    g_cloudDepth : register(t12);
RWTexture2D<float4> g_denoiseOut : register(u9);

COMPUTE_MAIN
{
    uint2 px = IN.DispatchThreadID.xy;
    if (px.x >= (uint)g_traceSize.x || px.y >= (uint)g_traceSize.y) return;

    float4 center = g_scatter[px];

    // Only filter when stochastic masking is active (otherwise no noise to clean).
    if (g_mode.x != 0 || g_lodParams.z <= 1.0)
    {
        g_denoiseOut[px] = center;
        return;
    }

    float cd = g_cloudDepth[px];
    int2 dim = int2((int)g_traceSize.x - 1, (int)g_traceSize.y - 1);
    float4 sum = 0.0;
    float wsum = 0.0;
    const float gk[3] = { 1.0, 0.66, 0.24 };   // 1D gaussian-ish weights by |offset|
    [unroll] for (int dy = -2; dy <= 2; ++dy)
    [unroll] for (int dx = -2; dx <= 2; ++dx)
    {
        int2 q = clamp(int2(px) + int2(dx, dy), int2(0, 0), dim);
        float4 s = g_scatter[q];
        float sd = g_cloudDepth[q];
        float wg = gk[abs(dx)] * gk[abs(dy)];
        float wd = exp(-abs(sd - cd) / (0.1 * cd + 1.0));      // depth edge-stop
        float wt = exp(-abs(s.a - center.a) / 0.1);            // transmittance edge-stop
        float w = wg * wd * wt;
        sum += s * w; wsum += w;
    }
    g_denoiseOut[px] = wsum > 0.0 ? sum / wsum : center;
}

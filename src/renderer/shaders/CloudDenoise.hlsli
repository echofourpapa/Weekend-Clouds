#if !defined( CLOUD_DENOISE_H )
#define CLOUD_DENOISE_H

// Shared à-trous bilateral tap (docs/PLAN.md 4.10, step P5.2c + P6.5 second pass).
// The edge-aware 5x5 kernel is run twice with growing hole spacing (stride 1 then
// stride 2) to reach a ~9x9 support at 5x5 cost, cleaning stochastic-masking noise
// before temporal reprojection. Both passes share this body; the entry files
// (CloudDenoise-c / CloudDenoise2-c) only differ in their source, destination and
// stride, so the filter math stays in one place. Passthrough (no work) when
// masking is off or a debug view is active, so the common case is almost free.

#include "CloudCommon.hlsli"

float4 AtrousDenoise(Texture2D<float4> src, Texture2D<float> depthTex, uint2 px, int stride)
{
    float4 center = src[px];

    // Only filter when stochastic masking is active (otherwise no noise to clean).
    if (g_mode.x != 0 || g_lodParams.z <= 1.0)
        return center;

    float cd = depthTex[px];
    int2 dim = int2((int)g_traceSize.x - 1, (int)g_traceSize.y - 1);
    float4 sum = 0.0;
    float wsum = 0.0;
    const float gk[3] = { 1.0, 0.66, 0.24 };   // 1D gaussian-ish weights by |offset|
    [unroll] for (int dy = -2; dy <= 2; ++dy)
    [unroll] for (int dx = -2; dx <= 2; ++dx)
    {
        int2 q = clamp(int2(px) + int2(dx, dy) * stride, int2(0, 0), dim);
        float4 s = src[q];
        float sd = depthTex[q];
        float wg = gk[abs(dx)] * gk[abs(dy)];
        float wd = exp(-abs(sd - cd) / (0.1 * cd + 1.0));      // depth edge-stop
        float wt = exp(-abs(s.a - center.a) / 0.1);            // transmittance edge-stop
        float w = wg * wd * wt;
        sum += s * w; wsum += w;
    }
    return wsum > 0.0 ? sum / wsum : center;
}

#endif

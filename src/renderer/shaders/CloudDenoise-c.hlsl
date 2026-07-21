// À-trous denoise pass 1 (docs/PLAN.md 4.10). Stride-1 5x5 bilateral over the
// half-res scatter, edge-stopped by cloud depth and transmittance. Output feeds
// the stride-2 pass (CloudDenoise2-c) via the à-trous scratch texture, so the
// final result lands in the denoise slot the reproject reads. Passthrough when
// masking is off or a debug view is active (see CloudDenoise.hlsli).

#include "CloudDenoise.hlsli"

Texture2D<float4>   g_scatter    : register(t11);
Texture2D<float>    g_cloudDepth : register(t12);
RWTexture2D<float4> g_denoiseOut : register(u14);   // à-trous pass-1 scratch

COMPUTE_MAIN
{
    uint2 px = IN.DispatchThreadID.xy;
    if (px.x >= (uint)g_traceSize.x || px.y >= (uint)g_traceSize.y) return;
    g_denoiseOut[px] = AtrousDenoise(g_scatter, g_cloudDepth, px, 1);
}

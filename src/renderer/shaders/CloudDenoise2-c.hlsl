// À-trous denoise pass 2 (docs/PLAN.md 4.10, P6.5). Stride-2 5x5 bilateral over
// the pass-1 result, widening the effective support to ~9x9 at 5x5 cost. Writes
// the final denoised scatter into the slot the temporal reproject reads (u9 ->
// t10). Passthrough when masking is off or a debug view is active.

#include "CloudDenoise.hlsli"

Texture2D<float4>   g_atrous     : register(t14);   // pass-1 result (à-trous scratch)
Texture2D<float>    g_cloudDepth : register(t12);
RWTexture2D<float4> g_denoiseOut : register(u9);    // final denoised scatter

COMPUTE_MAIN
{
    uint2 px = IN.DispatchThreadID.xy;
    if (px.x >= (uint)g_traceSize.x || px.y >= (uint)g_traceSize.y) return;
    g_denoiseOut[px] = AtrousDenoise(g_atrous, g_cloudDepth, px, 2);
}

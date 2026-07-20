// Composite: paint sky where the scene depth is the far plane (reverse-Z => 0),
// then blend clouds (premultiplied) over the deferred HDR output in place.
// docs/PLAN.md 4.12. The cloud blend is gated by g_skyParams.w (cloudsActive),
// so this one shader serves P1.3 (sky only) and P1.4+ (sky + clouds) without a
// compile-time permutation (the offline build produces one .cso per file).

#include "CloudCommon.hlsli"

Texture2D<float4>   g_skyViewLUT   : register(t5);
Texture2D<float>    g_sceneDepth   : register(t6);
Texture2D<float4>   g_cloudScatter : register(t11);   // rgb inscatter, a transmittance
RWTexture2D<float4> g_hdrOutput    : register(u12);    // deferred HDR, in-place

COMPUTE_MAIN
{
    uint2 px = IN.DispatchThreadID.xy;
    if (px.x >= (uint)g_outputSize.x || px.y >= (uint)g_outputSize.y) return;

    // Debug views replace the frame with the trace's visualization (full-res).
    if (g_mode.x != 0 && g_skyParams.w > 0.5)
    {
        g_hdrOutput[px] = float4(g_cloudScatter[px].rgb, 1.0);
        return;
    }

    float depth = g_sceneDepth[px];
    float3 dir = CloudRayDir(float2(px), g_outputSize.zw);

    float3 hdr = g_hdrOutput[px].rgb;

    // Sky background behind everything (reverse-Z: depth 0 == far plane == sky)
    if (depth <= 0.0)
        hdr = SampleSkyView(g_skyViewLUT, dir) + SunDisc(dir);

    if (g_skyParams.w > 0.5)
    {
        float4 cloud = g_cloudScatter[px];   // (inscatter.rgb, transmittance)
        hdr = hdr * cloud.a + cloud.rgb;
    }

    g_hdrOutput[px] = float4(hdr, 1.0);
}

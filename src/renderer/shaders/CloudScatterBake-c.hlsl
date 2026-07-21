// Canonical scattering transfer table (docs/PLAN.md P6.2). Bakes the outgoing
// scattered radiance of a canonical cloud medium as a function of optical depth
// toward the sun and view-sun angle - a 2D LUT that captures many scattering
// orders (a richer, baked superset of the inline 3-tap Wrenninge model). The
// full 6D spatial+directional table is documented future work; this 2D slice is
// the dominant variation and is what the trace fetches per pixel.
//
//   x-axis: optical depth tauSun in [0, 8]
//   y-axis: cos(view, sun) in [-1, 1]
//   value : RGB scattered-radiance factor (x sunRadiance at runtime)

#include "CloudCommon.hlsli"

RWTexture2D<float4> g_scatterLUT : register(u0);

#define LUT_W 64
#define LUT_H 64
#define ORDERS 16

COMPUTE_MAIN
{
    uint2 px = IN.DispatchThreadID.xy;
    if (px.x >= LUT_W || px.y >= LUT_H) return;

    float tau = (px.x + 0.5) / LUT_W * 8.0;
    float c = (px.y + 0.5) / LUT_H * 2.0 - 1.0;

    // Multi-order scattering: order n attenuates by exp(-tau*a^n), scatters with
    // a progressively isotropic phase and diminishing contribution (Wrenninge,
    // extended to ORDERS terms so the response is smooth and higher-order MS is
    // captured - too expensive inline, cheap here since it's a one-time bake).
    float3 sum = 0.0;
    float a = 1.0, b = 1.0, w = 1.0;
    float g0 = g_scatterParams.x, g1 = g_scatterParams.y, mix = g_scatterParams.z;
    for (int n = 0; n < ORDERS; ++n)
    {
        float phase = lerp(PhaseHG(c, g1 * b), PhaseHG(c, g0 * b), mix);
        sum += w * exp(-tau * a) * phase;
        a *= 0.5; b *= 0.6; w *= 0.5;
    }
    g_scatterLUT[px] = float4(sum, 1.0);
}

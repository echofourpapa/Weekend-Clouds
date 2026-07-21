// Sky -> equirectangular HDR (docs/PLAN.md P4.4). Renders the analytic
// atmosphere into a lat-long texture in the same format the engine IBL loader
// consumes, so the scene's image-based lighting can be rebuilt from the current
// sky (IBLProcessor::AddIBLFromEquirect). Reuses the Sky.hlsli scattering model.

#include "Sky.hlsli"

RWTexture2D<float4> g_equirect : register(u1);   // lat-long sky, RGBA16F (unused gen slot)

// Full single-scattering sky along an arbitrary view direction (the sky-view LUT
// is sun-relative and horizon-biased; for a cubemap source we integrate directly).
float3 SkyRadiance(float3 viewDir)
{
    float turbidity = max(g_skyParams.x, 1.0);
    float3 sunDir = normalize(g_sunDirWS.xyz);
    float r0 = R_GROUND + max(g_camPosWS.y + g_skyParams.y * 1000.0, 1.0);
    float muV = viewDir.y;
    float cosVS = dot(viewDir, sunDir);

    float tMax = RayAtmosphereTop(r0, muV);
    float tG = RayGround(r0, muV);
    if (tG > 0.0) tMax = min(tMax, tG);

    const int STEPS = 24;
    float dt = tMax / STEPS;
    float3 lum = 0.0, tr = 1.0;
    float pR = PhaseRayleigh(cosVS);
    float pM = PhaseMie(cosVS);
    for (int i = 0; i < STEPS; ++i)
    {
        float t = (i + 0.5) * dt;
        float rr = sqrt(r0 * r0 + t * t + 2.0 * r0 * muV * t);
        float h = rr - R_GROUND;
        float muS = (r0 * sunDir.y + t * cosVS) / rr;
        float dR = exp(-h / H_R), dM = exp(-h / H_M);
        float3 sT = SampleTransLUT(rr, muS);
        float3 inscat = (BETA_R * dR * pR + BETA_M * turbidity * dM * pM) * sT;
        lum += tr * inscat * dt;
        tr *= exp(-(BETA_R * dR + (BETA_M * turbidity + BETA_MA) * dM) * dt);
    }
    return lum * g_sunRadiance.rgb;
}

COMPUTE_MAIN
{
    uint2 px = IN.DispatchThreadID.xy;
    uint w, h; g_equirect.GetDimensions(w, h);
    if (px.x >= w || px.y >= h) return;

    float2 uv = (px + 0.5) / float2(w, h);
    float az = (uv.x * 2.0 - 1.0) * 3.14159265;      // -pi..pi
    float el = (0.5 - uv.y) * 3.14159265;            // +pi/2 (up) .. -pi/2 (down)
    float3 dir = float3(cos(el) * sin(az), sin(el), -cos(el) * cos(az));

    g_equirect[px] = float4(SkyRadiance(dir) + SunDisc(dir), 1.0);
}

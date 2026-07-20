#if !defined( SKY_IMPL_H )
#define SKY_IMPL_H

// Minimal single-scattering sky (docs/PLAN.md 4.1). Shared by the two entry
// shaders SkyTransLUT-c.hlsl and SkyViewLUT-c.hlsl. The offline build produces
// one .cso per entry file, so variants are separate files (engine idiom), not
// -D permutations.

#include "CloudCommon.hlsli"

RWTexture2D<float4> g_skyTransLUT : register(u4);
RWTexture2D<float4> g_skyViewLUT  : register(u5);
Texture2D<float4>   g_transLUTsrv : register(t4);

static const float  R_GROUND = 6360000.0;
static const float  R_TOP    = 6420000.0;
static const float3 BETA_R   = float3(5.802e-6, 13.558e-6, 33.1e-6);
static const float  BETA_M   = 3.996e-6;
static const float3 BETA_MA  = float3(4.4e-6, 4.4e-6, 4.4e-6);
static const float  H_R      = 8000.0;
static const float  H_M      = 1200.0;
static const float  MIE_G    = 0.8;

float RayAtmosphereTop(float r, float mu)
{
    float disc = r * r * (mu * mu - 1.0) + R_TOP * R_TOP;
    return max(0.0, -r * mu + sqrt(max(disc, 0.0)));
}

float RayGround(float r, float mu)
{
    float disc = r * r * (mu * mu - 1.0) + R_GROUND * R_GROUND;
    if (disc < 0.0) return -1.0;
    return -r * mu - sqrt(disc);
}

float2 TransUV(float r, float mu)
{
    float alt = saturate((r - R_GROUND) / (R_TOP - R_GROUND));
    float x = (mu + 0.15) / 1.15;
    return float2(saturate(x), alt);
}

float3 TransmittanceToTop(float r, float mu, float turbidity)
{
    if (RayGround(r, mu) > 0.0) return float3(0, 0, 0);
    const int STEPS = 40;
    float tMax = RayAtmosphereTop(r, mu);
    float dt = tMax / STEPS;
    float3 opt = 0;
    for (int i = 0; i < STEPS; ++i)
    {
        float t = (i + 0.5) * dt;
        float h = sqrt(r * r + t * t + 2.0 * r * mu * t) - R_GROUND;
        float dR = exp(-h / H_R);
        float dM = exp(-h / H_M);
        opt += (BETA_R * dR + (BETA_M * turbidity + BETA_MA) * dM) * dt;
    }
    return exp(-opt);
}

float3 SampleTransLUT(float r, float mu)
{
    return g_transLUTsrv.SampleLevel(linearClampSampler, TransUV(r, mu), 0).rgb;
}

float PhaseRayleigh(float c) { return 3.0 / (16.0 * 3.14159265) * (1.0 + c * c); }
float PhaseMie(float c)
{
    float g = MIE_G;
    float n = (1.0 - g * g) * (1.0 + c * c);
    float d = (2.0 + g * g) * pow(abs(1.0 + g * g - 2.0 * g * c), 1.5);
    return 3.0 / (8.0 * 3.14159265) * n / max(d, 1e-4);
}

void WriteTransLUT(uint2 px)
{
    uint w, h; g_skyTransLUT.GetDimensions(w, h);
    if (px.x >= w || px.y >= h) return;
    float turbidity = max(g_skyParams.x, 1.0);
    float2 uv = (px + 0.5) / float2(w, h);
    float mu = uv.x * 1.15 - 0.15;
    float r = lerp(R_GROUND, R_TOP, uv.y);
    g_skyTransLUT[px] = float4(TransmittanceToTop(r, mu, turbidity), 1.0);
}

void WriteSkyViewLUT(uint2 px)
{
    uint w, h; g_skyViewLUT.GetDimensions(w, h);
    if (px.x >= w || px.y >= h) return;
    float turbidity = max(g_skyParams.x, 1.0);
    float2 uv = (px + 0.5) / float2(w, h);
    float azimuth = (uv.x * 2.0 - 1.0) * 3.14159265;
    float ev = uv.y * 2.0 - 1.0;
    float elevation = sign(ev) * ev * ev * (3.14159265 * 0.5);

    float3 sunDir = normalize(g_sunDirWS.xyz);
    float3 up = float3(0, 1, 0);
    float3 sunAz = normalize(sunDir - up * dot(sunDir, up));
    if (length(sunAz) < 1e-3) sunAz = float3(1, 0, 0);
    float3 sunRight = normalize(cross(up, sunAz));
    float cosEl = cos(elevation), sinEl = sin(elevation);
    float3 viewDir = normalize(sunAz * (cosEl * cos(azimuth)) + sunRight * (cosEl * sin(azimuth)) + up * sinEl);

    float r0 = R_GROUND + max(g_camPosWS.y + g_skyParams.y * 1000.0, 1.0);
    float muV = viewDir.y;
    float muS = sunDir.y;
    float cosVS = dot(viewDir, sunDir);

    float tMax = RayAtmosphereTop(r0, muV);
    float tG = RayGround(r0, muV);
    if (tG > 0.0) tMax = min(tMax, tG);

    const int STEPS = 32;
    float dt = tMax / STEPS;
    float3 lum = 0;
    float3 tr = 1.0;
    float pR = PhaseRayleigh(cosVS);
    float pM = PhaseMie(cosVS);
    for (int i = 0; i < STEPS; ++i)
    {
        float t = (i + 0.5) * dt;
        float rr = sqrt(r0 * r0 + t * t + 2.0 * r0 * muV * t);
        float h = rr - R_GROUND;
        float muSlocal = (r0 * muS + t * cosVS) / rr;
        float dR = exp(-h / H_R);
        float dM = exp(-h / H_M);
        float3 sTrans = SampleTransLUT(rr, muSlocal);
        float3 inscat = (BETA_R * dR * pR + BETA_M * turbidity * dM * pM) * sTrans;
        lum += tr * inscat * dt;
        tr *= exp(-(BETA_R * dR + (BETA_M * turbidity + BETA_MA) * dM) * dt);
    }
    lum *= g_sunRadiance.rgb;
    g_skyViewLUT[px] = float4(lum, 1.0);
}

#endif

#if !defined( PBR_H )
#define PBR_H

#include "color_util.hlsli"

#define V_PI      3.14159265359
#define V_PI_2    6.28318530718
#define V_PI_HALF 1.57079632679
#define V_INV_PI  0.31830988618

// Approximation of the GGX Directional Albedo (Energy) for Direct Lighting
// Based on curve fits to the GGX integral
float GGX_Energy_Approximation(float roughness, float NoV)
{
    // A cheap robust fit for GGX Energy E(NoV, Roughness)
    // Returns the fraction of light that is reflected (Single Scattering)
    float r = roughness;
    float curve = 1.0 - (0.04 + 0.96 * pow(max(1.0 - r, 0.0), 5.0)); 
    
    // Lerp based on view angle to handle Fresnel edge behavior
    // (Simulates the behavior of the BRDF LUT's Red channel)
    return lerp(curve, 1.0, pow(max(1.0 - NoV, 0.0), 5.0) * r);
}

// -----------------------------------------------------------------------------
// Fdez-Agüera's Analytic Energy Approximations
// Reference: "Multiple-Scattering Microfacet BSDFs with the Smith Model" (JCGT 2019)
// -----------------------------------------------------------------------------

// Accurate approximation of the GGX directional albedo (Energy)
// Replaces: GGX_Energy_Approximation
float FdezAguera_Energy(float roughness, float NoV)
{
    // Coefficients for the analytic fit
    float4 c0 = float4(-1.0, -0.0275, -0.572, 0.022);
    float4 c1 = float4(1.0, 0.0425, 1.04, -0.04);

    float4 r = roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * NoV)) * r.x + r.y;
    
    // Returns (Scale, Bias) tuple similar to the Split-Sum LUT
    float2 energyLUT = float2(-1.04, 1.04) * a004 + r.zw;
    
    // E(NoV) = Scale + Bias (Assuming F0=1 for pure energy tracking)
    return saturate(energyLUT.x + energyLUT.y);
}

// Approximate Average Energy (E_avg) over the hemisphere
float FdezAguera_Eavg(float roughness)
{
    // Polynomial fit for the average energy of the GGX lobe
    float r = roughness;
    float r2 = r * r;
    // Fit derived from integrating the directional albedo
    return 1.0 - 0.1308 * r - 0.364 * r2; 
}
float3x3 generateTBN(float3 normal)
{
    float3 bitangent = float3(0.0, 1.0, 0.0);

    float NdotUp = dot(normal, float3(0.0, 1.0, 0.0));
    float epsilon = 0.0000001;
    if (1.0 - abs(NdotUp) <= epsilon) {
        // Sampling +Y or -Y, so we need a more robust bitangent.
        if (NdotUp > 0.0)
            bitangent = float3(0.0, 0.0, 1.0);
        else
            bitangent = float3(0.0, 0.0, -1.0);
    }

    float3 tangent = normalize(cross(bitangent, normal));
    bitangent = cross(normal, tangent);

    return transpose(float3x3(tangent, bitangent, normal));
}

// Calculate normalized sampling direction vector based on current fragment coordinates.
// This is essentially "inverse-sampling": we reconstruct what the sampling vector would be if we wanted it to "hit"
// this particular fragment in a cubemap.
float3 getSamplingVector(uint3 ThreadID, float2 TexelSize)
{
    float2 uv = TexelSize * (ThreadID.xy + 0.5f);
    uv.y = 1 - uv.y;
    uv = uv * 2 - 1;
    // Select vector based on cubemap face index.
    float3 ret;
    switch (ThreadID.z)
    {
    case 0: ret = float3(1.0, uv.y, -uv.x); break;
    case 1: ret = float3(-1.0, uv.y, uv.x); break;
    case 2: ret = float3(uv.x, 1.0, -uv.y); break;
    case 3: ret = float3(uv.x, -1.0, uv.y); break;
    case 4: ret = float3(uv.x, uv.y, 1.0); break;
    case 5: ret = float3(-uv.x, uv.y, -1.0); break;
    }
    return normalize(ret);
}


struct ImportanceSample
{
    float3 dir;
    float cosT;
    float sinT;
    float phi;
    float pdf;
};

float GTR2(float NdotH, float a)
{
    float a2 = max(a * a, 1e-5);
    float t = 1.0f + (a2 - 1.0f) * NdotH * NdotH;
    return a2 / (V_PI * t * t);
}

float D_GGX(float NoH, float roughness)
{
    float a = NoH * roughness;
    float k = roughness / (1.0f - NoH * NoH + a * a);
    return k * k * V_INV_PI;
}

// https://google.github.io/filament/Filament.md.html#materialsystem/clothmodel
float D_Charlie(float roughness, float NoH) 
{
    // Estevez and Kulla 2017, "Production Friendly Microfacet Sheen BRDF"
    float invAlpha  = 1.0 / roughness;
    float cos2h = NoH * NoH;
    float sin2h = max(1.0 - cos2h, 0.0078125); // 2^(-14/2), so sin2h^2 > 0 in fp16
    return (2.0 + invAlpha) * pow(sin2h, invAlpha * 0.5) / (2.0 * V_PI);
}

// V_Neubelt: A cheap visibility term often used with Charlie
float V_Neubelt(float NoV, float NoL) {
    // Optimized Ashikhmin form
    return 1.0 / (4.0 * (NoL + NoV - NoL * NoV));
}

float3 ImportanceSampleUniform(float2 Xi)
{
    float phi = 2.0 * V_PI * Xi.x;
    float cosT = 1.0 - Xi.y; // Uniform distribution in cosTheta
    float sinT = sqrt(1.0 - cosT * cosT);

    float3 H;
    H.x = sinT * cos(phi);
    H.y = sinT * sin(phi);
    H.z = cosT;
    return H;
}

float3 importanceSampleGGX(float2 Xi, float a2)
{
    float phi = V_PI_2 * Xi.x;
    float cosT = sqrt((1.0 - Xi.y) / (1.0 + (a2 - 1.0) * Xi.y));
    float sinT = sqrt(1.0 - cosT * cosT);

    float3 H;
    H.x = sinT * cos(phi);
    H.y = sinT * sin(phi);
    H.z = cosT;
    
    return H;
}

float3 importanceSampleGGXIBL(float2 Xi, float a, float3 N)
{
    float a2 = a * a;
    float3 H = importanceSampleGGX(Xi, a2);

    float3 up = abs(N.z) < 0.999 ? float3(0, 0, 1) : float3(1, 0, 0);
    float3 tangent = normalize(cross(up, N));
    float3 bitangent = cross(N, tangent);
    
    H = H.x * tangent + H.y * bitangent + H.z * N;
    
    return H;
}

float2 hammersley(uint i, float numSamples) {
    uint bits = i;
    bits = (bits << 16) | (bits >> 16);
    bits = ((bits & 0x55555555) << 1) | ((bits & 0xAAAAAAAA) >> 1);
    bits = ((bits & 0x33333333) << 2) | ((bits & 0xCCCCCCCC) >> 2);
    bits = ((bits & 0x0F0F0F0F) << 4) | ((bits & 0xF0F0F0F0) >> 4);
    bits = ((bits & 0x00FF00FF) << 8) | ((bits & 0xFF00FF00) >> 8);
    return float2(i / numSamples, bits / exp2(32));
}

float smithG_GGX(float NdotV, float alphaG)
{
    float a = alphaG * alphaG;
    float b = NdotV * NdotV;
    return 1 / (NdotV + sqrt(a + b - a * b));
}

float V_SmithGGXCorrelated(float NoV, float NoL, float a)
{
    //float a2 = a * a;
    //float b2V = NoV * NoV;
    //float b2L = NoL * NoL;
    float GGXV = smithG_GGX(NoV, a);
    float GGXL = smithG_GGX(NoL, a);
    //float GGXV = (NoV + sqrt(a2 + b2V - a2 * b2V));
    //float GGXL = (NoL + sqrt(a2 + b2L - a2 * b2L));
    return 0.5 / (GGXV + GGXL);
}

float V_SmithGGXCorrelated2(float NoV, float NoL, float roughness)
{
    float a2 = pow(roughness, 4.0);
    float GGXV = NoL * sqrt(NoV * NoV * (1.0 - a2) + a2);
    float GGXL = NoV * sqrt(NoL * NoL * (1.0 - a2) + a2);
    return 0.5 / (GGXV + GGXL);
}

float GDFG(float NoV, float NoL, float a)
{
    float a2 = a * a;
    float GGXL = NoV * sqrt((-NoL * a2 + NoL) * NoL + a2);
    float GGXV = NoL * sqrt((-NoV * a2 + NoV) * NoV + a2);
    return (2 * NoL) / (GGXV + GGXL);
}

float3 DFG(float NoV, float roughness, uint sampleCount)
{
    float a = roughness * roughness;
    float3 N = float3(0, 0, 1);
    float3 V;
    V.x = sqrt(1.0f - NoV * NoV);
    V.y = 0.0f;
    V.z = NoV;
    float3 r = 0.0f;
    for (uint i = 0; i < sampleCount; i++) {
        float2 Xi = hammersley(i, sampleCount);
        float a2 = max(a * a, 1e-5);
        float3 H = importanceSampleGGX(Xi, a2);
        float3 L = 2.0f * dot(V, H) * H - V;

        float NoL = saturate(L.z);
        float NoH = saturate(H.z);
        float VoH = saturate(dot(V, H));

        if (NoL > 0.0f)
        {
            float G = GDFG(NoV, NoL, a);
            // float Gv = (G * VoH) / (NoH * NoV);
            float Gv = G * VoH / NoH;
            float Fc = pow(1.0f - VoH, 5.0f);
            r.x += Gv * (1 - Fc);
            r.y += Gv * Fc;
        }
        
        // Sheen
        {
            float3 Hs = ImportanceSampleUniform(Xi);
            float3 Ls = 2.0f * dot(V, Hs) * Hs - V;
            
            float NoLs = saturate(Ls.z);
            float NoHs = saturate(Hs.z);
            float VoHs = saturate(dot(V, Hs));
            
            if (NoLs > 0.0f)
            {
                float Vis = V_Neubelt(NoV, NoLs);
                float D = D_Charlie(roughness, NoHs);
                
                float pdf = 1.f / V_PI_2;
                r.z += (Vis * D * NoLs) / pdf;
            }
        }
    }
    return r / sampleCount;
}

float computeLod(float pdf, float2 resolution, uint samples)
{
    float os = 1.0 / (float(samples) * pdf);
    float op = 4.0 * V_PI / (6.0 * resolution.x * resolution.y);
    float lod = 0.5 * log2(os/op); //6.0 * resolution.x * resolution.y / (float(samples) * pdf));
    return lod;
}
struct BDRFInput
{
    float3 N;
    float3 V;
    float3 L;
    float3 albedo;
    float metallic;
    float roughness;
    float specular;
    float subsurface;
    float sheen;
    float sheenTint;
    float sheenRoughness;
};

struct BRDFOutput
{
    float3 f0;
    float3 f90;
    float3 Fd;
    float3 Fs;
    float3 SheenColor;
    float NoL;
    float NoV;
};

float3 F_Schlick(float u, float3 f0) {
    float f = pow(1.0f - u, 5.0f);
    return f + f0 * (1.0f - f);
}

float3 F_Schlick(float u, float3 f0, float3 f90) {
    return f0 + (f90 - f0) * pow(1.0f - u, 5.0f);
}

float3 Fd_Burley(float NoV, float NoL, float3 f0, float3 f90) {
    float3 lightScatter = F_Schlick(NoL, f0, f90);
    float3 viewScatter = F_Schlick(NoV, f0, f90);
    return lightScatter * viewScatter;
}

BRDFOutput BRDF(in BDRFInput input)
{
    float3 L = input.L;
    float3 N = input.N;
    float3 V = input.V;
    float3 H = normalize(L + V);
    float NoV = abs(dot(N, V)); // avoid NAN
    float NoL = saturate(dot(N, L)); // avoid NAN
    float NoH = saturate(dot(N, H));
    float LoH = saturate(dot(L, H));

    BRDFOutput output;
    
    float lum = GetLuminanceRec2020(input.albedo);
    float3 tint = lum > 0.0f ? (input.albedo / lum) : 1.f;
    float3 f0 = lerp(0.08f * input.specular, input.albedo, input.metallic);
    float3 f90 = saturate(dot(f0, 50.0f * 0.33f));
    
    // Multi-scatering
    float E_V = FdezAguera_Energy(input.roughness, NoV);
    float E_L = FdezAguera_Energy(input.roughness, NoL);
    float E_avg = FdezAguera_Eavg(input.roughness);
    float3 F_avg = f0 + (f90 - f0) / 21.0;
    float3 Ems = (F_avg * E_avg) / (1.0 - F_avg * (1.0 - E_avg));
    float3 Fms = Ems * (1.0 - E_V) * (1.0 - E_L) / (V_PI * (1.0 - E_avg));
    
    // specular BRDF
    {
        float3 Fs = F_Schlick(LoH, f0, f90);
        float Vis = V_SmithGGXCorrelated(NoV, NoL, input.roughness);
        float Ds = GTR2(NoH, input.roughness);
        float3 Fr = Fs * Vis * Ds;
        
        output.Fs = Fr + Fms;
    }
    
    output.f0 = f0;
    output.f90 = f90;
    
    // Sheen
    float3 Csheen = lerp(1.f, tint, input.sheenTint);
    float sheenD = D_Charlie(input.sheenRoughness, NoH);
    float sheenV = V_Neubelt(NoV, NoL);
    float3 Fsheen = sheenD * sheenV * input.sheen * Csheen;
    
    output.SheenColor = Csheen;
    // diffuse BRDF
    {
        // Based on Hanrahan-Krueger brdf approximation of isotropic bssrdf
        // 1.25 scale is used to (roughly) preserve albedo
        // Fss90 used to "flatten" retroreflection based on roughness

        float Fss90 = LoH * LoH * input.roughness;
        float3 Fss = Fd_Burley(NoV, NoL, 1.0f, Fss90);
        float3 ss = 1.25f * (Fss * (1.0f / (NoL + NoV) - 0.5f) + 0.5f);
        
        f90 = 0.5f + 2.0f * input.roughness * LoH * LoH;
        float3 diff = Fd_Burley(NoV, NoL, 1.0f, f90);
        
        // Multi-scattering
        float3 E_total_L = E_L * F_avg + Ems * (1.0 - E_L);
        float3 kD = saturate(1.0f - E_total_L);
        
        output.Fd = (V_INV_PI * diff * lerp(input.albedo, ss, input.subsurface) + Fsheen) * (1.0f - input.metallic) * kD;
    }

    output.NoL = NoL;
    output.NoV = NoV;

    return output;
}

struct PBROutput
{
    float3 indirectDiff;
    float3 indirectSpec;
    float3 directDiff;
    float3 directSpec;
};

#endif // !defined( PBR_H )
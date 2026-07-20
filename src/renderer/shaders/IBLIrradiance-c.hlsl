
#include "pbr.hlsli"
#include "Compute_Header.hlsli"

TextureCube<float4> EnvMap : register(t0);
RWTexture2DArray<float4> IrrMapOut : register(u0);

#define SAMPLE_COUNT 1024

struct PreFilterInput
{
    float4 screenSize;
    uint sampleCount;
    float roughness;
    float mipCount;
};

cbuffer ConstantBuffer : register(b0)
{
    PreFilterInput g_input;
};

//https://www.gamedev.net/forums/topic/691168-calculating-irradiance-map/5351523/
float3 sampleHemisphere(float u1, float u2)
{
    float z = u1;
    float phi = V_PI_2 * u2;
    float r = sqrt(max(0.0, 1.0 - z * z));

    float3 H;
    H.x = r * cos(phi);
    H.y = r * sin(phi);
    H.z = z;
    
    return H;
}

COMPUTE_MAIN
{
    float3 N = getSamplingVector(IN.DispatchThreadID, g_input.screenSize.xy);
    
    float3 up = abs(N.z) < 0.999 ? float3(0, 0, 1) : float3(1, 0, 0);
    float3 tangent = normalize(cross(up, N));
    float3 bitangent = cross(N, tangent);
    
    float3 irradiance = 0.0f;

    for (uint i = 0; i < SAMPLE_COUNT; i++)
    {
        float2 Xi = hammersley(i, SAMPLE_COUNT);
        float3 H = sampleHemisphere(Xi.x, Xi.y);
        float3 L = tangent * H.x + bitangent * H.y + N * H.z;
        float NoL = saturate(dot(L, N));
        irradiance += EnvMap.Sample(iblSampler, L).rgb * NoL;
    }
    
    float hemispherePDF = 1.0f / V_PI_2;
    irradiance /= SAMPLE_COUNT * hemispherePDF;

    IrrMapOut[IN.DispatchThreadID] = float4(irradiance, 1);
}
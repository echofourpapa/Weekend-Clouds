
#include "pbr.hlsli"
#include "Compute_Header.hlsli"

TextureCube<float3> EnvMap : register(t0);
RWTexture2DArray<float3> EnvMapOut : register(u0);

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

COMPUTE_MAIN
{
    float3 color = 0.0f;
    float weight = 0.0f;

    float roughness = g_input.roughness * g_input.roughness;
    
    float3 N = getSamplingVector(IN.DispatchThreadID, g_input.screenSize.xy);
    float3 V = N;
    
    for (uint i = 0; i < SAMPLE_COUNT; ++i)
    {
        float2 Xi = hammersley(i, SAMPLE_COUNT);
        float3 H = importanceSampleGGXIBL(Xi, roughness, N);
        float VoH = dot(V, H);
        float3 L = 2.0f * VoH * H - V;
        float NoL = saturate(dot(N, L));
        if (NoL > 0.0f)
        {
            float D = GTR2(NoL, roughness);
            float pdf = D / 4.0 + 0.001;
            float lod = computeLod(pdf, g_input.screenSize.zw, SAMPLE_COUNT);
            lod = roughness == 0.0f ? 0.0f : clamp(lod, 0.0f, float(g_input.mipCount - 1));
            
            float3 envColor = EnvMap.SampleLevel(iblSampler, L, lod).rgb;
            color += envColor * NoL;
            weight += NoL;
        }
    }
    if (weight > 0.0f)
        color /= weight;
    else
        color = float3(1, 0, 1);

	// Write out color to output cubemap.
	EnvMapOut[IN.DispatchThreadID] = color;
}
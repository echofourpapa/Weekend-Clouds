
#include "pbr.hlsli"
#include "Compute_Header.hlsli"

RWTexture2D<float3> splitSumOut : register(u0);

#define SAMPLE_COUNT 1024

cbuffer ConstantBuffer : register(b0)
{
    float4 screenSize;
    uint sampleCount;
    float roughness;
};

COMPUTE_MAIN
{
    float2 TexelSize = screenSize.xy;
    float2 UV = TexelSize * (IN.DispatchThreadID.xy);

    float3 splitSum = DFG(UV.x + 1e-5, UV.y, SAMPLE_COUNT);
    splitSumOut[IN.DispatchThreadID.xy] = splitSum;
}
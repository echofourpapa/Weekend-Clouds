
#include "pbr.hlsli"
#include "Compute_Header.hlsli"

Texture2DArray<float4> MipIn : register(t0);
RWTexture2DArray<float4> MipOut : register(u0);

cbuffer ConstantBuffer : register(b0)
{
	float4 screenSize;
};

COMPUTE_MAIN
{
    float2 uv = screenSize.xy * (IN.DispatchThreadID.xy + 0.5f);
    float4 color = MipIn.SampleLevel(linearClampSampler, float3(uv, IN.DispatchThreadID.z), 0);
    MipOut[IN.DispatchThreadID.xyz] = color;
}

#include "pbr.hlsli"
#include "Compute_Header.hlsli"

Texture2D<float4> MipIn : register(t0);
RWTexture2D<float4> MipOut : register(u0);

cbuffer ConstantBuffer : register(b0)
{
	float4 screenSize;
};

COMPUTE_MAIN
{
	
	float2 uv = screenSize.xy * (IN.DispatchThreadID.xy + 0.5f);
    float4 color = MipIn.SampleLevel(linearClampSampler, uv, 0);
	MipOut[IN.DispatchThreadID.xy] = color;

}
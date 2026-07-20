
#include "pbr.hlsli"
#include "Compute_Header.hlsli"

Texture2D<float3> EnvMap : register(t0);
RWTexture2DArray<float3> CubeMapOut : register(u0);

cbuffer ConstantBuffer : register(b0)
{
	float4 screenSize;
	uint sampleCount;
	float roughness;
};

COMPUTE_MAIN
{
	float3 v = getSamplingVector(IN.DispatchThreadID, screenSize.zw);

	// Convert Cartesian direction vector to spherical coordinates.
	float phi = -atan2(v.z, v.x);
	float theta = acos(v.y);

	// Sample equirectangular texture.
	float3 color = EnvMap.SampleLevel(linearWrapSampler, float2(phi / V_PI_2, theta / V_PI), 0);

	// Write out color to output cubemap.
	CubeMapOut[IN.DispatchThreadID] = color;
}
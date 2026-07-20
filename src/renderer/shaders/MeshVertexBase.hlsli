#if !defined( MESH_VERTEX_BASE_H )
#define MESH_VERTEX_BASE_H

#include "MeshStructs.hlsli"

#ifdef Shadow
cbuffer ConstantBuffer : register(b0)
{
	row_major float4x4 worldViewProj;
};
#else
cbuffer ConstantBuffer : register(b0)
{
    row_major float4x4 worldViewProj;
    row_major float4x4 prevWorldViewProj;
    row_major float4x4 world;
    row_major float4x4 jitterMatrix;
};
#endif

VS_OUTPUT main(VS_INPUT input)
{
    VS_OUTPUT output;

#ifdef Shadow
    output.pos = mul(float4(input.pos, 1), worldViewProj);
#else
    
    output.curPos = mul(float4(input.pos, 1), worldViewProj);
    output.prevPos = mul(float4(input.pos, 1), prevWorldViewProj);
    output.pos = mul(float4(input.pos, 1), jitterMatrix);

    output.wPos = mul(float4(input.pos, 1), world).xyz;

    float3 norm = mul(float4(input.norm, 0), world).xyz;
    output.norm = normalize(norm);

    float3 tan = mul(float4(input.tan.xyz, 0), world).xyz;
    output.tan = normalize(tan);

    float3 binorm = cross(output.norm, output.tan) * input.tan.w;
    
    output.binorm = normalize(binorm);
#endif
    
    output.uv0 = input.uv[0].xy;
    output.uv1 = input.uv[1].xy;

    return output;
}
#endif // !defined( MESH_VERTEX_BASE_H )
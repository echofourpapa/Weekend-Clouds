#if !defined( MESH_PIXEL_HEADER_H )
#define MESH_PIXEL_HEADER_H

SamplerState anisoWrapSampler : register(s0);

cbuffer ConstantBuffer : register(b1)
{
    float4 fun;
    float4 texInfo[6];
};

#endif // !defined( MESH_PIXEL_HEADER_H )
#if !defined( MESH_PIXEL_BISTRO_BASE_H )
#define MESH_PIXEL_BISTRO_BASE_H

#include "color_util.hlsli"
#include "MeshStructs.hlsli"
#include "MeshPixel_Header.hlsli"
#include "mesh_util.hlsli"
#include "normal_util.hlsli"

Texture2D TexBaseColor : register(t0);
#ifndef Shadow
Texture2D TexNormal : register(t1);
Texture2D TexMetal : register(t2);
Texture2D TexRough : register(t3);

#endif

PS_DEFERRED_OUTPUT main(VS_OUTPUT input, bool front : SV_IsFrontFace)
{
    
    float2 uvs = UnjitterTextureUV(input.uv0, fun.xy);
    float4 diffMap = TexBaseColor.Sample(anisoWrapSampler, uvs);
#ifndef Shadow
    float2 normalMap = TexNormal.Sample(anisoWrapSampler, uvs).xy;
    float metal = TexMetal.Sample(anisoWrapSampler, uvs).r;
    float rough = TexRough.Sample(anisoWrapSampler, uvs).r;
        
    float2 velocity = CalcVelocity(input.curPos, input.prevPos);
    
    float3 N = PerturbNormal(normalMap, input.norm, input.tan, input.binorm);

    N = front ? N : -N;
    PS_DEFERRED_OUTPUT output;
    output.albedo = diffMap.rgb;
    output.normal.xy = EncodeNormal(N);
    output.normal.zw = velocity;
    output.metalrough.xy = float2(metal, rough);
    output.metalrough.zw = 0.0f;
    return output;
#endif
}
#endif // !defined( MESH_VERTEX_BASE_H )
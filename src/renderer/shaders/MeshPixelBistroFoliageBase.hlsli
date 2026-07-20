#if !defined( MESH_PIXEL_BISTRO_FOLIAGE_BASE_H )
#define MESH_PIXEL_BISTRO_FOLIAGE_BASE_H

#include "color_util.hlsli"
#include "MeshStructs.hlsli"
#include "MeshPixel_Header.hlsli"
#include "mesh_util.hlsli"
#include "normal_util.hlsli"

Texture2D TexBaseColor : register(t0);
#ifndef Shadow
Texture2D TexNormal : register(t1);
Texture2D TexSpec : register(t2);
#endif

// https://bgolus.medium.com/anti-aliased-alpha-test-the-esoteric-alpha-to-coverage-8b177335ae4f
float CalcMipLevel(float2 texture_coord)
{
    float2 dx = ddx(texture_coord);
    float2 dy = ddy(texture_coord);
    float delta_max_sqr = max(dot(dx, dx), dot(dy, dy));
                
    return max(0.0, 0.5 * log2(delta_max_sqr));
}

PS_DEFERRED_OUTPUT main(VS_OUTPUT input, bool front : SV_IsFrontFace)
{
#ifdef Shadow
    float2 uvs = input.uv0;
#else
    float2 uvs = UnjitterTextureUV(input.uv0, fun.xy);
#endif

    float4 diffMap = TexBaseColor.Sample(anisoWrapSampler, uvs);
    diffMap.a *= 1 + CalcMipLevel(uvs * texInfo[0].zw) * 0.25;
    diffMap.a = (diffMap.a - 0.5) / max(fwidth(diffMap.a), 0.0001) + 0.5;
    clip(diffMap.a > 0.5f ? 1 : -1);
#ifndef Shadow
    float2 normalMap = TexNormal.Sample(anisoWrapSampler, uvs).xy;
    float2 metalrough = TexSpec.Sample(anisoWrapSampler, uvs).zy;
        
    float2 velocity = CalcVelocity(input.curPos, input.prevPos);
    
    float3 N = PerturbNormal(normalMap, input.norm, input.tan, input.binorm);
    N = front ? N : -N;
    PS_DEFERRED_OUTPUT output;
    output.albedo = diffMap.rgb;
    output.normal.xy = EncodeNormal(N);
    output.normal.zw = velocity;
    output.metalrough.xy = metalrough.xy;
    output.metalrough.zw = 0.0f;
    return output;
#endif
}

#endif // !defined( MESH_VERTEX_BASE_H )
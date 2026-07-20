#if !defined( MESH_UTIL_H )
#define MESH_UTIL_H

float2 CalcVelocity(float4 newPos, float4 oldPos)
{
    newPos /= newPos.w;
    newPos.xy = newPos.xy * float2(0.5f, -0.5f) + 0.5f;
    
    oldPos /= oldPos.w;
    oldPos.xy = oldPos.xy * float2(0.5f, -0.5f) + 0.5f;

    return oldPos.xy - newPos.xy;
}

float2 UnjitterTextureUV(float2 uv, float2 jitter)
{
    return uv - ddx_fine(uv) * jitter.x + ddy_fine(uv) * jitter.y;
}

#endif // !defined( MESH_UTIL_H )
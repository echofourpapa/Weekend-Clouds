#include "Compute_Header.hlsli"
#include "lighting.hlsli"

Texture2D<float> DepthTex : register(t0); // Depth
RWStructuredBuffer<GpuFrustum> outTiles : register(u0);

cbuffer ConstantBuffer : register(b0)
{
    row_major float4x4 invViewProjMtx;
    row_major float4x4 viewMtx;
    float4 screenSize;
    float4 tileSize;
    float3 cameraPos;
    float padding;
};

/*
    Float -> Int for InterlockMin/max
    https://www.jeremyong.com/graphics/2023/09/05/f32-interlocked-min-max-hlsl/
*/

// Check isnan(value) before use.
uint order_preserving_float_map(float value)
{
    // For negative values, the mask becomes 0xffffffff.
    // For positive values, the mask becomes 0x80000000.
    uint uvalue = asuint(value);
    uint mask = -int(uvalue >> 31) | 0x80000000;
    return uvalue ^ mask;
}

float inverse_order_preserving_float_map(uint value)
{
    // If the msb is set, the mask becomes 0x80000000.
    // If the msb is unset, the mask becomes 0xffffffff.
    uint mask = ((value >> 31) - 1) | 0x80000000;
    return asfloat(value ^ mask);
}

float4 TransformPlane(float4 p, float4x4 t, bool renormalize)
{
    p = mul(t, p);
    return renormalize ? p * rsqrt(dot(p.xyz, p.xyz)) : p;
}

float2 UVtoSS(float2 uv)
{
    return uv * float2(2.0, -2.0) + float2(-1.0, 1.0);
}

float3 UnprojectPoint(float4 screenPos, float4x4 mtx)
{
    float4 worldPos = mul(screenPos, mtx);
    return worldPos.xyz / worldPos.w;
}

GpuFrustum GetTileFrustum(uint2 pixelCoord, float2 depthBounds, float2 oneOverScreenSize, uint lightTileSize, float4x4 invViewProj, float4x4 viewMtx, float3 cameraPos)
{
    float2 uvTL = float2(pixelCoord) * oneOverScreenSize;
    float2 uvBR = uvTL + (float(lightTileSize) * oneOverScreenSize);

    float4 screenTL = float4(UVtoSS(uvTL), 1.0f, 1.0f);
    float4 screenBR = float4(UVtoSS(uvBR), 1.0f, 1.0f);
    float4 screenTR = float4(screenBR.x, screenTL.y, 1.0f, 1.0f);
    float4 screenBL = float4(screenTL.x, screenBR.y, 1.0f, 1.0f);
    
    float3 worldTL = UnprojectPoint(screenTL, invViewProj);
    float3 worldTR = UnprojectPoint(screenTR, invViewProj);
    float3 worldBL = UnprojectPoint(screenBL, invViewProj);
    float3 worldBR = UnprojectPoint(screenBR, invViewProj);
    
    float3 rayTL = worldTL - cameraPos;
    float3 rayTR = worldTR - cameraPos;
    float3 rayBL = worldBL - cameraPos;
    float3 rayBR = worldBR - cameraPos;
    
    float3 viewForward = normalize(float3(viewMtx._13, viewMtx._23, viewMtx._33));
    
    GpuFrustum tileFrustum;
    tileFrustum.planes[0] = float4(normalize(cross(worldTL - worldBL, rayTL)), 0);
    tileFrustum.planes[0].w = -dot(tileFrustum.planes[0].xyz, cameraPos);
    
    tileFrustum.planes[1] = float4(normalize(cross(worldBR - worldTR, rayBR)), 0);
    tileFrustum.planes[1].w = -dot(tileFrustum.planes[1].xyz, cameraPos);

    tileFrustum.planes[2] = float4(normalize(cross(worldTR - worldTL, rayTR)), 0);
    tileFrustum.planes[2].w = -dot(tileFrustum.planes[2].xyz, cameraPos);

    tileFrustum.planes[3] = float4(normalize(cross(worldBL - worldBR, rayBL)), 0);
    tileFrustum.planes[3].w = -dot(tileFrustum.planes[3].xyz, cameraPos);
    
    // Handle Sky/Background case (Reverse-Z 0.0) to prevent culling lights in open air
    float minD = (depthBounds.x > 10000.0f) ? 0.0f : depthBounds.x;
    float maxD = (depthBounds.y == 0.0f) ? 10000.0f : depthBounds.y;

    tileFrustum.planes[4] = float4( viewForward,-dot(viewForward, cameraPos) - minD);
    tileFrustum.planes[5] = float4(-viewForward, dot(viewForward, cameraPos) + maxD);


    return tileFrustum;
}

groupshared int g_depthMin;
groupshared int g_depthMax;

COMPUTE_MAIN
{
    if (IN.DispatchThreadID.x >= uint(screenSize.z) || IN.DispatchThreadID.y >= uint(screenSize.w))
    {
        return;
    }

    if (IN.GroupIndex == 0)
    {
        g_depthMin = 0x7fffffff; //0xffffffffu;
        g_depthMax = 0; //0;
    }
    GroupMemoryBarrierWithGroupSync();
    
    float2 TexelSize = screenSize.xy;
    float2 UV = TexelSize * (IN.DispatchThreadID.xy + 0.5f);
    
    float depth = saturate(DepthTex.SampleLevel(linearClampSampler, UV, 0));
    
    GroupMemoryBarrierWithGroupSync();
    
    if (depth != 0)
    {
        const float depthMin = WaveActiveMin(depth);
        const float depthMax = WaveActiveMax(depth);
        
        if (WaveIsFirstLane())
        {
            const int zMin = asint(depthMin);
            const int zMax = asint(depthMax); 
            InterlockedMin(g_depthMin, zMin);
            InterlockedMax(g_depthMax, zMax);
        }
    }
    
    GroupMemoryBarrierWithGroupSync();
    
    if (IN.GroupIndex == 0)
    {
        float minDepth = asfloat(g_depthMin);
        float maxDepth = asfloat(g_depthMax);
        
        float nearDepth = tileSize.y / maxDepth; 
        float farDepth = tileSize.y / minDepth; 
        
        GpuFrustum frustum = GetTileFrustum(IN.DispatchThreadID.xy, 
            float2(nearDepth, farDepth), 
            TexelSize, 
            LIGHT_TILE_SIZE, 
            invViewProjMtx, 
            viewMtx, 
            cameraPos);
        
        uint tileIdx = IN.GroupID.x + IN.GroupID.y * (uint(screenSize.z) / THREAD_X);
        outTiles[tileIdx] = frustum;
    }
}
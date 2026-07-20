#include "Compute_Header.hlsli"
#include "lighting.hlsli"

#define FLT_MAX 3.402823466e+38
#ifndef LIGHT_TILE_SIZE
#define LIGHT_TILE_SIZE 16
#endif
#define NUM_MAX_LIGHTS 256

// Inputs
Texture2D<float> DepthTex : register(t0);
StructuredBuffer<GPULight> lightBVH : register(t1);

// Outputs
RWStructuredBuffer<LightTileData> outTiles : register(u0);

cbuffer ConstantBuffer : register(b0)
{
    row_major float4x4 invViewProjMtx;
    row_major float4x4 viewMtx;
    float4 screenSize;
    float4 tileSize; // z = tilesX, w = tilesY
    float3 cameraPos;
    float padding;
};

// Shared Memory
groupshared uint s_depthMin;
groupshared uint s_depthMax;
groupshared uint s_coarseLightCount;
groupshared uint s_coarseLightIndices[NUM_MAX_LIGHTS];
groupshared uint s_fineLightMarked[NUM_MAX_LIGHTS];

// Cache light data for fine culling
groupshared float4 s_cachedLightPosRadSq[NUM_MAX_LIGHTS];
groupshared uint s_finalLightCount;

// --- Helper Functions ---

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
    tileFrustum.planes[1] = float4(normalize(cross(worldBR - worldTR, rayBR)), 0); 
    tileFrustum.planes[2] = float4(normalize(cross(worldTR - worldTL, rayTR)), 0); 
    tileFrustum.planes[3] = float4(normalize(cross(worldBL - worldBR, rayBL)), 0); 
    
    tileFrustum.planes[0].w = -dot(tileFrustum.planes[0].xyz, cameraPos);
    tileFrustum.planes[1].w = -dot(tileFrustum.planes[1].xyz, cameraPos);
    tileFrustum.planes[2].w = -dot(tileFrustum.planes[2].xyz, cameraPos);
    tileFrustum.planes[3].w = -dot(tileFrustum.planes[3].xyz, cameraPos);

    float minD = (depthBounds.x > 10000.0f) ? 0.0f : depthBounds.x;
    float maxD = (depthBounds.y == 0.0f) ? 10000.0f : depthBounds.y;

    tileFrustum.planes[4] = float4( viewForward, -dot(viewForward, cameraPos) - minD);
    tileFrustum.planes[5] = float4(-viewForward,  dot(viewForward, cameraPos) + maxD);

    return tileFrustum;
}

static const uint invalidIndex32 = (uint)-1;

[numthreads(LIGHT_TILE_SIZE, LIGHT_TILE_SIZE, THREAD_Z)]
void main(ComputeShaderInput IN)
{
    uint groupIdx = IN.GroupIndex;

    if (groupIdx == 0)
    {
        s_depthMin = 0x7fffffff;
        s_depthMax = 0;
        s_coarseLightCount = 0;
        s_finalLightCount = 0;
    }
    
    if (groupIdx < NUM_MAX_LIGHTS)
    {
        s_fineLightMarked[groupIdx] = 0;
    }
    
    GroupMemoryBarrierWithGroupSync();
    
    float2 TexelSize = screenSize.xy;
    float2 UV = TexelSize * (IN.DispatchThreadID.xy + 0.5f);
    
    float depth = 0.0f;
    if (IN.DispatchThreadID.x < uint(screenSize.z) && IN.DispatchThreadID.y < uint(screenSize.w))
    {
        depth = saturate(DepthTex.SampleLevel(linearClampSampler, UV, 0));
    }
    
    if (depth != 0.0f)
    {
        const float depthMin = WaveActiveMin(depth);
        const float depthMax = WaveActiveMax(depth);
        
        if (WaveIsFirstLane())
        {
            InterlockedMin(s_depthMin, asint(depthMin));
            InterlockedMax(s_depthMax, asint(depthMax));
        }
    }
    
    GroupMemoryBarrierWithGroupSync();

    float minDepthRaw = asfloat(s_depthMin);
    float maxDepthRaw = asfloat(s_depthMax);

    if (groupIdx == 0)
    {
        // OPTIMIZATION: Early out if tile is completely Sky (no depth writes)
        if (s_depthMin != 0x7fffffff)
        {
            float linearNear = tileSize.y / maxDepthRaw; 
            float linearFar = tileSize.y / minDepthRaw;

            GpuFrustum frustum = GetTileFrustum(
                IN.GroupID.xy * LIGHT_TILE_SIZE, 
                float2(linearNear, linearFar), 
                TexelSize, LIGHT_TILE_SIZE, invViewProjMtx, viewMtx, cameraPos
            );

            uint index = 0;
            do
            {
                GPULight node = lightBVH[index];
                bool hit = false;
                bool isLight = node.positionIntensity.w > 0.0f;
                
                if(isLight) 
                    hit = SphereIntersect(node.positionIntensity.xyz, node.positionIntensity.w, frustum, viewMtx);
                else
                    hit = FrustumIntersect(node.positionIntensity.xyz, node.color.xyz, frustum, viewMtx);

                if (hit)
                {
                    if (isLight)
                    {
                        uint slot = s_coarseLightCount;
                        if (slot < NUM_MAX_LIGHTS)
                        {
                            s_coarseLightIndices[slot] = index;
                            s_coarseLightCount++;
                        }
                    }
                    index = (isLight) ? node.missIdx : index + 1;
                }
                else
                {
                    index = node.missIdx;
                }
            } while (index != invalidIndex32 && s_coarseLightCount < NUM_MAX_LIGHTS);
        }
    }
    
    GroupMemoryBarrierWithGroupSync();

    uint coarseCount = s_coarseLightCount;
    if (groupIdx < coarseCount)
    {
        uint lightIdx = s_coarseLightIndices[groupIdx];
        GPULight l = lightBVH[lightIdx];
        
        float radiusSq = GetLightRadiusSquared(l.positionIntensity.w);
        s_cachedLightPosRadSq[groupIdx] = float4(l.positionIntensity.xyz, radiusSq);
    }
    GroupMemoryBarrierWithGroupSync();

    if (coarseCount > 0 && depth != 0.0f) 
    {
        float4 ndc = float4(UV * 2.0 - 1.0, depth, 1.0);
        ndc.y = -ndc.y;
        float3 worldPos = UnprojectPoint(ndc, invViewProjMtx);

        for (uint i = 0; i < coarseCount; ++i)
        {
            float4 lightDat = s_cachedLightPosRadSq[i];
            float3 toLight = lightDat.xyz - worldPos;
            float distSq = dot(toLight, toLight);
            
            if (distSq <= lightDat.w)
            {
                s_fineLightMarked[i] = 1; 
            }
        }
    }
    
    GroupMemoryBarrierWithGroupSync();

    uint tileIdx = IN.GroupID.x + IN.GroupID.y * (uint(screenSize.z) / LIGHT_TILE_SIZE);

    if (groupIdx < coarseCount)
    {
        if (s_fineLightMarked[groupIdx])
        {
            uint slot;
            InterlockedAdd(s_finalLightCount, 1, slot);
            
            uint globalLightIdx = s_coarseLightIndices[groupIdx];
            outTiles[tileIdx].lights[slot] = globalLightIdx;
        }
    }
    
    GroupMemoryBarrierWithGroupSync();

    if (groupIdx == 0)
    {
        outTiles[tileIdx].lightCount = s_finalLightCount;
        
        // Debug depths: If sky, write 0/0 to avoid NaN
        if (minDepthRaw == 0x7fffffff)
            outTiles[tileIdx].depths = float2(0, 0);
        else
            outTiles[tileIdx].depths = float2(minDepthRaw, maxDepthRaw);
    }
}
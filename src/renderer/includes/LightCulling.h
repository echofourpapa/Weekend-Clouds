#pragma once
#include "types.h"
#include <d3d12.h>
#include "Constants.h"
#include <vector>

#define NUM_MAX_LIGHTS 256

namespace Awesome
{
    class AwesomeGraphics;
    class Scene;
    struct Light;
    struct Resource;
    struct DescriptorHandle;

    struct AABB
    {
        XMFLOAT3 min;
        XMFLOAT3 max;

        AABB() : min{ FLT_MAX, FLT_MAX , FLT_MAX }, max{ -FLT_MAX, -FLT_MAX , -FLT_MAX } {};

        void Expand(const AABB& other);
        bool Intersects(const AABB& other);
        static AABB FromLight(Light light);
    };

    struct BVHNode
    {
        AABB bbox;
        uint32 missIdx;
        uint32 lightIdx;
    };

    struct GPULight
    {
        XMFLOAT4 positionIntensity;
        XMFLOAT3 color;
        uint32 leftIdx;
    };

    struct LightTileData
    {
        uint32 lightCount;
        uint32 lights[NUM_MAX_LIGHTS];
        XMFLOAT2 depths;
    };

    class TileLightCull
    {
    public:
        TileLightCull(AwesomeGraphics* awesome);
        ~TileLightCull();

        bool StartUp();
        bool TearDown();
        void UpdateMissIndices(uint32 nodeIdx, uint32 missIdx);

        void AnimateLights(float deltaTime);
        void BuildLightBVH(Scene* scene);

        void BulldLightTiles();

        ID3D12Resource* GetBVHBuffer();
        ID3D12Resource* GetTileBuffer();

        uint32 GetBVHCount();

    private:
        AwesomeGraphics* m_Awesome;

        ID3D12RootSignature* m_lightCullRS;
        uint32 m_lightCullPSO;
        
        std::vector<DescriptorHandle> m_handles;

        Resource* m_constants;
        Resource* m_bvhBuffer;
        Resource* m_tileBuffer;

        std::vector<BVHNode> m_lightBVH;

        void BuildBVH(std::vector<Light>& lights, uint32 start, uint32 end);

        uint32 m_nodeCount;
        void InitResources();
        void SetupHeapViews();
        void FreeResources();
    };
};
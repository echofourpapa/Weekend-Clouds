#pragma once
#include <d3d12.h>
#include "types.h"
#include "Resource.h"
#include <unordered_map>
#include <vector>

namespace Awesome {

    class AwesomeGraphics;
    struct ResourceDesc;

    class ResourcePool
    {
    public:
        ResourcePool(AwesomeGraphics* awesome);
        ~ResourcePool();

        Resource* Allocate(ResourceDesc desc, LPCWSTR name, D3D12_HEAP_TYPE type);
        void Free(Resource* buffer);

        void StartFrame();

        void TearDown();

    private:
        AwesomeGraphics* m_Awesome;
        std::unordered_map<size_t, std::vector<Resource>> m_pool;
    };

};
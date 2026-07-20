#pragma once
#include <d3d12.h>
#include "types.h"

namespace Awesome {

    struct Resource
    {
        ID3D12Resource* resource;
        uint64 lastFrameActive;
        bool gpuInUse;
        bool cpuInUse;
    };

    struct ResourceDesc
    {
        D3D12_RESOURCE_DESC desc;
        D3D12_RESOURCE_STATES state;
        D3D12_CLEAR_VALUE clear;

        static size_t GetHash(const ResourceDesc& desc);
    };

};
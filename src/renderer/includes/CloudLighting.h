#pragma once
#include <d3d12.h>
#include "types.h"

namespace Awesome
{
    class AwesomeGraphics;
    class CloudSystem;

    // Sun-transmittance field cache (docs/PLAN.md 4.2). Owns a 128x32x128 R16F
    // 3D texture of optical depth toward the sun, rebuilt one Z-slab per frame
    // (analytic, no marching). Descriptor slots (t8/u6) are registered with
    // CloudSystem; this class creates the resource and dispatches the build.
    class CloudLighting
    {
    public:
        static const uint32 c_dimX = 128, c_dimY = 32, c_dimZ = 128;
        static const uint32 c_slabZ = 8;               // must match CloudLightCache-c.hlsl

        CloudLighting(AwesomeGraphics* Awesome, CloudSystem* clouds);
        ~CloudLighting();

        bool StartUp();
        bool TearDown();
        // Assumes CloudSystem::BindCommon() already ran this frame. sliceIndex
        // (0..15) selects the Z-slab to refresh.
        void Build();

        ID3D12Resource* GetCache() const { return m_cache; }

    private:
        AwesomeGraphics* m_Awesome;
        CloudSystem* m_clouds;
        ID3D12Resource* m_cache = nullptr;
        uint32 m_buildPSO = (uint32)-1;
    };
}

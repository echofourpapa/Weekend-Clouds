#pragma once
#include <d3d12.h>
#include "types.h"

namespace Awesome
{
    class AwesomeGraphics;
    class CloudSystem;

    // Minimal single-scattering sky (docs/PLAN.md 4.1). Owns the transmittance and
    // sky-view LUTs and their two PSO permutations. Regenerates only when the sun
    // or turbidity changes. Descriptor slots (t4/u4, t5/u5) are owned/registered by
    // CloudSystem; this class only creates the resources and dispatches.
    class SkyAtmosphere
    {
    public:
        static const uint32 c_transW = 256, c_transH = 64;
        static const uint32 c_viewW = 192, c_viewH = 108;

        SkyAtmosphere(AwesomeGraphics* Awesome, CloudSystem* clouds);
        ~SkyAtmosphere();

        bool StartUp();
        bool TearDown();
        void MarkDirty() { m_dirty = true; }
        // Assumes CloudSystem::BindCommon() already ran this frame.
        void Render();

        ID3D12Resource* GetTransLUT() const { return m_transLUT; }
        ID3D12Resource* GetSkyViewLUT() const { return m_skyViewLUT; }

    private:
        AwesomeGraphics* m_Awesome;
        CloudSystem* m_clouds;
        ID3D12Resource* m_transLUT = nullptr;
        ID3D12Resource* m_skyViewLUT = nullptr;
        uint32 m_transPSO = (uint32)-1;
        uint32 m_viewPSO = (uint32)-1;
        bool m_dirty = true;
    };
}

#pragma once
#include <d3d12.h>
#include "types.h"

namespace Awesome
{
    class AwesomeGraphics;

    // DXR acceleration structure over the macro AABBs (docs/PLAN.md 4.13). Used
    // only by the RayQuery A/B comparison trace; the primary path is tiled
    // compute. Guarded by AwesomeGraphics::IsRaytracingSupported() at the call
    // site - if RT is unavailable this is never built and GetTLASAddress() is 0.
    class RTScene
    {
    public:
        RTScene(AwesomeGraphics* Awesome);
        ~RTScene();

        bool StartUp();
        bool TearDown();

        // Rebuild the BLAS (procedural AABBs) + single-instance TLAS from the
        // given AABB buffer (D3D12_RAYTRACING_AABB * count, in NON_PIXEL state).
        // Records on the current command list; call after generation upload.
        void Build(ID3D12Resource* aabbBuffer, uint32 count);

        D3D12_GPU_VIRTUAL_ADDRESS GetTLASAddress() const { return m_tlas ? m_tlas->GetGPUVirtualAddress() : 0; }
        bool IsBuilt() const { return m_built; }

    private:
        ID3D12Resource* CreateUAVBuffer(uint64 size, D3D12_RESOURCE_STATES state, const wchar_t* name);

        AwesomeGraphics* m_Awesome;
        ID3D12Resource* m_blas = nullptr;
        ID3D12Resource* m_tlas = nullptr;
        ID3D12Resource* m_scratch = nullptr;
        ID3D12Resource* m_instanceBuf = nullptr;   // one D3D12_RAYTRACING_INSTANCE_DESC (upload heap)
        uint64 m_blasSize = 0, m_tlasSize = 0, m_scratchSize = 0;
        bool m_built = false;
    };
}

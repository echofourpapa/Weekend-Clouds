#pragma once
#include "types.h"
#include <d3d12.h>
#include "Constants.h"
#include <vector>

namespace Awesome {
    class AwesomeGraphics;
    struct Resource;
    struct DescriptorHandle;
    
    class GTAO {
    public:
        GTAO(AwesomeGraphics* Awesome);
        ~GTAO();

        bool StartUp();
        bool TearDown();
        void Resize();
        
        void Render(float delta);
        ID3D12Resource* GetOutputBuffer();

        // Settings
        float m_intensity;
        float m_radius;
        float m_minRadius;
        float m_thickness;
        uint32 m_sliceCount;
        uint32 m_stepCount;

    private:
        void InitResources();     // Allocates Resources from Pool
        void SetupHeapViews();    // Creates Descriptors (CBV, SRV, UAV)
        void FreeResources();     // Returns Resources to Pool

        AwesomeGraphics* m_Awesome;
        ID3D12RootSignature* m_rootSignature;
        
        // Resources (Managed by Pool)
        Resource* m_gtaoConstantBuffer;
        Resource* m_gtaoOutput;
        
        uint32 m_gtaoPSO;
        
        std::vector<DescriptorHandle> m_handles;
    };
}
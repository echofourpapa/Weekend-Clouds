#pragma once
#include "types.h"
#include "Awesome.h"
#include "pix3.h"
#include <d3d12.h>
#include "Constants.h"
#include "DescriptorHeap.h"

namespace Awesome
{
    struct Resource;
    class TemporalAntiAliasing
    {
    public:
        static float Halton(uint32 i, uint32 b);
        static const uint32 c_taaSampleCount = 8;

        TemporalAntiAliasing(AwesomeGraphics* Awesome);
        ~TemporalAntiAliasing();
        bool StartUp();
        bool TearDown();
        void Resize();
        void Render(float delta);
        XMFLOAT4X4 GetFrameJitter();
        bool GetUseCompute() { return m_useCompute; }
        void SetUseCompute(bool value) { m_useCompute = value; }
        ID3D12Resource* GetOutput();

    private:
        AwesomeGraphics* m_Awesome;
        ID3D12RootSignature* m_rootSignature;
        Resource* m_taaConstantBuffer;
        Resource* m_taaOutput;
        Resource* m_taaAccumulation;
        Resource* m_taaPrevMotion;
        uint32 m_taaComputePSO;
        uint32 m_taaQuadPSO;
        uint32 m_frameCounter;
        XMFLOAT2 m_jitterCache[c_taaSampleCount];

        std::vector<DescriptorHandle> m_handles;

        bool m_useCompute;

        void SetupJitterCache();

        void InitResources();
        void SetupHeapViews();
        void FreeResources();
    };
};
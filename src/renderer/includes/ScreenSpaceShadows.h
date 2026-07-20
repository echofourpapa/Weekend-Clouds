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
    class ScreenSpaceShadows
    {
    public:

        ScreenSpaceShadows(AwesomeGraphics* Awesome);
        ~ScreenSpaceShadows();
        bool StartUp();
        bool TearDown();
        void Resize();
        void SetupRender();
        void Render(float delta);
        ID3D12Resource* GetOutputBuffer();

        float m_surfaceThickness = 0.005f;
        float m_bilinearThreshold = 0.02f;
        float m_shadowContrast = 4.f;


    private:
        AwesomeGraphics* m_Awesome;
        ID3D12RootSignature* m_rootSignature;
        Resource* m_screenSpaceShadowsConstantBuffer;
        Resource* m_screenSpaceShadowsOutput;
        uint32 m_screenSpaceShadowsComputePSO;
        std::vector<DescriptorHandle> m_handles;

        void InitResources();
        void FreeResources();
    };
};
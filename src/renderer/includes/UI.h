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
    class QuadMesh;

    class UISystem
    {
    public:

        UISystem(AwesomeGraphics* Awesome, QuadMesh* quad);
        ~UISystem();
        bool StartUp();
        bool TearDown();
        void Resize();
        void PreRender(const D3D12_CPU_DESCRIPTOR_HANDLE& dsvHandle);
        void Render();
        void PostRender(const D3D12_CPU_DESCRIPTOR_HANDLE& rtvHandle, const D3D12_CPU_DESCRIPTOR_HANDLE& dsvHandle);
        void FinalComposite();

        ID3D12Resource* GetOutputBuffer();
        void FreeResources();

    private:
        AwesomeGraphics* m_Awesome;
        ID3D12RootSignature* m_rootSignature;
        ID3D12DescriptorHeap* m_UISystemRtvDescHeap;
        Resource* m_UISystemConstantBuffer;
        Resource* m_UIRenderTargetOutput;
        QuadMesh* m_quad;
        uint32 m_UISystemCompHDRPSO;
        uint32 m_UISystemCompSDRPSO;

        std::vector<DescriptorHandle> m_handles;

        bool m_hack;

        void InitResources();
        void SetupHeapViews();
    };
};
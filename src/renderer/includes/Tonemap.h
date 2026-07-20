#pragma once
#include "Awesome.h"
#include <d3d12.h>
#include "DescriptorHeap.h"

namespace Awesome
{
    class Tonemap
    {
    public:
        Tonemap(AwesomeGraphics* Awesome);
        ~Tonemap();

        bool StartUp();
        bool TearDown();
        void Resize();
        
        void Render(bool tonemap, ID3D12Resource* inputColor);
        
        Tonemapper GetTonemapper() const  { return m_tonemapper; }
        void SetTonemapper(Tonemapper tonemapper) { m_tonemapper = tonemapper; }
        
        ID3D12Resource* GetOutputBuffer();

    private:
        AwesomeGraphics* m_Awesome;
        ID3D12RootSignature* m_rootSignature;
        Resource* m_tonemapConstantBuffer;
        Resource* m_tonemapOutput;
        uint32 m_tonemapComputePSO;
        
        Tonemapper m_tonemapper;

        std::vector<DescriptorHandle> m_handles;

        void InitResources();
        void FreeResources();
        
        // [CHANGE] Added inputColor argument
        void SetupHeapViews(ID3D12Resource* inputColor);
    };
}
#pragma once
#include "types.h"
#include "Awesome.h"
#include "pix3.h"
#include <d3d12.h>
#include "Constants.h"
#include <string>

namespace Awesome
{

    class MipGenerator
    {
    public:

        MipGenerator(AwesomeGraphics* Awesome);
        ~MipGenerator();
        bool StartUp();
        bool TearDown();

        void GenerateMips(ID3D12Resource* texture, D3D12_RESOURCE_STATES state);

    private:
        AwesomeGraphics* m_Awesome;
        ID3D12RootSignature* m_rootSignature;
        ID3D12DescriptorHeap* m_mgCbvSrvUavDescHeap;
        ID3D12Resource* m_mgConstantBuffer;
        uint32 m_mgComputePSO;
        uint32 m_mgCubeComputePSO;

    };
};
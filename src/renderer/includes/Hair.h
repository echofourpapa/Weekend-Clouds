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

    struct HairData
    {
        uint32 materialIndex;
        std::vector<XMFLOAT4> points; // X,Y,Z position, W is normalized position in the strand
        std::vector<XMUINT2> strands; // offset and length in segments
    };

    struct Hair
    {
        uint32 m_data;
        uint32 m_pointCount;
        uint32 m_strandCount;
        ID3D12Resource* m_pointsBuffer;
        ID3D12Resource* m_strandsBuffer;
        uint32 m_materialIndex;
    };

    class HairSystem
    {
    public:

        HairSystem(AwesomeGraphics* Awesome);
        ~HairSystem();
        bool StartUp();
        bool TearDown();
        void Resize();
        void Render(float delta);
        ID3D12Resource* GetOutputBuffer();
        uint32 CreateHair(HairData& inHair);

    private:
        AwesomeGraphics* m_Awesome;
        ID3D12RootSignature* m_rootSignature;
        Resource* m_hairConstantBuffer;
        Resource* m_hairOutput;
        uint32 m_hairComputePSO;
        std::vector<HairData> m_hairData;
        std::vector<Hair> m_hairInstances;

        std::vector<DescriptorHandle> m_handles;

        void InitResources();
        void FreeResources();

    };
};
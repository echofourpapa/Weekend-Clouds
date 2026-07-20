#pragma once
#include "types.h"
#include "Awesome.h"
#include "pix3.h"
#include <d3d12.h>
#include "Constants.h"
#include <string>

namespace Awesome
{

    const DXGI_FORMAT c_iblFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
    const DXGI_FORMAT c_splitSumFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
    struct IBLData
    {
        ID3D12Resource* specProbe;
        float mipCount;
        ID3D12Resource* irradiance;
    };

    class IBLProcessor
    {
    public:

        IBLProcessor(AwesomeGraphics* Awesome);
        ~IBLProcessor();
        bool StartUp();
        bool TearDown();
        bool AddIBL(const char* path, IBLData& iblData);
        ID3D12Resource* GenerateSplitSum();
    private:
        AwesomeGraphics* m_Awesome;
        ID3D12RootSignature* m_rootSignature;
        ID3D12DescriptorHeap* m_iblCbvSrvUavDescHeap;
        ID3D12Resource* m_iblConstantBuffer;
        uint32 m_iblComputePSO;
        uint32 m_iblPreFilterPSO;
        uint32 m_iblIrradiancePSO;
        uint32 m_splitSumComputePSO;


    };

    class IBLSystem
    {
    public:

        IBLSystem(AwesomeGraphics* Awesome);
        ~IBLSystem();
        bool StartUp();
        bool TearDown();
        uint32 AddIBL(const char* path, const char* name);
        const IBLData& GetProbe(uint32 index) { return m_iblProbes[index]; }

        uint32 GetCurrentIBL() { return m_currentIBL; }
        void SetCurrentIBL(uint32 ibl);

        const std::string& GetIBLName(uint32 index) { return m_iblNames[index]; }
        uint32 GetIBLCount() { return uint32(m_iblNames.size()); }

        uint32 GetIBLByName(std::string name);
        ID3D12Resource* GetSplitSum() { return m_splitSumTexture; }

        float GetSkyBlur() { return m_skyBlur; }
        void SetSkyBlur(float skyBlur) { m_skyBlur = skyBlur; }

    private:
        AwesomeGraphics* m_Awesome;
        std::vector<IBLData> m_iblProbes;
        std::vector<std::string> m_iblNames;
        IBLProcessor* m_iblproccessor;
        uint32 m_currentIBL;
        ID3D12Resource* m_splitSumTexture;
        float m_skyBlur;
    };
};
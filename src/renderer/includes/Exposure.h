#pragma once
#include "Awesome.h"
#include <d3d12.h>
#include "DescriptorHeap.h"

namespace Awesome
{
    class Exposure
    {
    public:
        Exposure(AwesomeGraphics* Awesome);
        ~Exposure();

        bool StartUp();
        bool TearDown();
        void Resize();
        
        // [CHANGE] Added inputColor argument
        void Render(float deltaTime, ID3D12Resource* inputColor);

        ID3D12Resource* GetOutputBuffer(); // Returns Combined (RGB=AWB, A=Lum)
        ID3D12Resource* GetHistogramBuffer();

        // Getters/Setters
        void SetLogMin(float min);
        float GetLogMin();
        void SetLogMax(float max);
        float GetLogMax();
        void SetAdaptRate(float value);
        float GetAdaptRate();

    private:
        AwesomeGraphics* m_Awesome;
        ID3D12RootSignature* m_histogramRS;
        ID3D12DescriptorHeap* m_exposureUavClearDescHeap;

        Resource* m_exposureConstantBuffer;
        Resource* m_histogram;
        Resource* m_averageLum; // History (Fast, Slow)
        Resource* m_awbOutput;  // Combined Output

        std::vector<DescriptorHandle> m_handles;

        float m_logMin;
        float m_logMax;
        float m_adaptRate;

        uint32 m_exposureHistogramPSO;
        uint32 m_exposureAveLumPSO;

        void InitResources();
        void FreeResources();
        void SetupFrame();
        
        // [CHANGE] Added inputColor argument
        void SetupHeapViews(ID3D12Resource* inputColor);
    };
}
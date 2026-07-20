#pragma once
#include "Awesome.h"
#include <d3d12.h>

namespace Awesome
{
    class Exposure;
    class Tonemap;

    class PostFX
    {
    public:
        PostFX(AwesomeGraphics* Awesome);
        ~PostFX();

        bool StartUp();
        bool TearDown();
        void Resize();
        
        // [CHANGE] Added inputColor argument
        void Render(bool tonemap, float deltaTime, ID3D12Resource* inputColor);

        ID3D12Resource* GetOutputBuffer();
        ID3D12Resource* GetExposureBuffer();
        ID3D12Resource* GetHistogramBuffer();

        void SetLogMin(float min);
        float GetLogMin();
        void SetLogMax(float max);
        float GetLogMax();
        void SetAdaptRate(float value);
        float GetAdaptRate();
        Tonemapper GetTonemapper();
        void SetTonemapper(Tonemapper t);

    private:
        AwesomeGraphics* m_Awesome;
        Exposure* m_exposure;
        Tonemap* m_tonemap;
    };
}
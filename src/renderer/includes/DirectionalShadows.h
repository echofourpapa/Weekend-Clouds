#pragma once
#include "types.h"
#include "Awesome.h"
#include "pix3.h"
#include <d3d12.h>
#include "Constants.h"

namespace Awesome
{
    struct Camera;
    struct Light;
    struct ViewFrustum;

    struct DirectionalShadowSettings
    {
        uint32 cascades = 4;
        float cascadeRanges[c_maxCascadeCount] = {10.0f, 25.0f, 50.0f, 75.0f};
        uint32 size = 2048;
        float bias = 0.002f;
        float softAmount = 5.0f;
        float blendRange = 0.02f;
    };

    class DirectionalShadows
    {
    public:

        DirectionalShadows(AwesomeGraphics* Awesome);
        ~DirectionalShadows();
        bool StartUp();
        bool TearDown();
        void Resize(uint32 size);
        void PreRender();
        void SetupRender(uint32 cascade);
        void FreeResources();
        ID3D12Resource* GetOutputBuffer();

        XMMATRIX GetCascadeMatrix(uint32 cascade, ViewFrustum frustum, Light* light);

        DirectionalShadowSettings GetSettings() { return m_settings; }
        void SetSettings(DirectionalShadowSettings settings);

    private:
        AwesomeGraphics* m_Awesome;
        ID3D12DescriptorHeap* m_castShadowsDsDescHeap;
        ID3D12Resource* m_castShadowsOutput[c_frameBufferCount * c_maxCascadeCount];
        DirectionalShadowSettings m_settings;

        void InitResources();

    };
};
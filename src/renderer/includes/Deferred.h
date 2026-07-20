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

    enum class DeferredBuffers
    {
        Albedo = 0,
        Normal,
        MetalRough,
        //AO,
        //Position,
        Count //Means Depth 
    };

    enum class DebugFlags
    {
        Lit,
        Albedo,
        Normal,
        Metal,
        Roughness,
        Motion,
        SSShadows,
        Shadow_MUL,
        Depth,
        LightTiles,
        TileBounds,
        GTAODebug,
        BentNormals
    };

    static const DXGI_FORMAT c_deferredFormats[uint32(DeferredBuffers::Count)] = { 
        DXGI_FORMAT_R11G11B10_FLOAT, 
        DXGI_FORMAT_R16G16B16A16_FLOAT, 
        DXGI_FORMAT_R8G8B8A8_UNORM,
        // DXGI_FORMAT_R16_FLOAT
        // DXGI_FORMAT_R16G16B16A16_FLOAT
    };
    static const float c_deferredClear[uint32(DeferredBuffers::Count)][4] = { 
        {0.0f, 0.2f, 0.4f, 1.0f} ,
        {0.5f, 0.5f, 0.0f, 0.0f} ,
        {0.0f, 0.0f, 0.0f, 0.0f} ,
        // {1.0f, 1.0f, 1.0f, 1.0f} };
        // {0.0f, 0.0f, 0.0f, 1.0f} 
    };
    
    class DeferredRenderer
    {
    public:

        enum LightingFlags
        {
            DirectDiffuse = 1u << 0,
            DirectSpecular = 1u << 1,
            IndirectDiffuse = 1u << 2,
            IndirectSpecular = 1u << 3,
            DirectionalShadows = 1u << 4,
            SSShadows = 1u << 5,
            GTAO = 1u << 6,
            HBIL = 1u << 7,
            CascadeDebug = 1u << 8,
            Sheen = 1u << 9,
            Dummy = 1u << 10
        };

        DeferredRenderer(AwesomeGraphics* Awesome);
        ~DeferredRenderer();
        bool StartUp();
        bool TearDown();
        void Resize();
        void PreRender(const D3D12_CPU_DESCRIPTOR_HANDLE& dsvHandle);
        void Render(float delta);
        void FreeResources();
        void PostRender(const D3D12_CPU_DESCRIPTOR_HANDLE& rvtHandle, const D3D12_CPU_DESCRIPTOR_HANDLE& dsvHandle);
        
        void SetIBL(uint32 ibl);

        void SetCastShadow();
        
        void SetupHistory();

        ID3D12DescriptorHeap* GetDeferredRTVDescHeap() { return m_defferedRTVDescHeap; }
        ID3D12Resource* GetNormalMotionBuffer();
        ID3D12Resource* GetOutputBuffer();
        ID3D12Resource* GetHistoryBuffer();
        ID3D12Resource* GetDeferredTarget(DeferredBuffers buffer);
        
        float m_sheenTint;

    private:
        AwesomeGraphics* m_Awesome;
        ID3D12RootSignature* m_rootSignature;
        ID3D12DescriptorHeap* m_defferedRTVDescHeap;
        Resource* m_defferedConstantBuffer;
        Resource* m_deferredTargets[uint32(DeferredBuffers::Count)];
        Resource* m_deferredOutput;
        Resource* m_deferredHistory;
        LightingConstants m_pixelConstants;
        uint32 m_deferredPSO;

        std::vector<DescriptorHandle> m_handles;

        void InitResources();
        void SetupHeapViews();
    };
};
#pragma once
#include <d3d12.h>
#include <dxgi1_4.h>
#include <vector>

#include "AwesomeProfiler.h"
#include "GTAO.h"
#include "types.h"


namespace Awesome
{
    class Scene;
    class QuadMesh;
    class MeshSystem;
    class MaterialSystem;
    class TextureSystem;
    class ComputeSystem;
    class DeferredRenderer;
    class TileLightCull;
    class TemporalAntiAliasing;
    class IBLSystem;
    class ScreenSpaceShadows;
    //class HairSystem;
    class PostFX;
    class GTAO;
    class UISystem;
    class AweosmeProfiler;
    class DirectionalShadows;
    class ResourcePool;
    class DescriptorHeap;
    enum class DebugFlags;
    enum LightingFlags;

    struct ImageData;

    class AwesomeGraphics
    {
    public:
        AwesomeGraphics(HWND hwnd, uint16 width, uint16 height);
        ~AwesomeGraphics();
        bool StartUp();
        void TearDown();

        bool StartStats();

        float GetCurrentMemory();
        float GetTotalMemory();

        bool Render(float delta);
        void WaitForNextFrame();
        bool FlushGPU();
        void EndFence();

        void PlayWorms();

        void SetViewport();

        bool Resize(uint32 width, uint32 height);
        bool ResetSize();
        void Fullscreen(bool value);
        bool GetFullScreen() { return m_fullscreen; }
        const HWND GetHWND() const { return m_hwnd; }

        bool BeginFrame(bool present = true);
        bool EndFrame(bool present = true);

        bool LoadBuiltin();

        ID3D12Resource* CreateBuffer(D3D12_RESOURCE_DESC& desc, LPCWSTR name, D3D12_HEAP_TYPE type, D3D12_RESOURCE_STATES state, const D3D12_CLEAR_VALUE* clear=nullptr);
        bool UploadBuffer(ID3D12Resource* resource, D3D12_RESOURCE_DESC& desc, uint64 size, const uint8* srcData);
        bool UploadTexture(ID3D12Resource* resource, D3D12_RESOURCE_DESC& desc, uint64 size, const ImageData& imgData);
        void TransitionResource(ID3D12Resource* resource, D3D12_RESOURCE_STATES prevState, D3D12_RESOURCE_STATES nexState);

        ID3D12Device* Device() const;
        ID3D12Device5* Device5() const { return m_device5; } // null when the runtime lacks it

        ID3D12GraphicsCommandList* GetCommandList() const;
        ID3D12GraphicsCommandList4* GetCommandList4() const; // null when the runtime lacks it

        bool IsRaytracingSupported() const { return m_rtSupported; }
        bool AreTypedUAVLoadsSupported() const { return m_typedUAVLoads; }
        uint32 GetRaytracingTier() const { return m_rtTier; }
        uint32 GetHighestShaderModel() const { return m_shaderModel; }
        ID3D12CommandQueue* GetCommandQueue() const;
        const D3D12_VIEWPORT* GetViewport() const;
        const D3D12_RECT* GetScissorRect() const;

        uint16 GetCurrentFrameIndex() { return m_frameIndex; }
        uint64 GetCurrentFrame() { return m_currentFrame; }

        ID3D12Resource* GetCurrentRenderTarget() { return GetRenderTarget(m_frameIndex); }
        ID3D12Resource* GetRenderTarget(uint32 frame) { return m_renderTargets[frame]; }
        ID3D12Resource* GetDepthStencilBuffer() { return m_depthStencilBuffer[m_frameIndex]; }
        ID3D12Resource* GetFinalOut() const;

        uint32 AddScene(Scene& scene);
        Scene* GetScene(uint32 index);
        Scene* GetCurrentScene() const;
        bool SetCurrentScene(Scene* scene);
        bool SetCurrentScene(uint32 index);
        uint32 GetSceneCount() const;

        uint16 GetWidth() { return m_width; }
        uint16 GetHeight() { return m_height; }

        ResourcePool* GetResourcePool();
        DescriptorHeap* GetMainDescHeap();

        QuadMesh* GetQuad();
        MaterialSystem* GetMaterialSystem();
        MeshSystem* GetMeshSystem();
        TextureSystem* GetTextureSystem();
        ComputeSystem* GetComputeSystem();
        DeferredRenderer* GetDeferredRenderer();
        TemporalAntiAliasing* GetTAA();
        IBLSystem* GetIBLSystem();
        ScreenSpaceShadows* GetSSShadows();
        //HairSystem* GetHairSystem();
        PostFX* GetPostFX();
        UISystem* GetUISystem();
        DirectionalShadows* GetDirectShadows();
        TileLightCull* GetTiledLights();
        GTAO* GetGTAO();
        
        AwesomeProfiler* GetProfiler();

        DXGI_FORMAT GetSwapChainFormat() { return m_hdr ? DXGI_FORMAT_R10G10B10A2_UNORM : DXGI_FORMAT_R8G8B8A8_UNORM; }
        DXGI_FORMAT GetUIFormat() { return DXGI_FORMAT_R8G8B8A8_UNORM; }

        bool HasImGui() { return m_hasImgui; }

        DebugFlags GetDeferredDebugFlags() { return m_debugFlags; }
        void SetDeferredDebugFlags(DebugFlags mode) { m_debugFlags = mode; }

        uint32 GetLightingDebugFlags() { return m_lightingFlags; }
        bool IsLightingFlagSet(LightingFlags flag) { return (m_lightingFlags & (uint32)flag) == (uint32)flag; }
        void SetLightingFlag(LightingFlags flag) { m_lightingFlags |= (uint32)flag; }
        void RemoveLightingFlag(LightingFlags flag) { m_lightingFlags &= ~(uint32)flag; }
        void SetLightingDebugFlags(uint32 flags) { m_lightingFlags = flags; }

        bool GetTAAEnabled() { return m_taaEnabled; }
        void SetTAAEnabled(bool value) { m_taaEnabled = value; }

        bool GetHairEnabled() { return m_hairEnabled; }
        void SetHairEnabled(bool value) { m_hairEnabled = value; }

        bool GetVSyncEnabled() { return m_vsyncEnabled; }
        void SetVSyncEnabled(bool value) { m_vsyncEnabled = value; }

        bool GetPostFXEnabled() { return m_postFXEnabled; }
        void SetPostFXEnabled(bool value) { m_postFXEnabled = value; }

        bool GetSSShadowsEnabled() { return m_sSShadowsEnabled; }
        void SetSSShadowsEnabled(bool value) { m_sSShadowsEnabled = value; }

        uint16 GetVSyncInterval() { return m_vsyncInverval; }
        void SetVSyncInterval(uint16 value) { m_vsyncInverval = value; }

        bool GetHDR() { return m_hdr; }
        void SetHDR(bool value);// { m_hdr = value; }

        bool GetHDRSupported() { return m_hdrSupported; }

        float GetMaxNits() { return m_maxNits; }
        float GetMaxFullNits() { return m_maxFullNits; }

        float GetCurrentNits() { return m_currentNits; }
        void SetCurrentNits(float value) { m_currentNits = value; }

        float GetUIPaperWhite() { return m_uiPaperWhite; }
        void SetUIPaperWhite(float value) { m_uiPaperWhite = value; }

    private:

        HWND m_hwnd;
        uint16 m_width;
        uint16 m_height;
        bool m_fullscreen;
        bool m_hasImgui;
        bool m_taaEnabled;
        bool m_hairEnabled;
        bool m_vsyncEnabled;
        bool m_postFXEnabled;
        bool m_sSShadowsEnabled;
        bool m_hdr;
        bool m_hdrSupported;
        bool m_playWorms;
        float m_maxNits;
        float m_maxFullNits;
        float m_currentNits;
        float m_uiPaperWhite;
        uint16 m_vsyncInverval;
        uint32 m_lightingFlags;
        DebugFlags m_debugFlags;

        ResourcePool* m_resourcePool;

        QuadMesh* m_quad;

        Scene* m_scene;
        MaterialSystem* m_materialSystem;
        MeshSystem* m_meshSystem;
        TextureSystem* m_textureSystem;
        ComputeSystem* m_computeSystem;
        DeferredRenderer* m_deferredRenderer;
        TemporalAntiAliasing* m_taa;
        IBLSystem* m_iblSystem;
        ScreenSpaceShadows* m_screenSpaceShadows;
        //HairSystem* m_hairSystem;
        PostFX* m_postFX;
        UISystem* m_UISystem;
        DirectionalShadows* m_directShadows;
        TileLightCull* m_tileLightCulling;
        GTAO* m_gtao;
        
        AwesomeProfiler* m_profiler;

        // Device stuff
        ID3D12Device* m_device;
        ID3D12Device5* m_device5 = nullptr;
        bool m_rtSupported = false;
        bool m_typedUAVLoads = false;
        uint32 m_rtTier = 0;       // D3D12_RAYTRACING_TIER value
        uint32 m_shaderModel = 0;  // D3D_SHADER_MODEL value (0x68 = SM 6.8)
        IDXGIAdapter3* m_adapter;
        // Render Stuff
        IDXGISwapChain3* m_swapChain;
        ID3D12Resource* m_renderTargets[c_frameBufferCount];
        ID3D12DescriptorHeap* m_rtvDescHeap;
        ID3D12Resource* m_depthStencilBuffer[c_frameBufferCount];
        ID3D12DescriptorHeap* m_dsDescHeap;

        DescriptorHeap* m_mainCbvSrvUavDescHeap;

        // Command Stuff
        ID3D12CommandQueue* m_commandQueue;
        ID3D12GraphicsCommandList* m_commandList[c_frameBufferCount];
        ID3D12GraphicsCommandList4* m_commandList4[c_frameBufferCount] = {};
        ID3D12CommandAllocator* m_commandAllocator[c_frameBufferCount];

        // Fence stuff
        ID3D12Fence* m_fence;
        HANDLE m_fenceEvent;
        uint64 m_currentFrame;
        uint64 m_lastCompleted;

        uint16 m_frameIndex;
        uint32 m_rtvDescSize;
        uint32 m_dsvDescSize;

        D3D12_VIEWPORT m_viewport;
        D3D12_RECT m_scissorRect;

        std::vector<ID3D12Resource*> m_toRelease;

        std::vector<Scene> m_scenes;

        bool CreateSwapChain();
        bool UpdateRenderTargetViews();
        bool UpdateDepthStencilView();

        void UpdateViewport();

        void ReleaseTempResources();
    };
};
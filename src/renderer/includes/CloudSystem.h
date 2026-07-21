#pragma once
#include <d3d12.h>
#include <DirectXMath.h>
#include <vector>
#include "types.h"
#include "DescriptorHeap.h"

namespace Awesome
{
    class AwesomeGraphics;
    class CloudShaderCompiler;
    class SkyAtmosphere;
    class CloudGenerator;
    class CloudLighting;

    // Shared descriptor-table slot assignments (docs/PLAN.md 3.5). Every cloud
    // pass binds the full SRV + UAV tables; a pass only touches the slots it needs.
    enum CloudSRV
    {
        SRV_Kernel = 0, SRV_Macro, SRV_Tile, SRV_Weather, SRV_SkyTrans, SRV_SkyView,
        SRV_Depth, SRV_BlueNoise, SRV_LightCache, SRV_HistColor, SRV_HistDepth,
        SRV_Scatter, SRV_CloudDepth, SRV_DenoiseTmp, SRV_MacroGrid, SRV_Count
    };
    enum CloudUAV
    {
        UAV_Kernel = 0, UAV_Macro, UAV_Tile, UAV_Weather, UAV_SkyTrans, UAV_SkyView,
        UAV_LightCache, UAV_Scatter, UAV_CloudDepth, UAV_DenoiseTmp, UAV_HistColor,
        UAV_HistDepth, UAV_HdrOut, UAV_MacroCount, UAV_MacroGrid, UAV_Count
    };

    static const uint32 c_cloudTilePx = 16;
    static const uint32 c_cloudMaxTileMacros = 64;

    // CPU mirror of CloudTile in CloudTileBin-c.hlsl / CloudTrace-c.hlsl.
    struct CloudTile
    {
        uint32 count;
        uint32 pad[3];
        uint32 macroIdx[c_cloudMaxTileMacros];
    };
    static_assert(sizeof(CloudTile) == 272, "CloudTile must be 272 bytes");

    // CPU mirror of the CloudConstants cbuffer in CloudCommon.hlsli (docs/PLAN.md 3.4).
    struct CloudConstants
    {
        DirectX::XMFLOAT4X4 invViewProj;
        DirectX::XMFLOAT4X4 prevViewProj;
        DirectX::XMFLOAT4 camPosWS;
        DirectX::XMFLOAT4 sunDirWS;
        DirectX::XMFLOAT4 sunRadiance;
        DirectX::XMFLOAT4 windOffset;
        DirectX::XMFLOAT4 windPhaseVel;
        DirectX::XMFLOAT4 cacheOriginWS;
        DirectX::XMFLOAT4 traceSize;
        DirectX::XMFLOAT4 outputSize;
        uint32 counts[4];
        DirectX::XMFLOAT4 lodParams;
        DirectX::XMFLOAT4 scatterParams;
        DirectX::XMFLOAT4 ambientParams;
        uint32 mode[4];
        DirectX::XMFLOAT4 temporal;
        DirectX::XMFLOAT4 skyParams;
        uint32 genParams[4];
        DirectX::XMFLOAT4 erosionParams;
    };
    static_assert(sizeof(CloudConstants) % 16 == 0, "CloudConstants must be 16-byte aligned");

    // Volumetric cloud renderer (docs/PLAN.md). The single cloud hook in
    // AwesomeGraphics. Owns the shared root signature, constant buffers, and
    // descriptor tables; drives all sub-objects and passes.
    class CloudSystem
    {
    public:
        CloudSystem(AwesomeGraphics* Awesome);
        ~CloudSystem();

        bool StartUp();
        bool TearDown();
        void Render(float delta);
        void ReloadShaders();

        // Shared binding used by every cloud pass (root sig, CB, full tables).
        void BindCommon();
        // Write a persistent view into one slot of every table copy.
        void WriteSRV(uint32 slot, ID3D12Resource* res, const D3D12_SHADER_RESOURCE_VIEW_DESC* desc);
        void WriteUAV(uint32 slot, ID3D12Resource* res, const D3D12_UNORDERED_ACCESS_VIEW_DESC* desc);
        // Create a committed UAV-capable 2D texture (starts in NON_PIXEL_SHADER_RESOURCE).
        ID3D12Resource* CreateTex2D(uint32 w, uint32 h, DXGI_FORMAT fmt, const wchar_t* name);

        ID3D12RootSignature* GetRootSignature() const { return m_rootSignature; }
        CloudGenerator* GetGenerator() const { return m_generator; }

        // --- ImGui-facing state ---
        bool  m_enabled = true;
        float m_timeOfDay = 14.0f;
        float m_turbidity = 3.0f;
        float m_sunIntensity = 20.0f;
        float m_windSpeed = 8.0f;     // m/s
        float m_windDir = 30.0f;      // degrees
        float m_maskAggressiveness = 1.0f;   // 1 = masking off; higher = more culling
        float m_survivalFloor = 0.05f;
        float m_temporalAlpha = 0.1f;         // temporal blend (lower = steadier, slower)
        bool  m_accumulate = false;           // static-camera progressive accumulation (validation)
        uint32 m_debugView = 0;
        uint32 m_lightMode = 0;
        uint32 m_traversalMode = 0;   // 0 tiled (primary), 3 brute (A/B)

    private:
        void UpdateConstants(float delta);
        void FillNullDescriptors();
        uint32 CurBlock() const;   // index into m_srvBlocks/m_uavBlocks for this frame

        AwesomeGraphics* m_Awesome;
        CloudShaderCompiler* m_shaderCompiler;
        SkyAtmosphere* m_sky;
        CloudGenerator* m_generator;
        CloudLighting* m_lighting;
        uint32 m_cacheSlice = 0;

        ID3D12RootSignature* m_rootSignature = nullptr;
        ID3D12Resource* m_scatterTex = nullptr;    // RGBA16F: rgb inscatter, a transmittance
        ID3D12Resource* m_cloudDepthTex = nullptr; // R16F
        ID3D12Resource* m_tileBuf = nullptr;       // per-tile macro lists
        ID3D12Resource* m_history[2] = {};         // temporal ping-pong (RGBA16F, trace res)
        ID3D12Resource* m_denoiseTex = nullptr;    // denoised scatter (RGBA16F, trace res)
        uint32 m_historyIdx = 0;
        uint32 m_denoisePSO = (uint32)-1;
        uint32 m_brutePSO = (uint32)-1;
        uint32 m_binPSO = (uint32)-1;
        uint32 m_tracePSO = (uint32)-1;
        uint32 m_reprojPSO = (uint32)-1;
        uint32 m_traceW = 0, m_traceH = 0;
        uint32 m_tileCountX = 0, m_tileCountY = 0;
        uint32 m_traceScale = 2;   // 2 = half-res trace, 4 = quarter

        ID3D12Resource* m_constantBuffer[c_frameBufferCount] = {};
        uint8* m_constantMapped[c_frameBufferCount] = {};
        CloudConstants m_constants = {};

        ID3D12Resource* m_nullTex = nullptr;   // fills unused descriptor slots
        uint32 m_compositePSO = (uint32)-1;
        float m_timeSeconds = 0.0f;
        float m_lastSunY = -999.0f;            // sky-dirty tracking
        // Accumulation change-detection state.
        DirectX::XMFLOAT4X4 m_prevViewProjForAccum = {};
        float m_prevTimeOfDay = -999.0f;
        uint32 m_accumCount = 0;

        // Persistent descriptor blocks from the Assets section: [frameIndex][pingpong].
        std::vector<DescriptorHandle> m_srvBlocks[c_frameBufferCount][2];
        std::vector<DescriptorHandle> m_uavBlocks[c_frameBufferCount][2];
    };
}

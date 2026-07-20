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

    // CPU mirror of the CloudConstants cbuffer in CloudCommon.hlsli.
    // Field order and packing are a contract (docs/PLAN.md 3.4) - change both or neither.
    struct CloudConstants
    {
        DirectX::XMFLOAT4X4 invViewProj;     // unjittered, current frame
        DirectX::XMFLOAT4X4 prevViewProj;    // unjittered, previous frame
        DirectX::XMFLOAT4 camPosWS;          // xyz cam pos, w = time seconds
        DirectX::XMFLOAT4 sunDirWS;          // xyz toward sun, w = sun angular radius (rad)
        DirectX::XMFLOAT4 sunRadiance;       // rgb, w = exposure hint
        DirectX::XMFLOAT4 windOffset;        // xyz accumulated advection (m), w = wind speed
        DirectX::XMFLOAT4 windPhaseVel;      // per-octave phase velocity (turns/s)
        DirectX::XMFLOAT4 cacheOriginWS;     // xyz cache voxel(0,0,0) origin, w = 1/voxelSize
        DirectX::XMFLOAT4 traceSize;         // x,y res; z,w 1/res
        DirectX::XMFLOAT4 outputSize;        // x,y res; z,w 1/res
        uint32 counts[4];                    // macroCount, kernelCount, tileCountX, tileCountY
        DirectX::XMFLOAT4 lodParams;         // footprintScale, lodSkipThreshold, maskAggressiveness, survivalFloor
        DirectX::XMFLOAT4 scatterParams;     // hgG0, hgG1, hgBlend, msOctaves
        DirectX::XMFLOAT4 ambientParams;     // ambientStrength, groundAlbedo, cloudBaseY, cloudTopY
        uint32 mode[4];                      // debugView, lightMode, traversalMode, frameIndex
        DirectX::XMFLOAT4 temporal;          // alphaBase, disocclusionTauDelta, accumCount, histBlendMax
        DirectX::XMFLOAT4 skyParams;         // turbidity, groundOffsetKm, lutPass, flags
        uint32 genParams[4];                 // seed, kernelsPerMacroPerOctave, octaveCount, cacheSliceIndex
        DirectX::XMFLOAT4 erosionParams;     // erosionBoundK, detailPosClampSigma, minSigmaM, maxSigmaM
    };
    static_assert(sizeof(CloudConstants) % 16 == 0, "CloudConstants must be 16-byte aligned");

    // Volumetric cloud renderer (docs/PLAN.md). Owns every cloud sub-object and is
    // the ONLY cloud-related hook in AwesomeGraphics. Runs after deferred lighting,
    // before Camera::EndFrame / TAA; composites into the deferred HDR output.
    class CloudSystem
    {
    public:
        CloudSystem(AwesomeGraphics* Awesome);
        ~CloudSystem();

        bool StartUp();
        bool TearDown();
        void Render(float delta);

        // Debug-only DXC hot reload: FlushGPU + rebuild all cloud PSOs from source.
        void ReloadShaders();

        // --- ImGui-facing state (GTAO-style public members) ---
        bool  m_enabled = true;
        float m_timeOfDay = 14.0f;        // hours; drives sun + sky LUTs (P1.3)
        uint32 m_debugView = 0;           // CLOUD_DBG_*
        uint32 m_lightMode = 0;           // CLOUD_LIGHT_*
        uint32 m_traversalMode = 3;       // CLOUD_TRAV_* (brute until P2)

    private:
        void UpdateConstants(float delta);

        AwesomeGraphics* m_Awesome;
        CloudShaderCompiler* m_shaderCompiler;

        ID3D12RootSignature* m_rootSignature = nullptr;

        // Persistently mapped per-frame-index constant buffers (DOD constraint C4).
        ID3D12Resource* m_constantBuffer[c_frameBufferCount] = {};
        uint8* m_constantMapped[c_frameBufferCount] = {};
        CloudConstants m_constants = {};

        float m_timeSeconds = 0.0f;

        // Persistent descriptor tables, allocated once from the Assets section
        // (docs/PLAN.md 3.5): [frameIndex(3)][historyPingPong(2)] SRV + UAV blocks.
        std::vector<DescriptorHandle> m_srvBlocks[c_frameBufferCount][2];
        std::vector<DescriptorHandle> m_uavBlocks[c_frameBufferCount][2];
    };
}

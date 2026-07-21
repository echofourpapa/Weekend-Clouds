#pragma once
#include <d3d12.h>
#include <vector>
#include "types.h"

namespace Awesome
{
    class AwesomeGraphics;
    class CloudSystem;

    // CPU mirror of CloudMacro in CloudKernels.hlsli (docs/PLAN.md 3.2), 64 bytes.
    struct CloudMacro
    {
        float position[3];
        float amplitude;
        float sigma[3];
        uint32 quatXY;
        uint32 quatZW;
        uint32 detailBegin;
        uint32 detailCount;
        uint32 seed;
        float boundRadius;
        float heightFrac01;
        uint32 octaveMask;
        uint32 pad0;
    };
    static_assert(sizeof(CloudMacro) == 64, "CloudMacro must be 64 bytes");

    // CPU mirror of CloudKernelPacked (docs/PLAN.md 3.1), 32 bytes.
    struct CloudKernelPacked { uint32 a[4]; uint32 b[4]; };
    static_assert(sizeof(CloudKernelPacked) == 32, "CloudKernelPacked must be 32 bytes");

    // Procedural cloudscape generation. Generation is CPU-side and amortized (it
    // only reruns when parameters change), so it does not violate C4 (the
    // per-frame path stays allocation-free); wind is phase-animated in the trace.
    // Macros come from a coverage FBM over a placement grid; each macro spawns
    // Gabor detail kernels across octaves in a fixed slot range [i*K, i*K+K).
    class CloudGenerator
    {
    public:
        static const uint32 c_maxMacros = 8192;
        static const uint32 c_maxKernels = 400 * 1024;
        static const uint32 c_gridDim = 128;             // placement cells per axis
        static const float  c_cellMeters;                // world size of one cell

        CloudGenerator(AwesomeGraphics* Awesome, CloudSystem* clouds);
        ~CloudGenerator();

        bool StartUp();
        bool TearDown();

        // Uploads freshly generated buffers on the first frame after a change.
        void EnsureUploaded();
        void RequestRegen() { m_dirty = true; }

        uint32 GetMacroCount() const { return m_macroCount; }
        uint32 GetKernelCount() const { return m_kernelCount; }
        ID3D12Resource* GetAABBBuffer() const { return m_aabbBuf; }   // D3D12_RAYTRACING_AABB per macro
        // True once after each upload, so the caller can rebuild the acceleration structure.
        bool ConsumeRebuildFlag() { bool r = m_rebuildAS; m_rebuildAS = false; return r; }

        // ImGui-facing generation params.
        float m_coverage = 0.5f;
        float m_cloudType = 0.6f;
        uint32 m_seed = 1337;
        uint32 m_octaves = 4;              // 1..4
        uint32 m_kernelsPerMacro = 32;     // total detail kernels per macro
        float m_cloudBaseY = 1400.0f;
        float m_cloudTopY = 3000.0f;

    private:
        void Regenerate();                 // CPU: fill m_cpuMacros + m_cpuKernels

        AwesomeGraphics* m_Awesome;
        CloudSystem* m_clouds;
        ID3D12Resource* m_macroBuf = nullptr;
        ID3D12Resource* m_kernelBuf = nullptr;
        ID3D12Resource* m_aabbBuf = nullptr;   // D3D12_RAYTRACING_AABB per macro (DXR A/B path)
        std::vector<CloudMacro> m_cpuMacros;
        std::vector<CloudKernelPacked> m_cpuKernels;
        std::vector<D3D12_RAYTRACING_AABB> m_cpuAABBs;
        bool m_rebuildAS = false;
        uint32 m_macroCount = 0;
        uint32 m_kernelCount = 0;
        bool m_dirty = true;
        bool m_uploaded = false;
        bool m_inReadState = false;   // buffers currently in NON_PIXEL (vs COPY_DEST)
    };
}

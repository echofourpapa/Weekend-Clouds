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
        uint32 quatXY;      // quat x,y as 2x snorm16
        uint32 quatZW;      // quat z,w as 2x snorm16 (w >= 0)
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

    // Owns the macro/kernel/count buffers. In P1.4 it authors a static test
    // cluster of macros on the CPU (proving the packing + analytic math);
    // GPU procedural generation replaces the CPU authoring in Phase 3.
    class CloudGenerator
    {
    public:
        static const uint32 c_maxMacros = 8192;
        static const uint32 c_maxKernels = 400 * 1024;

        CloudGenerator(AwesomeGraphics* Awesome, CloudSystem* clouds);
        ~CloudGenerator();

        bool StartUp();
        bool TearDown();

        // Uploads the authored CPU macros on the first frame (needs an open
        // command list); transitions the macro buffer to a shader resource.
        void EnsureUploaded();

        uint32 GetMacroCount() const { return m_macroCount; }
        uint32 GetKernelCount() const { return m_kernelCount; }

    private:
        void AuthorBruteScene();

        AwesomeGraphics* m_Awesome;
        CloudSystem* m_clouds;
        ID3D12Resource* m_macroBuf = nullptr;
        ID3D12Resource* m_kernelBuf = nullptr;
        std::vector<CloudMacro> m_cpuMacros;
        uint32 m_macroCount = 0;
        uint32 m_kernelCount = 0;
        bool m_uploaded = false;
    };
}

#include "CloudGenerator.h"
#include "CloudSystem.h"
#include "Awesome.h"
#include "Util.h"
#include <cmath>

using namespace Awesome;

static uint32 PackSnorm16x2(float a, float b)
{
    auto q = [](float v) -> uint32 {
        v = v < -1.0f ? -1.0f : (v > 1.0f ? 1.0f : v);
        int s = (int)lroundf(v * 32767.0f);
        return (uint32)(s & 0xFFFF);
    };
    return q(a) | (q(b) << 16);
}

CloudGenerator::CloudGenerator(AwesomeGraphics* Awesome, CloudSystem* clouds)
    : m_Awesome(Awesome)
    , m_clouds(clouds)
{
}

CloudGenerator::~CloudGenerator()
{
}

static ID3D12Resource* CreateStructured(AwesomeGraphics* g, uint32 count, uint32 stride, D3D12_RESOURCE_STATES state, const wchar_t* name)
{
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = (uint64)count * stride;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    desc.Format = DXGI_FORMAT_UNKNOWN;
    return g->CreateBuffer(desc, name, D3D12_HEAP_TYPE_DEFAULT, state);
}

bool CloudGenerator::StartUp()
{
    m_macroBuf = CreateStructured(m_Awesome, c_maxMacros, sizeof(CloudMacro), D3D12_RESOURCE_STATE_COPY_DEST, L"Cloud Macro Buffer");
    m_kernelBuf = CreateStructured(m_Awesome, c_maxKernels, sizeof(CloudKernelPacked), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, L"Cloud Kernel Buffer");
    if (!m_macroBuf || !m_kernelBuf)
        return false;

    AuthorBruteScene();

    // Register structured-buffer SRVs into the shared tables (t1 macro, t0 kernel).
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
        srv.Format = DXGI_FORMAT_UNKNOWN;
        srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Buffer.NumElements = c_maxMacros;
        srv.Buffer.StructureByteStride = sizeof(CloudMacro);
        m_clouds->WriteSRV(SRV_Macro, m_macroBuf, &srv);

        srv.Buffer.NumElements = c_maxKernels;
        srv.Buffer.StructureByteStride = sizeof(CloudKernelPacked);
        m_clouds->WriteSRV(SRV_Kernel, m_kernelBuf, &srv);
    }

    return true;
}

bool CloudGenerator::TearDown()
{
    SafeRelease(m_macroBuf);
    SafeRelease(m_kernelBuf);
    return true;
}

void CloudGenerator::AuthorBruteScene()
{
    // A small cumulus cluster: flattened Gaussians (sigmaY < sigmaXZ) around a
    // 2 km ceiling, spread over a few km. Pure Gaussians (no detail kernels).
    m_cpuMacros.clear();
    const int N = 14;
    unsigned seed = 1337u;
    auto rnd = [&](float lo, float hi) {
        seed = seed * 1664525u + 1013904223u;
        return lo + (hi - lo) * ((seed >> 8) & 0xFFFF) / 65535.0f;
    };

    for (int i = 0; i < N; ++i)
    {
        CloudMacro m = {};
        float x = rnd(-4000.0f, 4000.0f);
        float z = rnd(-4000.0f, 4000.0f);
        float y = rnd(1600.0f, 2400.0f);
        float sx = rnd(700.0f, 1400.0f);
        float sz = sx * rnd(0.8f, 1.2f);
        float sy = sx * rnd(0.35f, 0.6f);           // flattened
        m.position[0] = x; m.position[1] = y; m.position[2] = z;
        m.sigma[0] = sx; m.sigma[1] = sy; m.sigma[2] = sz;
        m.amplitude = rnd(0.025f, 0.06f);

        float yaw = rnd(-3.14159f, 3.14159f);       // exercise the quat unpack
        float qy = sinf(yaw * 0.5f), qw = cosf(yaw * 0.5f);
        m.quatXY = PackSnorm16x2(0.0f, qy);
        m.quatZW = PackSnorm16x2(0.0f, qw < 0.0f ? -qw : qw);
        m.detailBegin = 0; m.detailCount = 0;
        m.seed = (uint32)(i * 2654435761u);
        m.boundRadius = 3.0f * (sx > sz ? sx : sz);
        m.heightFrac01 = 0.5f;
        m.octaveMask = 0;
        m_cpuMacros.push_back(m);
    }
    m_macroCount = (uint32)m_cpuMacros.size();
    m_kernelCount = 0;
}

void CloudGenerator::EnsureUploaded()
{
    if (m_uploaded || m_cpuMacros.empty())
        return;

    D3D12_RESOURCE_DESC desc = m_macroBuf->GetDesc();
    uint64 bytes = (uint64)m_cpuMacros.size() * sizeof(CloudMacro);
    m_Awesome->UploadBuffer(m_macroBuf, desc, bytes, (const uint8*)m_cpuMacros.data());
    m_Awesome->TransitionResource(m_macroBuf, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    m_uploaded = true;
}

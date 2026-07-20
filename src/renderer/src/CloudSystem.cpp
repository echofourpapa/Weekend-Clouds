#include "CloudSystem.h"
#include "CloudShaderCompiler.h"
#include "Awesome.h"
#include "Compute.h"
#include "Scene.h"
#include "Util.h"

using namespace Awesome;
using namespace DirectX;

// Global register map (docs/PLAN.md 3.5): every cloud pass shares this root signature.
// p0 root CBV b0 | p1 SRV table t0-t14 | p2 UAV table u0-u14 | p3 root SRV t15 (TLAS)
static const uint32 c_cloudSrvSlots = 15;
static const uint32 c_cloudUavSlots = 15;

CloudSystem::CloudSystem(AwesomeGraphics* Awesome)
    : m_Awesome(Awesome)
    , m_shaderCompiler(new CloudShaderCompiler(Awesome))
{
}

CloudSystem::~CloudSystem()
{
    delete m_shaderCompiler;
}

bool CloudSystem::StartUp()
{
    m_shaderCompiler->StartUp();

    // Shared root signature for all cloud passes
    {
        D3D12_DESCRIPTOR_RANGE srvRange = {};
        srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srvRange.NumDescriptors = c_cloudSrvSlots;
        srvRange.BaseShaderRegister = 0;
        srvRange.RegisterSpace = 0;
        srvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_DESCRIPTOR_RANGE uavRange = {};
        uavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        uavRange.NumDescriptors = c_cloudUavSlots;
        uavRange.BaseShaderRegister = 0;
        uavRange.RegisterSpace = 0;
        uavRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_ROOT_PARAMETER params[4] = {};
        params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        params[0].Descriptor = { 0, 0 };   // b0
        params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[1].DescriptorTable = { 1, &srvRange };
        params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[2].DescriptorTable = { 1, &uavRange };
        params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        params[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;   // TLAS GPU VA (RQ paths; 0 = unused)
        params[3].Descriptor = { 15, 0 };  // t15
        params[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        D3D12_ROOT_SIGNATURE_DESC desc = {};
        desc.NumParameters = _countof(params);
        desc.pParameters = params;
        desc.NumStaticSamplers = _countof(c_computeSamplers);
        desc.pStaticSamplers = c_computeSamplers;
        desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

        ID3DBlob* signature = nullptr;
        ID3DBlob* errorBuff = nullptr;
        if (FAILED(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &errorBuff)))
        {
            if (errorBuff) DebugPrint((char*)errorBuff->GetBufferPointer());
            return false;
        }

        HRESULT hr = m_Awesome->Device()->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature));
        SafeRelease(signature);
        SafeRelease(errorBuff);
        if (FAILED(hr))
            return false;
        m_rootSignature->SetName(L"Cloud Root Signature");
    }

    // Persistently mapped constant buffers, one per frame index (C4: no per-frame allocation)
    {
        const uint64 cbSize = (sizeof(CloudConstants) + 255) & ~255ull;
        for (uint32 i = 0; i < c_frameBufferCount; ++i)
        {
            D3D12_RESOURCE_DESC desc = {};
            desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            desc.Width = cbSize;
            desc.Height = 1;
            desc.DepthOrArraySize = 1;
            desc.MipLevels = 1;
            desc.SampleDesc.Count = 1;
            desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            desc.Format = DXGI_FORMAT_UNKNOWN;

            m_constantBuffer[i] = m_Awesome->CreateBuffer(desc, L"Cloud Constants", D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
            if (!m_constantBuffer[i])
                return false;

            D3D12_RANGE noRead = { 0, 0 };
            if (FAILED(m_constantBuffer[i]->Map(0, &noRead, (void**)&m_constantMapped[i])))
                return false;
        }
    }

    // Persistent descriptor blocks from the Assets section (docs/PLAN.md 3.5).
    // Views are written into these slots as resources come online in later steps.
    for (uint32 f = 0; f < c_frameBufferCount; ++f)
    {
        for (uint32 p = 0; p < 2; ++p)
        {
            m_Awesome->GetMainDescHeap()->AllocateBlock(m_srvBlocks[f][p], c_cloudSrvSlots, DescriptorSection::Assets);
            m_Awesome->GetMainDescHeap()->AllocateBlock(m_uavBlocks[f][p], c_cloudUavSlots, DescriptorSection::Assets);
            if (m_srvBlocks[f][p].size() != c_cloudSrvSlots || m_uavBlocks[f][p].size() != c_cloudUavSlots)
            {
                DebugPrint("CloudSystem: descriptor block allocation failed (Assets section full?)\n");
                return false;
            }
        }
    }

    return true;
}

bool CloudSystem::TearDown()
{
    for (uint32 i = 0; i < c_frameBufferCount; ++i)
    {
        if (m_constantBuffer[i])
        {
            m_constantBuffer[i]->Unmap(0, nullptr);
            SafeRelease(m_constantBuffer[i]);
        }
        for (uint32 p = 0; p < 2; ++p)
        {
            for (DescriptorHandle& h : m_srvBlocks[i][p])
                m_Awesome->GetMainDescHeap()->Free(h);
            for (DescriptorHandle& h : m_uavBlocks[i][p])
                m_Awesome->GetMainDescHeap()->Free(h);
            m_srvBlocks[i][p].clear();
            m_uavBlocks[i][p].clear();
        }
    }

    SafeRelease(m_rootSignature);
    m_shaderCompiler->TearDown();
    return true;
}

void CloudSystem::ReloadShaders()
{
    if (!m_shaderCompiler)
        return;
    m_Awesome->FlushGPU();
    // PSO list is rebuilt here as passes come online (P1.3+). Empty for now.
    DebugPrint("CloudSystem: shader reload requested (no PSOs yet)\n");
}

void CloudSystem::UpdateConstants(float delta)
{
    m_timeSeconds += delta;
    Camera* cam = m_Awesome->GetCurrentScene()->GetCamera();

    // Camera viewProj/prevViewProj are unjittered (TAA jitter is applied per-mesh),
    // which is exactly what cloud rays need (docs/PLAN.md 4.8).
    XMMATRIX viewProj = cam->GetViewProjectionSpaceMatrix();
    XMStoreFloat4x4(&m_constants.invViewProj, XMMatrixInverse(nullptr, viewProj));
    XMStoreFloat4x4(&m_constants.prevViewProj, cam->prevViewProjMatrix);

    m_constants.camPosWS = { cam->transform.position.x, cam->transform.position.y, cam->transform.position.z, m_timeSeconds };

    float w = (float)m_Awesome->GetWidth();
    float h = (float)m_Awesome->GetHeight();
    m_constants.traceSize = { w, h, 1.0f / w, 1.0f / h };      // full-res until P5.2
    m_constants.outputSize = { w, h, 1.0f / w, 1.0f / h };

    m_constants.mode[0] = m_debugView;
    m_constants.mode[1] = m_lightMode;
    m_constants.mode[2] = m_traversalMode;
    m_constants.mode[3] = (uint32)m_Awesome->GetCurrentFrame();

    // Defaults refined as features come online (sun/sky P1.3, LOD/masking P5.1)
    m_constants.sunDirWS = { 0.0f, 1.0f, 0.0f, 0.004625f };
    m_constants.sunRadiance = { 1.0f, 1.0f, 1.0f, 1.0f };
    m_constants.scatterParams = { 0.85f, -0.15f, 0.7f, 3.0f };
    m_constants.ambientParams = { 1.0f, 0.3f, 1200.0f, 3200.0f };
    m_constants.lodParams = { 2.0f * tanf(cam->verticalFOV * 0.5f) / h, 0.02f, 1.0f, 0.05f };   // verticalFOV is radians
    m_constants.erosionParams = { 0.7f, 4.0f, 20.0f, 2000.0f };

    memcpy(m_constantMapped[m_Awesome->GetCurrentFrameIndex()], &m_constants, sizeof(CloudConstants));
}

void CloudSystem::Render(float delta)
{
    if (!m_enabled)
        return;

    PIXScopedEvent(m_Awesome->GetCommandList(), 0, "Clouds");
    UpdateConstants(delta);

    // Passes come online in later steps: sky LUTs + composite (P1.3),
    // trace (P1.4), tiles (P2), generation (P3), lighting (P4), temporal (P5).
}

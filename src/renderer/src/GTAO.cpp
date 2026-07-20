#include "GTAO.h"
#include "Awesome.h"
#include "Util.h"
#include "Compute.h"
#include "Constants.h"
#include "Material.h"
#include "Deferred.h"
#include "Scene.h"
#include "ResourcePool.h"
#include "DescriptorHeap.h"

using namespace Awesome;
using namespace DirectX;

GTAO::GTAO(AwesomeGraphics* Awesome)
    : m_Awesome(Awesome)
    , m_rootSignature(nullptr)
    , m_gtaoConstantBuffer(nullptr)
    , m_gtaoOutput(nullptr)
    , m_gtaoPSO(invalidIndex32)
    , m_intensity(1.f)
    , m_radius(1.5f)
    , m_minRadius(0.25f)
    , m_thickness(0.25f)
    , m_sliceCount(4)
    , m_stepCount(8)
{
}

GTAO::~GTAO()
{
}

bool GTAO::StartUp()
{
    // Range 0: CBV (b0)
    // Range 1: SRVs (t0, t1) -> Depth, Normal
    // Range 2: UAV (u0) -> Output
    {
        D3D12_DESCRIPTOR_RANGE ranges[3];

        ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
        ranges[0].NumDescriptors = 1;
        ranges[0].BaseShaderRegister = 0;
        ranges[0].RegisterSpace = 0;
        ranges[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        ranges[1].NumDescriptors = 2; 
        ranges[1].BaseShaderRegister = 0;
        ranges[1].RegisterSpace = 0;
        ranges[1].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        ranges[2].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        ranges[2].NumDescriptors = 1;
        ranges[2].BaseShaderRegister = 0;
        ranges[2].RegisterSpace = 0;
        ranges[2].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_ROOT_PARAMETER params[3];

        params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[0].DescriptorTable = { 1, &ranges[0] };
        params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[1].DescriptorTable = { 1, &ranges[1] };
        params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[2].DescriptorTable = { 1, &ranges[2] };
        params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        D3D12_ROOT_SIGNATURE_DESC desc = {};
        desc.NumParameters = _countof(params);
        desc.pParameters = params;
        desc.NumStaticSamplers = _countof(c_computeSamplers);
        desc.pStaticSamplers = c_computeSamplers;
        desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

        ID3DBlob* signature;
        ID3DBlob* errorBuff;
        if (FAILED(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &errorBuff)))
        {
            if (errorBuff) DebugPrint((char*)errorBuff->GetBufferPointer());
            return false;
        }

        if (FAILED(m_Awesome->Device()->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature))))
            return false;
    }

    std::vector<D3D_SHADER_MACRO*> permuations;
    D3D_SHADER_MACRO* defines = new D3D_SHADER_MACRO[1];
    defines[0] = { NULL, NULL };
    permuations.push_back(defines);

    uint32 shader = m_Awesome->GetComputeSystem()->CompileShader(L"GTAO-c", permuations);
    m_gtaoPSO = m_Awesome->GetComputeSystem()->CreatePipeline(shader, m_rootSignature);

    // PERFORMANCE: Allocate descriptors ONCE here.
    m_Awesome->GetMainDescHeap()->AllocateBlock(m_handles, 4);

    return true;
}

bool GTAO::TearDown()
{
    SafeRelease(m_rootSignature);
    
    // Release persistent handles
    for (auto& handle : m_handles)
        m_Awesome->GetMainDescHeap()->Free(handle);
    m_handles.clear();

    // Ensure any pool resources are freed if they weren't already
    if (m_gtaoConstantBuffer) m_Awesome->GetResourcePool()->Free(m_gtaoConstantBuffer);
    if (m_gtaoOutput) m_Awesome->GetResourcePool()->Free(m_gtaoOutput);

    return true;
}

void GTAO::Resize()
{
}

void GTAO::Render(float delta)
{
    PIXScopedEvent(m_Awesome->GetCommandList(), 0, "GTAO");

    // 1. Get Resources & Update Views
    InitResources();

    // 2. Transitions
    m_Awesome->TransitionResource(m_Awesome->GetDepthStencilBuffer(), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    m_Awesome->TransitionResource(m_Awesome->GetDeferredRenderer()->GetNormalMotionBuffer(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    m_Awesome->TransitionResource(m_gtaoOutput->resource, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    // 3. Pipeline Setup
    m_Awesome->GetCommandList()->SetComputeRootSignature(m_rootSignature);
    m_Awesome->GetComputeSystem()->SetPSO(m_gtaoPSO);

    // 4. Constants
    GTAOConstants constants = {};
    Camera* cam = m_Awesome->GetCurrentScene()->GetCamera();

    XMMATRIX projMtx = cam->GetProjectionMatrix();
    XMMATRIX invProjMtx = XMMatrixInverse(nullptr, projMtx);
    XMMATRIX viewMtx = cam->GetViewSpaceMatrix();
    XMMATRIX invViewMtx = XMMatrixInverse(nullptr, viewMtx);
    XMStoreFloat4x4(&constants.proj, invViewMtx);
    XMStoreFloat4x4(&constants.invProj, invProjMtx);
    XMStoreFloat4x4(&constants.view, viewMtx);
    
    constants.screenSize = { 
        (float)m_Awesome->GetWidth(), 
        (float)m_Awesome->GetHeight(), 
        1.0f / m_Awesome->GetWidth(), 
        1.0f / m_Awesome->GetHeight() 
    };
    constants.viewerPos = {
        cam->transform.position.x,
        cam->transform.position.y,
        cam->transform.position.z,
        1.0f
    };
    
    constants.intensity = m_intensity;
    constants.radius = m_radius;
    constants.minRadius = m_minRadius;
    constants.sliceCount = m_sliceCount;
    constants.stepCount = m_stepCount;
    constants.frameCount = static_cast<uint32>(m_Awesome->GetCurrentFrame());
    constants.falloff = m_thickness;
    
    void* mapped = nullptr;
    m_gtaoConstantBuffer->resource->Map(0, nullptr, &mapped);
    memcpy(mapped, &constants, sizeof(GTAOConstants));
    m_gtaoConstantBuffer->resource->Unmap(0, nullptr);

    // 5. Bind Descriptors
    m_Awesome->GetCommandList()->SetComputeRootDescriptorTable(0, m_handles[0].gpuHandle); // CBV
    m_Awesome->GetCommandList()->SetComputeRootDescriptorTable(1, m_handles[1].gpuHandle); // SRVs
    m_Awesome->GetCommandList()->SetComputeRootDescriptorTable(2, m_handles[3].gpuHandle); // UAV

    // 6. Dispatch
    uint32 dispatchX = 1 + (m_Awesome->GetWidth() - 1) / c_compute_thread_x;
    uint32 dispatchY = 1 + (m_Awesome->GetHeight() - 1) / c_compute_thread_y;
    m_Awesome->GetCommandList()->Dispatch(dispatchX, dispatchY, 1);

    // 7. Restore States
    m_Awesome->TransitionResource(m_Awesome->GetDepthStencilBuffer(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);
    m_Awesome->TransitionResource(m_Awesome->GetDeferredRenderer()->GetNormalMotionBuffer(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
    m_Awesome->TransitionResource(m_gtaoOutput->resource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    // 8. Release Resources
    FreeResources();
}

ID3D12Resource* GTAO::GetOutputBuffer()
{
    return m_gtaoOutput ? m_gtaoOutput->resource : nullptr;
}

void GTAO::InitResources()
{
    // --- 1. Allocate Resources from Pool (No null checks) ---

    // Constant Buffer
    {
        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Alignment = 0;
        desc.Width = (sizeof(GTAOConstants) + 255) & ~255;
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.SampleDesc.Count = 1;
        desc.SampleDesc.Quality = 0;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        desc.Flags = D3D12_RESOURCE_FLAG_NONE;
        desc.Format = DXGI_FORMAT_UNKNOWN;

        ResourceDesc rDesc = {};
        rDesc.desc = desc;
        rDesc.state = D3D12_RESOURCE_STATE_GENERIC_READ;
        m_gtaoConstantBuffer = m_Awesome->GetResourcePool()->Allocate(rDesc, L"GTAO CB", D3D12_HEAP_TYPE_UPLOAD);
    }

    // Output Buffer
    {
        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Alignment = 0;
        desc.Width = m_Awesome->GetWidth();
        desc.Height = m_Awesome->GetHeight();
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.SampleDesc.Count = 1;
        desc.SampleDesc.Quality = 0;
        desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        desc.Format = DXGI_FORMAT_R11G11B10_FLOAT; 

        ResourceDesc rDesc = {};
        rDesc.desc = desc;
        rDesc.state = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        m_gtaoOutput = m_Awesome->GetResourcePool()->Allocate(rDesc, L"GTAO Out", D3D12_HEAP_TYPE_DEFAULT);
    }

    // --- 2. Update Views on Persistent Handles ---
    m_Awesome->GetMainDescHeap()->AllocateBlock(m_handles, 4);
    // 0. CBV
    {
        D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
        cbvDesc.BufferLocation = m_gtaoConstantBuffer->resource->GetGPUVirtualAddress();
        cbvDesc.SizeInBytes = (uint32)m_gtaoConstantBuffer->resource->GetDesc().Width;
        m_Awesome->Device()->CreateConstantBufferView(&cbvDesc, m_handles[0].cpuHandle);
    }

    // 1. SRV: Depth (Cast D32 to R32 for reading)
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;
        srvDesc.Texture2D.MostDetailedMip = 0;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

        m_Awesome->Device()->CreateShaderResourceView(m_Awesome->GetDepthStencilBuffer(), &srvDesc, m_handles[1].cpuHandle);
    }

    // 2. SRV: Normal (t1)
    {
        ID3D12Resource* normal = m_Awesome->GetDeferredRenderer()->GetNormalMotionBuffer();
        m_Awesome->Device()->CreateShaderResourceView(normal, nullptr, m_handles[2].cpuHandle);
    }

    // 3. UAV: Output (u0)
    {
        m_Awesome->Device()->CreateUnorderedAccessView(m_gtaoOutput->resource, nullptr, nullptr, m_handles[3].cpuHandle);
    }
}

void GTAO::FreeResources()
{
    // Return resources to the pool
    m_Awesome->GetResourcePool()->Free(m_gtaoConstantBuffer);
    m_Awesome->GetResourcePool()->Free(m_gtaoOutput);
    
    for (auto& handle : m_handles)
        m_Awesome->GetMainDescHeap()->Free(handle);
    m_handles.clear();
}
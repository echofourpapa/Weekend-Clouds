#include "Tonemap.h"
#include "Util.h"
#include "Compute.h"
#include "Material.h"
#include "Deferred.h"
#include "Scene.h"
#include "Resource.h"
#include "ResourcePool.h"
#include "PostFX.h"
#include "DescriptorHeap.h"

using namespace Awesome;

Tonemap::Tonemap(AwesomeGraphics* Awesome)
    : m_Awesome(Awesome)
    , m_rootSignature(nullptr)
    , m_tonemapConstantBuffer(nullptr)
    , m_tonemapOutput(nullptr)
    , m_tonemapComputePSO(invalidIndex32)
    , m_tonemapper(Tonemapper::GT7)
    , m_handles{}
{
}

Tonemap::~Tonemap()
{
}

bool Tonemap::StartUp()
{
    InitResources();

    // Create Root Signature
    {
        D3D12_DESCRIPTOR_RANGE  descriptorTableRanges[3];
        descriptorTableRanges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
        descriptorTableRanges[0].NumDescriptors = 1;
        descriptorTableRanges[0].BaseShaderRegister = 0;
        descriptorTableRanges[0].RegisterSpace = 0;
        descriptorTableRanges[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        descriptorTableRanges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        descriptorTableRanges[1].NumDescriptors = 2;
        descriptorTableRanges[1].BaseShaderRegister = 0;
        descriptorTableRanges[1].RegisterSpace = 0;
        descriptorTableRanges[1].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        descriptorTableRanges[2].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        descriptorTableRanges[2].NumDescriptors = 1;
        descriptorTableRanges[2].BaseShaderRegister = 0;
        descriptorTableRanges[2].RegisterSpace = 0;
        descriptorTableRanges[2].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_ROOT_DESCRIPTOR_TABLE descriptorTable;
        descriptorTable.NumDescriptorRanges = _countof(descriptorTableRanges);
        descriptorTable.pDescriptorRanges = descriptorTableRanges;

        D3D12_ROOT_PARAMETER  rootParameters[1];
        rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rootParameters[0].DescriptorTable = descriptorTable;
        rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        D3D12_ROOT_SIGNATURE_DESC desc = {};
        desc.NumParameters = _countof(rootParameters);
        desc.pParameters = rootParameters;
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

    uint32 shader = m_Awesome->GetComputeSystem()->CompileShader(L"tonemapping-c", permuations);
    m_tonemapComputePSO = m_Awesome->GetComputeSystem()->CreatePipeline(shader, m_rootSignature);

    return true;
}

bool Tonemap::TearDown()
{
    FreeResources();
    SafeRelease(m_rootSignature);
    return true;
}

void Tonemap::Resize()
{
}

void Tonemap::Render(bool tonemap, ID3D12Resource* inputColor)
{
    PIXScopedEvent(m_Awesome->GetCommandList(), 0, "Tonemap Render");
    
    SetupHeapViews(inputColor);

    m_Awesome->TransitionResource(m_Awesome->GetPostFX()->GetExposureBuffer(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    m_Awesome->GetCommandList()->SetComputeRootSignature(m_rootSignature);
    m_Awesome->GetComputeSystem()->SetPSO(m_tonemapComputePSO);

    TonemapConstants constants = {};
    constants.minNits = 0.f;
    constants.maxNits = m_Awesome->GetCurrentNits();
    constants.paperWhiteNits = m_Awesome->GetUIPaperWhite();
    constants.sRGB = !m_Awesome->GetHDR();
    constants.tonemap = tonemap;
    constants.tonemapper = m_tonemapper;
    constants.screenSize = {
        1.0f / float(m_Awesome->GetWidth()),
        1.0f / float(m_Awesome->GetHeight()),
        float(m_Awesome->GetWidth()),
        float(m_Awesome->GetHeight()) };

    void* mapped = nullptr;
    m_tonemapConstantBuffer->resource->Map(0, nullptr, &mapped);
    memcpy(mapped, &constants, sizeof(TonemapConstants));
    m_tonemapConstantBuffer->resource->Unmap(0, nullptr);

    m_Awesome->GetCommandList()->SetComputeRootDescriptorTable(0, m_handles[0].gpuHandle);

    uint32 dispatchX = 1 + ((m_Awesome->GetWidth() - 1) / c_compute_thread_x);
    uint32 dispatchY = 1 + ((m_Awesome->GetHeight() - 1) / c_compute_thread_y);

    m_Awesome->GetCommandList()->Dispatch(dispatchX, dispatchY, 1);
    
    m_Awesome->TransitionResource(m_Awesome->GetPostFX()->GetExposureBuffer(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    
    FreeResources();
}

ID3D12Resource* Tonemap::GetOutputBuffer()
{
    return m_tonemapOutput->resource;
}

void Tonemap::FreeResources()
{
    m_Awesome->GetResourcePool()->Free(m_tonemapConstantBuffer);
    m_Awesome->GetResourcePool()->Free(m_tonemapOutput);
    for (auto& handle : m_handles)
        m_Awesome->GetMainDescHeap()->Free(handle);
    m_handles.clear();
}

void Tonemap::InitResources()
{
}

void Tonemap::SetupHeapViews(ID3D12Resource* inputColor)
{
    m_Awesome->GetMainDescHeap()->AllocateBlock(m_handles, 4);

    // 0. Constant Buffer
    {
        D3D12_RESOURCE_DESC resDesc = {};
        resDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        resDesc.Alignment = 0;
        resDesc.Width = TonemapConstantsAlignedSize;
        resDesc.Height = 1;
        resDesc.DepthOrArraySize = 1;
        resDesc.MipLevels = 1;
        resDesc.Format = DXGI_FORMAT_UNKNOWN;
        resDesc.SampleDesc.Count = 1;
        resDesc.SampleDesc.Quality = 0;
        resDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        resDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
        
        ResourceDesc rDesc = {};
        rDesc.desc = resDesc;
        rDesc.state = D3D12_RESOURCE_STATE_GENERIC_READ;
        m_tonemapConstantBuffer = m_Awesome->GetResourcePool()->Allocate(rDesc, L"Tonemap Constant Buffer", D3D12_HEAP_TYPE_UPLOAD);
    }

    // View 0: CBV
    {
        D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
        cbvDesc.BufferLocation = m_tonemapConstantBuffer->resource->GetGPUVirtualAddress();
        cbvDesc.SizeInBytes = TonemapConstantsAlignedSize;
        m_Awesome->Device()->CreateConstantBufferView(&cbvDesc, m_handles[0].cpuHandle);
    }
    
    // View 1: SRV (Input)
    {
        m_Awesome->Device()->CreateShaderResourceView(inputColor, nullptr, m_handles[1].cpuHandle);
    }

    // View 2: SRV (Exposure)
    {
        m_Awesome->Device()->CreateShaderResourceView(m_Awesome->GetPostFX()->GetExposureBuffer(), nullptr, m_handles[2].cpuHandle);
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
        desc.Format = m_Awesome->GetSwapChainFormat();

        ResourceDesc rDesc = {};
        rDesc.desc = desc;
        rDesc.state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        m_tonemapOutput = m_Awesome->GetResourcePool()->Allocate(rDesc, L"Tonemap Output Buffer", D3D12_HEAP_TYPE_DEFAULT);
    }

    // View 3: UAV (Output)
    {
        m_Awesome->Device()->CreateUnorderedAccessView(m_tonemapOutput->resource, nullptr, nullptr, m_handles[3].cpuHandle);
    }
}
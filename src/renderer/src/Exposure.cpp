#include "Exposure.h"
#include "Util.h"
#include "Compute.h"
#include "Material.h"
#include "Deferred.h"
#include "Scene.h"
#include "Resource.h"
#include "ResourcePool.h"
#include "DescriptorHeap.h"

using namespace Awesome;

Exposure::Exposure(AwesomeGraphics* Awesome)
    : m_Awesome(Awesome)
    , m_histogramRS(nullptr)
    , m_exposureUavClearDescHeap(nullptr)
    , m_exposureConstantBuffer(nullptr)
    , m_histogram(nullptr)
    , m_averageLum(nullptr)
    , m_awbOutput(nullptr)
    , m_handles{}
    , m_logMin(-10.0f)
    , m_logMax(20.0f)
    , m_adaptRate(1.f)
    , m_exposureHistogramPSO(invalidIndex32)
    , m_exposureAveLumPSO(invalidIndex32)
{
}

Exposure::~Exposure()
{
}

bool Exposure::StartUp()
{
    // Exposure Clear Descriptor Heap
    {
        D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
        heapDesc.NumDescriptors = 3;
        heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        if (FAILED(m_Awesome->Device()->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_exposureUavClearDescHeap))))
            return false;
        m_exposureUavClearDescHeap->SetName(L"Exposure UAV Clear Descriptor Heap");
    }

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
        descriptorTableRanges[2].NumDescriptors = 2; // History + Combined
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
            if(errorBuff) DebugPrint((char*)errorBuff->GetBufferPointer());
            return false;
        }

        if (FAILED(m_Awesome->Device()->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_histogramRS))))
            return false;
    }

    std::vector<D3D_SHADER_MACRO*> permuations;
    D3D_SHADER_MACRO* defines = new D3D_SHADER_MACRO[1];
    defines[0] = { NULL, NULL };
    permuations.push_back(defines);

    uint32 shader = m_Awesome->GetComputeSystem()->CompileShader(L"exposure_histogram-c", permuations);
    m_exposureHistogramPSO = m_Awesome->GetComputeSystem()->CreatePipeline(shader, m_histogramRS);

    shader = m_Awesome->GetComputeSystem()->CompileShader(L"exposure_average_lum-c", permuations);
    m_exposureAveLumPSO = m_Awesome->GetComputeSystem()->CreatePipeline(shader, m_histogramRS);

    return true;
}

bool Exposure::TearDown()
{
    FreeResources();
    SafeRelease(m_histogramRS);
    return true;
}

void Exposure::Resize()
{
}

void Exposure::Render(float deltaTime, ID3D12Resource* inputColor)
{
    // Setup stuffs
    {
        PIXScopedEvent(m_Awesome->GetCommandList(), 0, "Exposure Setup");
        SetupFrame();
        SetupHeapViews(inputColor);
        ExposureConstants constants = {};
        constants.minLogLum = m_logMin;
        constants.invLogLumrange = 1.0f / (m_logMax - m_logMin);
        constants.screenSize = {
            1.0f / float(m_Awesome->GetWidth()),
            1.0f / float(m_Awesome->GetHeight()),
            float(m_Awesome->GetWidth()),
            float(m_Awesome->GetHeight()) };
        constants.deltaTime = deltaTime;
        constants.tau = m_adaptRate;
        constants.hdr = m_Awesome->GetHDR();
        void* mapped = nullptr;
        m_exposureConstantBuffer->resource->Map(0, nullptr, &mapped);
        memcpy(mapped, &constants, sizeof(ExposureConstants));
        m_exposureConstantBuffer->resource->Unmap(0, nullptr);

        m_Awesome->GetCommandList()->SetComputeRootSignature(m_histogramRS);
    }

    {
        PIXScopedEvent(m_Awesome->GetCommandList(), 0, "Clear Histogram");
        auto clearHandle = m_exposureUavClearDescHeap->GetCPUDescriptorHandleForHeapStart();
        clearHandle.ptr += m_Awesome->GetCurrentFrameIndex() * m_Awesome->Device()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        const uint32 clearVal[4] = { 0,0,0,0 };
        m_Awesome->GetCommandList()->ClearUnorderedAccessViewUint(m_handles[3].gpuHandle, clearHandle, m_histogram->resource, clearVal, 0, nullptr);
    }

    {
        PIXScopedEvent(m_Awesome->GetCommandList(), 0, "Exposure Histogram");
        m_Awesome->GetCommandList()->SetComputeRootDescriptorTable(0, m_handles[0].gpuHandle);
        m_Awesome->GetComputeSystem()->SetPSO(m_exposureHistogramPSO);

        uint32 dispatchX = 1 + ((m_Awesome->GetWidth() - 1) / 16);
        uint32 dispatchY = 1 + ((m_Awesome->GetHeight() - 1) / 16);

        m_Awesome->GetCommandList()->Dispatch(dispatchX, dispatchY, 1);
    }

    {
        m_Awesome->TransitionResource(m_histogram->resource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

        PIXScopedEvent(m_Awesome->GetCommandList(), 0, "Exposure Average Luminance & AWB");
        m_Awesome->GetCommandList()->SetComputeRootDescriptorTable(0, m_handles[5].gpuHandle);
        m_Awesome->GetComputeSystem()->SetPSO(m_exposureAveLumPSO);
        m_Awesome->GetCommandList()->Dispatch(1, 1, 1);
    }

    FreeResources();
}

ID3D12Resource* Exposure::GetOutputBuffer()
{
    return m_awbOutput->resource; // Return Combined
}

ID3D12Resource* Exposure::GetHistogramBuffer()
{
    return m_histogram->resource;
}

void Exposure::FreeResources()
{
    m_Awesome->GetResourcePool()->Free(m_exposureConstantBuffer);
    m_Awesome->GetResourcePool()->Free(m_histogram);
    m_Awesome->GetResourcePool()->Free(m_awbOutput);
    m_Awesome->GetResourcePool()->Free(m_averageLum);
    for (auto& handle : m_handles)
        m_Awesome->GetMainDescHeap()->Free(handle);
    m_handles.clear();
}

void Exposure::InitResources()
{
    // History Buffer (Fast/Slow)
    {
        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Alignment = 0;
        desc.Width = 1;
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.SampleDesc.Count = 1;
        desc.SampleDesc.Quality = 0;
        desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        desc.Format = DXGI_FORMAT_R32G32_FLOAT; 

        ResourceDesc rDesc = {};
        rDesc.desc = desc;
        rDesc.state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        m_averageLum = m_Awesome->GetResourcePool()->Allocate(rDesc, L"Average Luminance History", D3D12_HEAP_TYPE_DEFAULT);
    }
    
    // Combined Output Buffer (RGB=AWB, A=Lum)
    {
        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Alignment = 0;
        desc.Width = 1;
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.SampleDesc.Count = 1;
        desc.SampleDesc.Quality = 0;
        desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT; 

        ResourceDesc rDesc = {};
        rDesc.desc = desc;
        rDesc.state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        m_awbOutput = m_Awesome->GetResourcePool()->Allocate(rDesc, L"Exposure Combined Output", D3D12_HEAP_TYPE_DEFAULT);
    }
}

void Exposure::SetupFrame()
{
    // Constant Buffer
    {
        D3D12_RESOURCE_DESC resDesc = {};
        resDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        resDesc.Alignment = 0;
        resDesc.Width = ExposureConstantsAlignedSize;
        resDesc.Height = 1;
        resDesc.DepthOrArraySize = 1;
        resDesc.MipLevels = 1;
        resDesc.Format = DXGI_FORMAT_UNKNOWN;
        resDesc.SampleDesc.Count = 1;
        resDesc.SampleDesc.Quality = 0;
        resDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        resDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

        wchar_t name[256];
        swprintf(name, 256, L"Exposure Constant Buffer");

        ResourceDesc rDesc = {};
        rDesc.desc = resDesc;
        rDesc.state = D3D12_RESOURCE_STATE_GENERIC_READ;
        m_exposureConstantBuffer = m_Awesome->GetResourcePool()->Allocate(rDesc, name, D3D12_HEAP_TYPE_UPLOAD);
    }

    // Histogram buffer
    {
        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Alignment = 0;
        desc.Width = 256 * sizeof(uint32);
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.SampleDesc.Count = 1;
        desc.SampleDesc.Quality = 0;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        desc.Format = DXGI_FORMAT_UNKNOWN;

        ResourceDesc rDesc = {};
        rDesc.desc = desc;
        rDesc.state = D3D12_RESOURCE_STATE_COMMON;
        m_histogram = m_Awesome->GetResourcePool()->Allocate(rDesc, L"Exposure Histogram", D3D12_HEAP_TYPE_DEFAULT);

        m_Awesome->TransitionResource(m_histogram->resource, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }
}

void Exposure::SetupHeapViews(ID3D12Resource* inputColor)
{
    m_Awesome->GetMainDescHeap()->AllocateBlock(m_handles, 10);

    // Constant View
    {
        D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
        cbvDesc.BufferLocation = m_exposureConstantBuffer->resource->GetGPUVirtualAddress();
        cbvDesc.SizeInBytes = ExposureConstantsAlignedSize;

        m_Awesome->Device()->CreateConstantBufferView(&cbvDesc, m_handles[0].cpuHandle);
        m_Awesome->Device()->CreateConstantBufferView(&cbvDesc, m_handles[5].cpuHandle);
    }

    // Shader Resource Views
    {
        m_Awesome->Device()->CreateShaderResourceView(inputColor, nullptr, m_handles[1].cpuHandle);
        m_Awesome->Device()->CreateShaderResourceView(inputColor, nullptr, m_handles[6].cpuHandle);
    }

    // SRVs for Output (Used by Tonemap)
    {
        // View 2: SRV Combined Output (Was AverageLum)
        m_Awesome->Device()->CreateShaderResourceView(m_awbOutput->resource, nullptr, m_handles[2].cpuHandle);
        
        // View 7: Histogram
        D3D12_SHADER_RESOURCE_VIEW_DESC desc = {};
        desc.Format = DXGI_FORMAT_R32_TYPELESS;
        desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        desc.Buffer.NumElements = 256;
        desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
        m_Awesome->Device()->CreateShaderResourceView(m_histogram->resource, &desc, m_handles[7].cpuHandle);
    }

    // Histogram UAV
    {
        D3D12_UNORDERED_ACCESS_VIEW_DESC desc = {};
        desc.Format = DXGI_FORMAT_R32_TYPELESS;
        desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        desc.Buffer.NumElements = 256;
        desc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;

        m_Awesome->Device()->CreateUnorderedAccessView(m_histogram->resource, nullptr, &desc, m_handles[3].cpuHandle);

        auto clearHandle = m_exposureUavClearDescHeap->GetCPUDescriptorHandleForHeapStart();
        clearHandle.ptr += m_Awesome->GetCurrentFrameIndex() * m_Awesome->Device()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        m_Awesome->Device()->CreateUnorderedAccessView(m_histogram->resource, nullptr, &desc, clearHandle);
    }

    // UAVs for AveLum & Combined
    {
        // First Pass Dummy
        m_Awesome->Device()->CreateUnorderedAccessView(m_awbOutput->resource, nullptr, nullptr, m_handles[4].cpuHandle);
        
        // Second Pass
        m_Awesome->Device()->CreateUnorderedAccessView(m_averageLum->resource, nullptr, nullptr, m_handles[8].cpuHandle); // u0 History
        m_Awesome->Device()->CreateUnorderedAccessView(m_awbOutput->resource, nullptr, nullptr, m_handles[9].cpuHandle);  // u1 Combined
    }
}

void Exposure::SetLogMin(float min) { m_logMin = min; }
float Exposure::GetLogMin() { return m_logMin; }

void Exposure::SetLogMax(float max) { m_logMax = max; }
float Exposure::GetLogMax() { return m_logMax; }

void Exposure::SetAdaptRate(float value) { m_adaptRate = value; }
float Exposure::GetAdaptRate() { return m_adaptRate; }
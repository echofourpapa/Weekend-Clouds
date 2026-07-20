#include "UI.h"
#include "Util.h"
#include "Compute.h"
#include "Material.h"
#include "Deferred.h"
#include "Scene.h"
#include "Resource.h"
#include "ResourcePool.h"
#include "QuadMesh.h"
#include "PostFX.h"

using namespace Awesome;

UISystem::UISystem(AwesomeGraphics* Awesome, QuadMesh* quad)
    : m_Awesome(Awesome)
    , m_rootSignature(nullptr)
    , m_UISystemRtvDescHeap(nullptr)
    , m_UISystemConstantBuffer(nullptr)
    , m_UIRenderTargetOutput{ nullptr }
    , m_quad(quad)
    , m_UISystemCompHDRPSO(invalidIndex32)
    , m_UISystemCompSDRPSO(invalidIndex32)
    , m_handles{}
    , m_hack(true)
{
}

UISystem::~UISystem()
{

}

bool UISystem::StartUp()
{

    // UI RTV Descriptor Heap
    {
        D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
        heapDesc.NumDescriptors = c_frameBufferCount;
        heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        if (FAILED(m_Awesome->Device()->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_UISystemRtvDescHeap))))
            return false;
        m_UISystemRtvDescHeap->SetName(L"UISystem RTV Descriptor Heap");
    }

    //InitResources();

    // Create Root Signature
    {
        D3D12_DESCRIPTOR_RANGE  descriptorTableRanges[1];

        descriptorTableRanges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        descriptorTableRanges[0].NumDescriptors = 4;
        descriptorTableRanges[0].BaseShaderRegister = 0;
        descriptorTableRanges[0].RegisterSpace = 0;
        descriptorTableRanges[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;


        // create a descriptor table
        D3D12_ROOT_DESCRIPTOR_TABLE descriptorTable;
        descriptorTable.NumDescriptorRanges = _countof(descriptorTableRanges);
        descriptorTable.pDescriptorRanges = descriptorTableRanges;

        D3D12_ROOT_PARAMETER  rootParameters[2];

        rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rootParameters[0].DescriptorTable = descriptorTable;
        rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        rootParameters[1].Descriptor = { 0,0 };
        rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

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
            DebugPrint((char*)errorBuff->GetBufferPointer());
            return false;
        }

        if (FAILED(m_Awesome->Device()->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature))))
            return false;
    }

    std::vector<D3D_SHADER_MACRO*> permuations;
    D3D_SHADER_MACRO* defines = new D3D_SHADER_MACRO[1];
    defines[0] = { NULL, NULL };
    permuations.push_back(defines);

    uint32 vshader = m_Awesome->GetMaterialSystem()->LoadShader(L"uicomp-v", ShaderType::Vertex, permuations);
    uint32 pshader = m_Awesome->GetMaterialSystem()->LoadShader(L"uicomp-p", ShaderType::Pixel, permuations);
    m_UISystemCompHDRPSO = m_Awesome->GetMaterialSystem()->CreateUIPipeline(vshader, pshader, m_quad->GetQuadMeshLayout(), m_rootSignature, true);
    m_UISystemCompSDRPSO = m_Awesome->GetMaterialSystem()->CreateUIPipeline(vshader, pshader, m_quad->GetQuadMeshLayout(), m_rootSignature, false);

    //FreeResources();

    return true;
}

bool UISystem::TearDown()
{
    FreeResources();
    SafeRelease(m_rootSignature);
    SafeRelease(m_UISystemRtvDescHeap);
    return true;
}

void UISystem::Resize()
{
}

void UISystem::PreRender(const D3D12_CPU_DESCRIPTOR_HANDLE& dsvHandle)
{
    PIXScopedEvent(m_Awesome->GetCommandList(), 0, "UI System Prerender");

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
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        desc.Format = m_Awesome->GetUIFormat();

        D3D12_CLEAR_VALUE clear = {};
        clear.Color[0] = c_uiClearColor[0];
        clear.Color[1] = c_uiClearColor[1];
        clear.Color[2] = c_uiClearColor[2];
        clear.Color[3] = c_uiClearColor[3];
        clear.Format = m_Awesome->GetUIFormat();

        ResourceDesc rDesc = {};
        rDesc.desc = desc;
        rDesc.state = D3D12_RESOURCE_STATE_RENDER_TARGET;
        rDesc.clear = clear;
        m_UIRenderTargetOutput = m_Awesome->GetResourcePool()->Allocate(rDesc, L"UI Render Target", D3D12_HEAP_TYPE_DEFAULT);
    }

    auto rtvHandle = m_UISystemRtvDescHeap->GetCPUDescriptorHandleForHeapStart();
    uint32 rtvOffset = m_Awesome->Device()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    rtvHandle.ptr += m_Awesome->GetCurrentFrameIndex() * rtvOffset;

    {
        D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
        rtvDesc.Format = m_Awesome->GetUIFormat();
        rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

        m_Awesome->Device()->CreateRenderTargetView(m_UIRenderTargetOutput->resource, &rtvDesc, rtvHandle);
    }
    
    m_Awesome->GetCommandList()->OMSetRenderTargets(1, &rtvHandle, TRUE, &dsvHandle);
    m_Awesome->GetCommandList()->ClearRenderTargetView(rtvHandle, c_uiClearColor, 0, NULL);

}

void UISystem::Render()
{
}

void UISystem::PostRender(const D3D12_CPU_DESCRIPTOR_HANDLE& rtvHandle, const D3D12_CPU_DESCRIPTOR_HANDLE& dsvHandle)
{
    m_Awesome->GetCommandList()->OMSetRenderTargets(1, &rtvHandle, TRUE, &dsvHandle);
}

void UISystem::FinalComposite()
{
    PIXScopedEvent(m_Awesome->GetCommandList(), 0, "UI System");
    SetupHeapViews();
    {
        PIXScopedEvent(m_Awesome->GetCommandList(), 0, "UI Composite");
        m_Awesome->TransitionResource(m_Awesome->GetPostFX()->GetHistogramBuffer(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        m_Awesome->TransitionResource(m_Awesome->GetFinalOut(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        m_Awesome->TransitionResource(m_UIRenderTargetOutput->resource, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

        m_Awesome->GetCommandList()->SetGraphicsRootSignature(m_rootSignature);
        uint32 pso = m_Awesome->GetHDR() ? m_UISystemCompHDRPSO : m_UISystemCompSDRPSO;
        m_Awesome->GetMaterialSystem()->SetPSO(pso);

        PostFXConstants constants = {};
        constants.maxNits = m_Awesome->GetUIPaperWhite();
        constants.sRGB = !m_Awesome->GetHDR();
        constants.tonemap = constants.sRGB;

        constants.screenSize = {
            1.0f / float(m_Awesome->GetWidth()),
            1.0f / float(m_Awesome->GetHeight()),
            float(m_Awesome->GetWidth()),
            float(m_Awesome->GetHeight()) };

        void* mapped = nullptr;
        m_UISystemConstantBuffer->resource->Map(0, nullptr, &mapped);
        memcpy(mapped, &constants, sizeof(PostFXConstants));
        m_UISystemConstantBuffer->resource->Unmap(0, nullptr);

        m_Awesome->GetCommandList()->SetGraphicsRootDescriptorTable(0, m_handles[0].gpuHandle);
        m_Awesome->GetCommandList()->SetGraphicsRootConstantBufferView(1, m_UISystemConstantBuffer->resource->GetGPUVirtualAddress());

        assert(m_quad != nullptr);
        m_quad->PreRender();

        m_Awesome->GetCommandList()->DrawIndexedInstanced(6, 1, 0, 0, 0);
    }
    m_Awesome->TransitionResource(m_Awesome->GetFinalOut(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    m_Awesome->TransitionResource(m_UIRenderTargetOutput->resource, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

    FreeResources();
}

ID3D12Resource* UISystem::GetOutputBuffer()
{
    return m_UIRenderTargetOutput->resource;
}

void UISystem::FreeResources()
{
    m_Awesome->GetResourcePool()->Free(m_UISystemConstantBuffer);
    m_Awesome->GetResourcePool()->Free(m_UIRenderTargetOutput);
    for (auto& handle : m_handles)
        m_Awesome->GetMainDescHeap()->Free(handle);
    m_handles.clear();
}

void UISystem::InitResources()
{

}

void UISystem::SetupHeapViews()
{
    m_Awesome->GetMainDescHeap()->AllocateBlock(m_handles, 4);
    // Constant Buffer
    {
        D3D12_RESOURCE_DESC resDesc = {};
        resDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        resDesc.Alignment = 0;
        resDesc.Width = PostFXConstantsAlignedSize;
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
        m_UISystemConstantBuffer = m_Awesome->GetResourcePool()->Allocate(rDesc, L"UI Comp Constant Buffer", D3D12_HEAP_TYPE_UPLOAD);
    }

    // Shader Resource Views
    {
        m_Awesome->Device()->CreateShaderResourceView(m_Awesome->GetFinalOut(), nullptr, m_handles[0].cpuHandle);
    }

    {
        m_Awesome->Device()->CreateShaderResourceView(m_UIRenderTargetOutput->resource, nullptr, m_handles[1].cpuHandle);
    }

    {
        D3D12_SHADER_RESOURCE_VIEW_DESC desc = {};
        desc.Format = DXGI_FORMAT_R32_UINT;
        desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        desc.Buffer.NumElements = 256;
        m_Awesome->Device()->CreateShaderResourceView(m_Awesome->GetPostFX()->GetHistogramBuffer(), &desc, m_handles[2].cpuHandle);
    }

    {
        m_Awesome->Device()->CreateShaderResourceView(m_Awesome->GetPostFX()->GetExposureBuffer(), nullptr, m_handles[3].cpuHandle);
    }
}

#include "Deferred.h"
#include "Material.h"
#include "Util.h"
#include "Compute.h"
#include "Scene.h"
#include "IBL.h"
#include "ScreenSpaceShadows.h"
#include "DirectionalShadows.h"
#include "LightCulling.h"
#include "TAA.h"
#include "Resource.h"
#include "ResourcePool.h"

using namespace Awesome;

enum DescHeapOffset
{
    CBV,
    GBuffer,
    Depth = GBuffer + uint32(DeferredBuffers::Count),
    CastShadow,
    SSShadow,
    Radiance,
    Irradiance,
    BRDF,
    LightBVH,
    LightTiles,
    GTAO,
    UAV,
    Count
};

DeferredRenderer::DeferredRenderer(AwesomeGraphics* Awesome)
	: m_Awesome(Awesome)
    , m_rootSignature(nullptr)
	, m_defferedRTVDescHeap(nullptr)
    , m_defferedConstantBuffer(nullptr)
	, m_deferredTargets{nullptr, nullptr, nullptr}
    , m_deferredOutput(nullptr)
    , m_deferredHistory(nullptr)
    , m_pixelConstants{}
    , m_deferredPSO(invalidIndex32)
    , m_sheenTint(0.5f)
    , m_handles{}
{
}

DeferredRenderer::~DeferredRenderer()
{
}

bool DeferredRenderer::StartUp()
{
    // Deferred Buffer Descriptor Heap
    {
        D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
        heapDesc.NumDescriptors = uint32(uint32(DeferredBuffers::Count)) * c_frameBufferCount;
        heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        if (FAILED(m_Awesome->Device()->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_defferedRTVDescHeap))))
            return false;
        m_defferedRTVDescHeap->SetName(L"Deferred RTV Descritor Heap");
    }
    
    // Create Root Signature
    {
        D3D12_DESCRIPTOR_RANGE  descriptorTableRanges[3];
        descriptorTableRanges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
        descriptorTableRanges[0].NumDescriptors = 1;
        descriptorTableRanges[0].BaseShaderRegister = 0;
        descriptorTableRanges[0].RegisterSpace = 0;
        descriptorTableRanges[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        descriptorTableRanges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        descriptorTableRanges[1].NumDescriptors = uint32(DeferredBuffers::Count) + 9; // Depth + Shadows + IBL(2) + Split Sum + Light Tiles + GTAO
        descriptorTableRanges[1].BaseShaderRegister = 0;
        descriptorTableRanges[1].RegisterSpace = 0;
        descriptorTableRanges[1].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        descriptorTableRanges[2].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        descriptorTableRanges[2].NumDescriptors = 1;
        descriptorTableRanges[2].BaseShaderRegister = 0;
        descriptorTableRanges[2].RegisterSpace = 0;
        descriptorTableRanges[2].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        // create a descriptor table
        D3D12_ROOT_DESCRIPTOR_TABLE descriptorTable;
        descriptorTable.NumDescriptorRanges = _countof(descriptorTableRanges);
        descriptorTable.pDescriptorRanges = &descriptorTableRanges[0];

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

    uint32 shader = m_Awesome->GetComputeSystem()->CompileShader(L"Deferred-c", permuations);
    m_deferredPSO = m_Awesome->GetComputeSystem()->CreatePipeline(shader, m_rootSignature);
    
    SetupHistory();
    
	return true;
}

bool DeferredRenderer::TearDown()
{
    SafeRelease(m_rootSignature);
    SafeRelease(m_defferedRTVDescHeap);
    m_Awesome->GetResourcePool()->Free(m_deferredHistory);
    FreeResources();
	return true;
}

void DeferredRenderer::Resize()
{
    m_Awesome->GetResourcePool()->Free(m_deferredHistory);
    SetupHistory();
}

void DeferredRenderer::PreRender(const D3D12_CPU_DESCRIPTOR_HANDLE& dsvHandle)
{
    PIXScopedEvent(m_Awesome->GetCommandList(), 0, "GBuffer Pre Render");
    InitResources();
    auto heap = GetDeferredRTVDescHeap();
    auto handle = heap->GetCPUDescriptorHandleForHeapStart();
    uint32 offset = m_Awesome->Device()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    handle.ptr += offset * uint32(DeferredBuffers::Count) * m_Awesome->GetCurrentFrameIndex();

    m_Awesome->GetCommandList()->OMSetRenderTargets(uint32(DeferredBuffers::Count), &handle, true, &dsvHandle);

    for (uint32 i = 0; i < uint32(DeferredBuffers::Count); ++i)
    {
        m_Awesome->GetCommandList()->ClearRenderTargetView(handle, c_deferredClear[i], 0, nullptr);
        handle.ptr += offset;
    }

    //SetupHeapViews();
}

void DeferredRenderer::Render(float delta)
{
    PIXScopedEvent(m_Awesome->GetCommandList(), 0, "Deferred Lighting");
    {
        SetupHeapViews();

        for (uint32 i = 0; i < uint32(DeferredBuffers::Count); ++i)
            m_Awesome->TransitionResource(m_deferredTargets[i]->resource, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        m_Awesome->TransitionResource(m_Awesome->GetDepthStencilBuffer(), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        m_Awesome->TransitionResource(m_Awesome->GetDirectShadows()->GetOutputBuffer(), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        m_Awesome->TransitionResource(m_Awesome->GetSSShadows()->GetOutputBuffer(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        m_Awesome->TransitionResource(m_Awesome->GetTiledLights()->GetTileBuffer(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        // m_Awesome->TransitionResource(m_Awesome->GetGTAO()->GetOutputBuffer(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        
        PIXScopedEvent(m_Awesome->GetCommandList(), 0, "Deferred Lighting Dispatch");
        m_Awesome->GetCommandList()->SetComputeRootSignature(m_rootSignature);

        m_Awesome->GetComputeSystem()->SetPSO(m_deferredPSO);

        LightingConstants pixelConstants = {};

        float mipCount = 0;
        if (m_Awesome->GetIBLSystem()->GetIBLCount() > 0)
        {
            uint32 iblIdx = m_Awesome->GetIBLSystem()->GetCurrentIBL();
            mipCount = m_Awesome->GetIBLSystem()->GetProbe(iblIdx).mipCount;
        }
        pixelConstants.iblMipCount = mipCount;
        pixelConstants.skyBlur = m_Awesome->GetIBLSystem()->GetSkyBlur();
        pixelConstants.cameraPos =
        {
            m_Awesome->GetCurrentScene()->GetCamera()->transform.position.x,
            m_Awesome->GetCurrentScene()->GetCamera()->transform.position.y,
            m_Awesome->GetCurrentScene()->GetCamera()->transform.position.z,
            1.f
        };

        pixelConstants.lightDir = {
            m_Awesome->GetCurrentScene()->GetSunLight()->position.x,
            m_Awesome->GetCurrentScene()->GetSunLight()->position.y,
            m_Awesome->GetCurrentScene()->GetSunLight()->position.z,
            m_Awesome->GetCurrentScene()->GetSunLight()->intensity
        };

        Camera* cam = m_Awesome->GetCurrentScene()->GetCamera();

        XMMATRIX viewProjMat = cam->GetViewProjectionSpaceMatrix();

        XMMATRIX jitter = XMMatrixIdentity();

        if (m_Awesome->GetTAAEnabled())
        {
            XMFLOAT4X4 j = m_Awesome->GetTAA()->GetFrameJitter();
            jitter = XMLoadFloat4x4(&j);
        }

        XMMATRIX jitterVP = viewProjMat * jitter;

        XMMATRIX invViewProjMat = XMMatrixInverse(nullptr, jitterVP);
        XMStoreFloat4x4(&pixelConstants.invViewProj, invViewProjMat);
        XMStoreFloat4x4(&pixelConstants.invViewProjUnjittered, XMMatrixInverse(nullptr, viewProjMat));

        Light* sun = m_Awesome->GetCurrentScene()->GetSunLight();

        DirectionalShadowSettings dss = m_Awesome->GetDirectShadows()->GetSettings();

        XMMATRIX projMat = cam->GetProjectionMatrix();
        XMMATRIX jitterP = projMat * jitter;
        for (uint32 c = 0; c < dss.cascades; c++) // LOL
        {
            float nC = c == 0 ? 0.5f : dss.cascadeRanges[c - 1];
            float fC = dss.cascadeRanges[c];

            ViewFrustum frustum = cam->GetFrustum(nC, fC);
            XMMATRIX shadowMatrix = m_Awesome->GetDirectShadows()->GetCascadeMatrix(c, frustum, sun);
            XMStoreFloat4x4(&pixelConstants.shadowMatrix[c], shadowMatrix);

            XMFLOAT4 dt = { 0, 0, dss.cascadeRanges[c], 1 };
            XMVECTOR depthTest = XMLoadFloat4(&dt);
            XMVECTOR csDT = XMVector3Transform(depthTest, jitterP);
            XMStoreFloat4(&dt, csDT);
            pixelConstants.cascadeDepths[c] = dt.z / dt.w;
        }

        pixelConstants.cascadeCount = dss.cascades;
        pixelConstants.shadowSettings.x = dss.bias;
        pixelConstants.shadowSettings.y = dss.softAmount;
        pixelConstants.shadowSettings.z = 1.0f - dss.blendRange;
        pixelConstants.shadowSettings.w = 1.0f/(float)dss.size;

        pixelConstants.lightFlags = uint32(m_Awesome->GetLightingDebugFlags());
        pixelConstants.debugFlags = uint32(m_Awesome->GetDeferredDebugFlags());

        pixelConstants.screenSize = {
            1.0f / float(m_Awesome->GetWidth()),
            1.0f / float(m_Awesome->GetHeight()),
            float(m_Awesome->GetWidth()),
            float(m_Awesome->GetHeight()) };
        pixelConstants.hdrMode = m_Awesome->GetHDR() ? 1 : 0;
        pixelConstants.sheenTint = m_sheenTint;
        pixelConstants.frameCount = m_Awesome->GetCurrentFrame();

        void* mapped = nullptr;
        m_defferedConstantBuffer->resource->Map(0, nullptr, &mapped);
        memcpy(mapped, &pixelConstants, sizeof(pixelConstants));
        m_defferedConstantBuffer->resource->Unmap(0, nullptr);

        m_Awesome->GetCommandList()->SetComputeRootDescriptorTable(0, m_handles[0].gpuHandle);

        uint32 dispatchX = 1 + ((m_Awesome->GetWidth() - 1) / c_compute_thread_x);
        uint32 dispatchY = 1 + ((m_Awesome->GetHeight() - 1) / c_compute_thread_y);

        m_Awesome->GetCommandList()->Dispatch(dispatchX, dispatchY, 1);

        for (uint32 i = 0; i < uint32(DeferredBuffers::Count); ++i)
            m_Awesome->TransitionResource(m_deferredTargets[i]->resource, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
        m_Awesome->TransitionResource(m_Awesome->GetDepthStencilBuffer(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);
        m_Awesome->TransitionResource(m_Awesome->GetDirectShadows()->GetOutputBuffer(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);
        m_Awesome->TransitionResource(m_Awesome->GetSSShadows()->GetOutputBuffer(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        // m_Awesome->TransitionResource(m_Awesome->GetGTAO()->GetOutputBuffer(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    }
    PIXScopedEvent(m_Awesome->GetCommandList(), 0, "Copy Lighting History");
    {
        m_Awesome->TransitionResource(m_deferredOutput->resource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
        
        D3D12_TEXTURE_COPY_LOCATION destination = {};
        destination.pResource = m_deferredHistory->resource;
        destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        destination.SubresourceIndex = 0;

        D3D12_TEXTURE_COPY_LOCATION source = {};
        source.pResource = m_deferredOutput->resource;
        source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        source.SubresourceIndex = 0;

        m_Awesome->GetCommandList()->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
        
        m_Awesome->TransitionResource(m_deferredOutput->resource, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }
    
}

void DeferredRenderer::FreeResources()
{
    m_Awesome->GetResourcePool()->Free(m_defferedConstantBuffer);
    m_Awesome->GetResourcePool()->Free(m_deferredOutput);
    for (uint32 i = 0; i < uint32(DeferredBuffers::Count); ++i)
        m_Awesome->GetResourcePool()->Free(m_deferredTargets[i]);
    for (auto& handle : m_handles)
        m_Awesome->GetMainDescHeap()->Free(handle);
    m_handles.clear();
}

void DeferredRenderer::PostRender(const D3D12_CPU_DESCRIPTOR_HANDLE& rvtHandle, const D3D12_CPU_DESCRIPTOR_HANDLE& dsvHandle)
{
    PIXScopedEvent(m_Awesome->GetCommandList(), 0, "GBuffer Post Render");
    m_Awesome->GetCommandList()->OMSetRenderTargets(1, &rvtHandle, TRUE, &dsvHandle);
}

void DeferredRenderer::SetIBL(uint32 ibl)
{

    if (ibl == invalidIndex32 || ibl >= m_Awesome->GetIBLSystem()->GetIBLCount())
        return;

    const IBLData& probe = m_Awesome->GetIBLSystem()->GetProbe(ibl);

    // Radiance
    {
        D3D12_RESOURCE_DESC desc = probe.specProbe->GetDesc();

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
        srvDesc.TextureCube.MostDetailedMip = 0;
        srvDesc.TextureCube.MipLevels = desc.MipLevels;
        srvDesc.TextureCube.ResourceMinLODClamp = 0.0f;
        srvDesc.Format = desc.Format;

        m_Awesome->Device()->CreateShaderResourceView(probe.specProbe, &srvDesc, m_handles[DescHeapOffset::Radiance].cpuHandle);
    }
    //Irradiance
    {
        D3D12_RESOURCE_DESC desc = probe.irradiance->GetDesc();

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
        srvDesc.TextureCube.MostDetailedMip = 0;
        srvDesc.TextureCube.MipLevels = desc.MipLevels;
        srvDesc.TextureCube.ResourceMinLODClamp = 0.0f;
        srvDesc.Format = desc.Format;

        m_Awesome->Device()->CreateShaderResourceView(probe.irradiance, &srvDesc, m_handles[DescHeapOffset::Irradiance].cpuHandle);
    }
}

void DeferredRenderer::SetCastShadow()
{
    DirectionalShadowSettings dss = m_Awesome->GetDirectShadows()->GetSettings();
    D3D12_SHADER_RESOURCE_VIEW_DESC dsDesc;
    ZeroMemory(&dsDesc, sizeof(dsDesc));
    dsDesc.Texture2DArray.MipLevels = 1;
    dsDesc.Texture2DArray.MostDetailedMip = 0;
    dsDesc.Texture2DArray.ArraySize = dss.cascades;
    dsDesc.Texture2DArray.FirstArraySlice = 0;
    dsDesc.Format = DXGI_FORMAT_R32_FLOAT;
    dsDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
    dsDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

    m_Awesome->Device()->CreateShaderResourceView(m_Awesome->GetDirectShadows()->GetOutputBuffer(), &dsDesc, m_handles[DescHeapOffset::CastShadow].cpuHandle);
}

void DeferredRenderer::SetupHistory()
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
    desc.Flags = D3D12_RESOURCE_FLAG_NONE;
    desc.Format = c_deferredFormats[(uint32)DeferredBuffers::Albedo];

    ResourceDesc hDesc = {};
    hDesc.desc = desc;
    hDesc.state = D3D12_RESOURCE_STATE_COPY_DEST;
    m_deferredHistory = m_Awesome->GetResourcePool()->Allocate(hDesc, L"Deferred History Buffer", D3D12_HEAP_TYPE_DEFAULT);
}

ID3D12Resource* DeferredRenderer::GetNormalMotionBuffer() { return m_deferredTargets[uint32(DeferredBuffers::Normal)]->resource; }

ID3D12Resource* DeferredRenderer::GetOutputBuffer() { return m_deferredOutput->resource; }
ID3D12Resource* DeferredRenderer::GetHistoryBuffer() { return m_deferredHistory->resource; }

ID3D12Resource* DeferredRenderer::GetDeferredTarget(DeferredBuffers buffer) { return m_deferredTargets[uint32(buffer)]->resource; }

void DeferredRenderer::InitResources()
{
    // Deferred GBuffers
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

        D3D12_CLEAR_VALUE clearValue = {};


        for (int i = 0; i < uint32(DeferredBuffers::Count); ++i)
        {
            clearValue.Color[0] = c_deferredClear[i][0];
            clearValue.Color[1] = c_deferredClear[i][1];
            clearValue.Color[2] = c_deferredClear[i][2];
            clearValue.Color[3] = c_deferredClear[i][3];
            desc.Format = c_deferredFormats[i];
            clearValue.Format = c_deferredFormats[i];
            wchar_t name[256];
            swprintf(name, 256, L"Deferred GBuffer %d", i);

            ResourceDesc rDesc = {};
            rDesc.desc = desc;
            rDesc.state = D3D12_RESOURCE_STATE_RENDER_TARGET;
            rDesc.clear = clearValue;
            m_deferredTargets[i] = m_Awesome->GetResourcePool()->Allocate(rDesc, name, D3D12_HEAP_TYPE_DEFAULT);

        }
    }

    // Render Target Views
    {
        D3D12_RENDER_TARGET_VIEW_DESC desc;
        desc.Texture2D.MipSlice = 0;
        desc.Texture2D.PlaneSlice = 0;
        desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

        uint32 offset = m_Awesome->Device()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

        auto handle = m_defferedRTVDescHeap->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += offset * uint32(DeferredBuffers::Count) * m_Awesome->GetCurrentFrameIndex();

        for (int i = 0; i < uint32(DeferredBuffers::Count); i++) {
            desc.Format = c_deferredFormats[i];
            m_Awesome->Device()->CreateRenderTargetView(m_deferredTargets[i]->resource, &desc, handle);
            handle.ptr += offset;
        }
    }
}

void DeferredRenderer::SetupHeapViews()
{
    m_Awesome->GetMainDescHeap()->AllocateBlock(m_handles, uint32(DescHeapOffset::Count));

    // Constant Buffer
    {
        D3D12_RESOURCE_DESC resDesc = {};
        resDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        resDesc.Alignment = 0;
        resDesc.Width = LightingConstantsAlignedSize;
        resDesc.Height = 1;
        resDesc.DepthOrArraySize = 1;
        resDesc.MipLevels = 1;
        resDesc.Format = DXGI_FORMAT_UNKNOWN;
        resDesc.SampleDesc.Count = 1;
        resDesc.SampleDesc.Quality = 0;
        resDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        resDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

        wchar_t name[256];
        swprintf(name, 256, L"Deferred Constant Buffer");

        ResourceDesc rDesc = {};
        rDesc.desc = resDesc;
        rDesc.state = D3D12_RESOURCE_STATE_GENERIC_READ;
        m_defferedConstantBuffer = m_Awesome->GetResourcePool()->Allocate(rDesc, name, D3D12_HEAP_TYPE_UPLOAD);
    }

    // Constant Buffer View
    {
        D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
        cbvDesc.BufferLocation = m_defferedConstantBuffer->resource->GetGPUVirtualAddress();
        cbvDesc.SizeInBytes = LightingConstantsAlignedSize;

        m_Awesome->Device()->CreateConstantBufferView(&cbvDesc, m_handles[DescHeapOffset::CBV].cpuHandle);
    }


    // Shader Resource Views
    {
        for (int i = 0; i < uint32(DeferredBuffers::Count); i++) {
            m_Awesome->Device()->CreateShaderResourceView(m_deferredTargets[i]->resource, nullptr, m_handles[DescHeapOffset::GBuffer + i].cpuHandle);
        }
    }

    // Depth/Stencil SRV
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC dsDesc;
        ZeroMemory(&dsDesc, sizeof(dsDesc));
        dsDesc.Texture2D.MipLevels = 1;
        dsDesc.Texture2D.MostDetailedMip = 0;
        dsDesc.Format = DXGI_FORMAT_R32_FLOAT;
        dsDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        dsDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

        m_Awesome->Device()->CreateShaderResourceView(m_Awesome->GetDepthStencilBuffer(), &dsDesc, m_handles[DescHeapOffset::Depth].cpuHandle);
    }

    // Cast Shadows
    SetCastShadow();

    // Screen Space Shadows
    {
        m_Awesome->Device()->CreateShaderResourceView(m_Awesome->GetSSShadows()->GetOutputBuffer(), nullptr, m_handles[DescHeapOffset::SSShadow].cpuHandle);
    }

    uint32 ibl = m_Awesome->GetIBLSystem()->GetCurrentIBL();
    SetIBL(ibl);

    // Split Sum
    {
        m_Awesome->Device()->CreateShaderResourceView(m_Awesome->GetIBLSystem()->GetSplitSum(), nullptr, m_handles[DescHeapOffset::BRDF].cpuHandle);
    }

    // Lighting Tiles    
    D3D12_SHADER_RESOURCE_VIEW_DESC lbdesc = {};
    lbdesc.Format = DXGI_FORMAT_UNKNOWN;
    lbdesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    lbdesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    lbdesc.Buffer.NumElements = m_Awesome->GetTiledLights()->GetBVHCount();
    lbdesc.Buffer.StructureByteStride = sizeof(GPULight);
    lbdesc.Buffer.FirstElement = 0;
    lbdesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

    m_Awesome->Device()->CreateShaderResourceView(m_Awesome->GetTiledLights()->GetBVHBuffer(), &lbdesc, m_handles[DescHeapOffset::LightBVH].cpuHandle);
    
    uint32 tilesX = 1 + ((m_Awesome->GetWidth() - 1) / 16);
    uint32 tilesY = 1 + ((m_Awesome->GetHeight() - 1) / 16);

    D3D12_SHADER_RESOURCE_VIEW_DESC ltdesc = {};
    ltdesc.Format = DXGI_FORMAT_UNKNOWN;
    ltdesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    ltdesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    ltdesc.Buffer.NumElements = tilesX * tilesY;
    ltdesc.Buffer.StructureByteStride = sizeof(LightTileData);
    lbdesc.Buffer.FirstElement = 0;
    lbdesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

    m_Awesome->Device()->CreateShaderResourceView(m_Awesome->GetTiledLights()->GetTileBuffer(), &ltdesc, m_handles[DescHeapOffset::LightTiles].cpuHandle);
    
    // GTAO
    {
        m_Awesome->Device()->CreateShaderResourceView(m_Awesome->GetGTAO()->GetOutputBuffer(), nullptr, m_handles[DescHeapOffset::GTAO].cpuHandle);
    }
    
    // Deferred Output Buffer
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
        desc.Format = c_deferredFormats[(uint32)DeferredBuffers::Albedo];

        ResourceDesc rDesc = {};
        rDesc.desc = desc;
        rDesc.state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        m_deferredOutput = m_Awesome->GetResourcePool()->Allocate(rDesc, L"Deferred Output Buffer", D3D12_HEAP_TYPE_DEFAULT);
    }

    // Output UAV
    {
        m_Awesome->Device()->CreateUnorderedAccessView(m_deferredOutput->resource, nullptr, nullptr, m_handles[DescHeapOffset::UAV].cpuHandle);
    }
}

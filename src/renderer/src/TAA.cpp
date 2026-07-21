#include "TAA.h"
#include "Util.h"
#include "Scene.h"
#include "Compute.h"
#include "Material.h"
#include "Deferred.h"
#include "PostFX.h"
#include "Resource.h"
#include "ResourcePool.h"
#include "DescriptorHeap.h" // Added missing include

using namespace Awesome;

TemporalAntiAliasing::TemporalAntiAliasing(AwesomeGraphics* Awesome)
	: m_Awesome(Awesome)
	, m_rootSignature(nullptr)
    , m_taaConstantBuffer(nullptr)
	, m_taaOutput(nullptr)
    , m_taaAccumulation(nullptr)
    , m_taaPrevMotion(nullptr)
	, m_taaComputePSO(invalidIndex32)
    , m_taaQuadPSO(invalidIndex32)
    , m_frameCounter(0)
    , m_jitterCache{}
    , m_handles{}
    , m_useCompute(true)
{
}

TemporalAntiAliasing::~TemporalAntiAliasing()
{
}

bool TemporalAntiAliasing::StartUp()
{
    SetupJitterCache();
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
        descriptorTableRanges[1].NumDescriptors = 5;
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

        if (FAILED(m_Awesome->Device()->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature))))
            return false;
    }

    std::vector<D3D_SHADER_MACRO*> permuations;
    D3D_SHADER_MACRO* defines = new D3D_SHADER_MACRO[1];
    defines[0] = { NULL, NULL };
    permuations.push_back(defines);

    uint32 shader = m_Awesome->GetComputeSystem()->CompileShader(L"TAA-c", permuations);
    m_taaComputePSO = m_Awesome->GetComputeSystem()->CreatePipeline(shader, m_rootSignature);

	return true;
}

bool TemporalAntiAliasing::TearDown()
{
    FreeResources(); // Ensure handles are freed
    SafeRelease(m_rootSignature);
	return true;
}

void TemporalAntiAliasing::Resize()
{
    m_Awesome->GetResourcePool()->Free(m_taaAccumulation);
    m_Awesome->GetResourcePool()->Free(m_taaPrevMotion);
    m_Awesome->GetResourcePool()->Free(m_taaOutput); // Also resize output
    SetupJitterCache();
    InitResources();
}

void TemporalAntiAliasing::Render(float delta)
{
    PIXScopedEvent(m_Awesome->GetCommandList(), 0, "TAA");
    m_frameCounter++;
    m_frameCounter = m_frameCounter % c_taaSampleCount;

    SetupHeapViews();

    {
        PIXScopedEvent(m_Awesome->GetCommandList(), 0, "TAA Resolve");
        
        // TRANSITION: Input is now Deferred Output (HDR Lighting)
        m_Awesome->TransitionResource(m_Awesome->GetDeferredRenderer()->GetOutputBuffer(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        
        m_Awesome->TransitionResource(m_Awesome->GetDeferredRenderer()->GetNormalMotionBuffer(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        m_Awesome->TransitionResource(m_Awesome->GetDepthStencilBuffer(), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        
        // Output needs to be UAV
        m_Awesome->TransitionResource(m_taaOutput->resource, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        m_Awesome->GetCommandList()->SetComputeRootSignature(m_rootSignature);

        m_Awesome->GetComputeSystem()->SetPSO(m_taaComputePSO);

        TAAConstants constants = {};
        constants.screenSize = {
            1.0f / float(m_Awesome->GetWidth()),
            1.0f/ float(m_Awesome->GetHeight()),
            float(m_Awesome->GetWidth()),
            float(m_Awesome->GetHeight()) };

        // Sky pixels have no geometry motion vector; give TAA the matrices to
        // reproject depth==0 pixels by camera motion (P5.3).
        Camera* taaCam = m_Awesome->GetCurrentScene()->GetCamera();
        XMMATRIX vp = taaCam->GetViewProjectionSpaceMatrix();
        XMStoreFloat4x4(&constants.invViewProj, XMMatrixInverse(nullptr, vp));
        XMStoreFloat4x4(&constants.prevViewProj, taaCam->prevViewProjMatrix);

        void* mapped = nullptr;
        m_taaConstantBuffer->resource->Map(0, nullptr, &mapped);
        memcpy(mapped, &constants, sizeof(TAAConstants));
        m_taaConstantBuffer->resource->Unmap(0, nullptr);

        m_Awesome->GetCommandList()->SetComputeRootDescriptorTable(0, m_handles[0].gpuHandle);

        uint32 dispatchX = 1 + ((m_Awesome->GetWidth() -1) / c_compute_thread_x);
        uint32 dispatchY = 1 + ((m_Awesome->GetHeight()- 1)/ c_compute_thread_y);

        m_Awesome->GetCommandList()->Dispatch(dispatchX, dispatchY, 1);
        
        m_Awesome->TransitionResource(m_Awesome->GetDepthStencilBuffer(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);
        
        // Transition Input back to UAV (Deferred Renderer expects it as UAV usually, or we leave it as SRV for PostFX)
        // Deferred Output is left as SRV for PostFX to read next.
    }

    {
        PIXScopedEvent(m_Awesome->GetCommandList(), 0, "TAA Finaly Copies");
        m_Awesome->TransitionResource(m_taaOutput->resource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
        m_Awesome->TransitionResource(m_taaAccumulation->resource, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
        m_Awesome->TransitionResource(m_taaPrevMotion->resource, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
        m_Awesome->TransitionResource(m_Awesome->GetDeferredRenderer()->GetNormalMotionBuffer(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE);
        {
            PIXScopedEvent(m_Awesome->GetCommandList(), 0, "TAA Copy Output To Accum");

            D3D12_TEXTURE_COPY_LOCATION destination = {};
            destination.pResource = m_taaAccumulation->resource;
            destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            destination.SubresourceIndex = 0;

            D3D12_TEXTURE_COPY_LOCATION source = {};
            source.pResource = m_taaOutput->resource;
            source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            source.SubresourceIndex = 0;

            m_Awesome->GetCommandList()->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
        }

        {
            PIXScopedEvent(m_Awesome->GetCommandList(), 0, "TAA Velocity to History");

            D3D12_TEXTURE_COPY_LOCATION destination = {};
            destination.pResource = m_taaPrevMotion->resource;
            destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            destination.SubresourceIndex = 0;

            D3D12_TEXTURE_COPY_LOCATION source = {};
            source.pResource = m_Awesome->GetDeferredRenderer()->GetNormalMotionBuffer();
            source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            source.SubresourceIndex = 0;

            m_Awesome->GetCommandList()->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
        }
        m_Awesome->TransitionResource(m_taaAccumulation->resource, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        m_Awesome->TransitionResource(m_taaPrevMotion->resource, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        
        // Output ready for PostFX to read
        m_Awesome->TransitionResource(m_taaOutput->resource, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        
        m_Awesome->TransitionResource(m_Awesome->GetDeferredRenderer()->GetNormalMotionBuffer(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
    }
    FreeResources();
}

XMFLOAT4X4 TemporalAntiAliasing::GetFrameJitter()
{
    float jx = m_jitterCache[m_frameCounter].x;
    float jy = m_jitterCache[m_frameCounter].y;
    XMFLOAT4X4 jitterMatrix = {
    1,  0,  0,  0,
    0,  1,  0,  0,
    0,  0,  1,  0,
    jx, jy, 0,  1,
    };
    return jitterMatrix;
}

ID3D12Resource* TemporalAntiAliasing::GetOutput() { return m_taaOutput->resource; }

void TemporalAntiAliasing::SetupJitterCache()
{
    // Jitter Cache
    for (uint32 i = 0; i < c_taaSampleCount; ++i)
    {
        float haltonX = Halton(i + 1, 2) * 2.0f - 1.0f;
        float haltonY = Halton(i + 1, 3) * 2.0f - 1.0f;
        float jitterX = haltonX / (m_Awesome->GetWidth());
        float jitterY = haltonY / (m_Awesome->GetHeight());
        m_jitterCache[i] = XMFLOAT2(jitterX, jitterY);
    }
}

void TemporalAntiAliasing::InitResources()
{
    // HDR Format (R16G16B16A16_FLOAT or R11G11B10_FLOAT)
    // We use R16G16B16A16_FLOAT for precision in accumulation
    DXGI_FORMAT hdrFormat = DXGI_FORMAT_R16G16B16A16_FLOAT; 

    // TAA Acculumation Buffer
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
        desc.Format = hdrFormat; 

        ResourceDesc rDesc = {};
        rDesc.desc = desc;
        rDesc.state = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        m_taaAccumulation = m_Awesome->GetResourcePool()->Allocate(rDesc, L"TAA Accumulation Buffer", D3D12_HEAP_TYPE_DEFAULT);
    }

    // Velocity History
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
        desc.Format = c_deferredFormats[uint32(DeferredBuffers::Normal)];

        ResourceDesc rDesc = {};
        rDesc.desc = desc;
        rDesc.state = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        m_taaPrevMotion = m_Awesome->GetResourcePool()->Allocate(rDesc, L"TAA Velocity History Buffer", D3D12_HEAP_TYPE_DEFAULT);
    }
    
    // TAA Output Buffer (Allocated here to ensure it exists for SetupHeapViews)
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
        desc.Format = hdrFormat;

        ResourceDesc rDesc = {};
        rDesc.desc = desc;
        rDesc.state = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE; // Start as SRV for PostFX
        m_taaOutput = m_Awesome->GetResourcePool()->Allocate(rDesc, L"TAA Output Buffer", D3D12_HEAP_TYPE_DEFAULT);
    }
}

void TemporalAntiAliasing::SetupHeapViews()
{
    m_Awesome->GetMainDescHeap()->AllocateBlock(m_handles, 7);
    // Constant Buffer
    {
        D3D12_RESOURCE_DESC resDesc = {};
        resDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        resDesc.Alignment = 0;
        resDesc.Width = TAAConstantsAlignedSize;
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
        m_taaConstantBuffer = m_Awesome->GetResourcePool()->Allocate(rDesc, L"TAA Constant Buffer", D3D12_HEAP_TYPE_UPLOAD);
    }

    // Constant Bufer View
    {
        D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
        cbvDesc.BufferLocation = m_taaConstantBuffer->resource->GetGPUVirtualAddress();
        cbvDesc.SizeInBytes = TAAConstantsAlignedSize;


        m_Awesome->Device()->CreateConstantBufferView(&cbvDesc, m_handles[0].cpuHandle);
    }

    // Shader Resource Views
    {
        // [CHANGE] Input: Deferred Renderer Output (HDR) instead of PostFX
        m_Awesome->Device()->CreateShaderResourceView(m_Awesome->GetDeferredRenderer()->GetOutputBuffer(), nullptr, m_handles[1].cpuHandle);

        m_Awesome->Device()->CreateShaderResourceView(m_taaAccumulation->resource, nullptr, m_handles[2].cpuHandle);

        m_Awesome->Device()->CreateShaderResourceView(m_Awesome->GetDeferredRenderer()->GetNormalMotionBuffer(), nullptr, m_handles[3].cpuHandle);

        m_Awesome->Device()->CreateShaderResourceView(m_taaPrevMotion->resource, nullptr, m_handles[4].cpuHandle);

        D3D12_SHADER_RESOURCE_VIEW_DESC dsDesc;
        ZeroMemory(&dsDesc, sizeof(dsDesc));
        dsDesc.Texture2D.MipLevels = 1;
        dsDesc.Texture2D.MostDetailedMip = 0;
        dsDesc.Format = DXGI_FORMAT_R32_FLOAT;
        dsDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        dsDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        m_Awesome->Device()->CreateShaderResourceView(m_Awesome->GetDepthStencilBuffer(), &dsDesc, m_handles[5].cpuHandle);
    }

    // UAV Output
    {
        m_Awesome->Device()->CreateUnorderedAccessView(m_taaOutput->resource, nullptr, nullptr, m_handles[6].cpuHandle);
    }
}

void TemporalAntiAliasing::FreeResources()
{
    m_Awesome->GetResourcePool()->Free(m_taaConstantBuffer);
    
    // NOTE: Do NOT free m_taaOutput here if it needs to persist for PostFX to read it.
    // However, since we allocate it in InitResources (Persistent), we don't Free it here.
    // The previous code freed it, but it was per-frame. We made it persistent in InitResources.
    
    for (auto& handle : m_handles)
        m_Awesome->GetMainDescHeap()->Free(handle);
    m_handles.clear();
}

float TemporalAntiAliasing::Halton(uint32 i, uint32 b)
{
    float f = 1.0f;
    float r = 0.0f;

    while (i > 0)
    {
        f /= static_cast<float>(b);
        r = r + f * static_cast<float>(i % b);
        i = static_cast<uint32_t>(floorf(static_cast<float>(i) / static_cast<float>(b)));
    }

    return r;
}
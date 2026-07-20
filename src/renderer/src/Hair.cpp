#include "Hair.h"
#include "Util.h"
#include "Compute.h"
#include "Material.h"
#include "Deferred.h"
#include "Scene.h"
#include "TAA.h"
#include "ResourcePool.h"

using namespace Awesome;

HairSystem::HairSystem(AwesomeGraphics* Awesome)
    : m_Awesome(Awesome)
    , m_rootSignature(nullptr)
    , m_hairConstantBuffer(nullptr)
    , m_hairOutput(nullptr)
    , m_hairComputePSO(invalidIndex32)
{
    m_hairData.reserve(20);
    m_hairInstances.reserve(20);
}

HairSystem::~HairSystem()
{
}

bool HairSystem::StartUp()
{
    /*
    // Constant Buffer & View
    {
        D3D12_RESOURCE_DESC resDesc = {};
        resDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        resDesc.Alignment = 0;
        resDesc.Width = HairConstantsAlignedSize;
        resDesc.Height = 1;
        resDesc.DepthOrArraySize = 1;
        resDesc.MipLevels = 1;
        resDesc.Format = DXGI_FORMAT_UNKNOWN;
        resDesc.SampleDesc.Count = 1;
        resDesc.SampleDesc.Quality = 0;
        resDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        resDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

        wchar_t name[256];
        swprintf(name, 256, L"Hair System Constant Buffer");
        m_hairConstantBuffer = m_Awesome->CreateBuffer(resDesc, name, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);


        D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
        cbvDesc.BufferLocation = m_hairConstantBuffer->GetGPUVirtualAddress();
        cbvDesc.SizeInBytes = HairConstantsAlignedSize;

        //auto handle = m_hairCbvSrvUavDescHeap->GetCPUDescriptorHandleForHeapStart();

        //m_Awesome->Device()->CreateConstantBufferView(&cbvDesc, handle);
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
        descriptorTableRanges[1].NumDescriptors = 4;
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

    uint32 shader = m_Awesome->GetComputeSystem()->CompileShader(L"HairRenderer-c", permuations);
    m_hairComputePSO = m_Awesome->GetComputeSystem()->CreatePipeline(shader, m_rootSignature);
    */
    return true;
}

bool HairSystem::TearDown()
{
    /*
    for (uint32 i = 0; i < m_hairInstances.size(); ++i)
    {
        SafeRelease(m_hairInstances[i].m_pointsBuffer);
        SafeRelease(m_hairInstances[i].m_strandsBuffer);
    }
    m_hairInstances.clear();
    m_hairData.clear();
    SafeRelease(m_rootSignature);
    SafeRelease(m_hairConstantBuffer);
    SafeRelease(m_hairOutput);
    */
    return true;
}

void HairSystem::Resize()
{

}

void HairSystem::Render(float delta)
{
    /*
    PIXScopedEvent(m_Awesome->GetCommandList(), 0, "Hair System");
    {
        PIXScopedEvent(m_Awesome->GetCommandList(), 0, "Hair System Render");
        m_Awesome->TransitionResource(m_Awesome->GetDepthStencilBuffer(), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

        m_Awesome->GetCommandList()->SetComputeRootSignature(m_rootSignature);

        m_Awesome->GetComputeSystem()->SetPSO(m_hairComputePSO);



        {
            PIXScopedEvent(m_Awesome->GetCommandList(), 0, "Hair System Copy Output To Frame");
            m_Awesome->TransitionResource(m_Awesome->GetTAA()->GetOutput(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
            m_Awesome->TransitionResource(m_hairOutput->resource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST);
            D3D12_TEXTURE_COPY_LOCATION destination = {};
            destination.pResource = m_hairOutput->resource;
            destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            destination.SubresourceIndex = 0;

            D3D12_TEXTURE_COPY_LOCATION source = {};
            source.pResource = m_Awesome->GetTAA()->GetOutput();
            source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            destination.SubresourceIndex = 0;

            m_Awesome->GetCommandList()->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
            m_Awesome->TransitionResource(m_Awesome->GetTAA()->GetOutput(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            m_Awesome->TransitionResource(m_hairOutput->resource, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        }



        HairConstants constants = {};

        XMMATRIX projMat = m_Awesome->GetCurrentScene()->GetCamera()->GetProjectionMatrix();
        XMMATRIX inProjMat = XMMatrixInverse(nullptr, projMat);
        // XMMATRIX t_invProjMat = XMMatrixTranspose(inProjMat);
        XMStoreFloat4x4(&constants.invProj, inProjMat);

        XMMATRIX viewMat = m_Awesome->GetCurrentScene()->GetCamera()->GetViewSpaceMatrix();
        XMMATRIX invViewMat = XMMatrixInverse(nullptr, viewMat);
        // XMMATRIX t_invViewMat = XMMatrixTranspose(invViewMat);
        XMStoreFloat4x4(&constants.invView, invViewMat);

        XMMATRIX viewProj = viewMat * projMat;
        // XMMATRIX t_wvp = XMMatrixTranspose(viewProj);

        XMStoreFloat4x4(&constants.worldViewProj, viewProj);

        constants.cameraPos = {
            m_Awesome->GetCurrentScene()->GetCamera()->transform.position.x,
            m_Awesome->GetCurrentScene()->GetCamera()->transform.position.y,
            m_Awesome->GetCurrentScene()->GetCamera()->transform.position.z,
            1.f
        };

        constants.screenSize = {
            1.0f / float(m_Awesome->GetWidth()),
            1.0f / float(m_Awesome->GetHeight()),
            float(m_Awesome->GetWidth()),
            float(m_Awesome->GetHeight()) };



        uint32 offset = m_Awesome->Device()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        auto handle = m_hairCbvSrvUavDescHeap->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += offset; // CBV
        // Shader Resource Views
        {
            m_Awesome->Device()->CreateShaderResourceView(m_Awesome->GetTAA()->GetOutput(), nullptr, handle);
            handle.ptr += offset;

            D3D12_SHADER_RESOURCE_VIEW_DESC dsDesc;
            ZeroMemory(&dsDesc, sizeof(dsDesc));
            dsDesc.Texture2D.MipLevels = 1;
            dsDesc.Texture2D.MostDetailedMip = 0;
            dsDesc.Format = DXGI_FORMAT_R32_FLOAT;
            dsDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            dsDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

            m_Awesome->Device()->CreateShaderResourceView(m_Awesome->GetDepthStencilBuffer(), &dsDesc, handle);
            handle.ptr += offset;
        }

        for (uint32 i = 0; i < uint32(m_hairInstances.size()); ++i)
        {
            PIXScopedEvent(m_Awesome->GetCommandList(), 0, "Hair System Dispatch");
            {
                D3D12_SHADER_RESOURCE_VIEW_DESC desc;
                ZeroMemory(&desc, sizeof(desc));
                desc.Buffer.FirstElement = i;
                desc.Buffer.NumElements = m_hairInstances[i].m_pointCount;
                desc.Buffer.StructureByteStride = sizeof(XMFLOAT4);
                desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
                desc.Format = DXGI_FORMAT_UNKNOWN;
                desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
                desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                m_Awesome->Device()->CreateShaderResourceView(m_hairInstances[i].m_pointsBuffer, &desc, handle);
                
            }
            handle.ptr += offset;
            {
                D3D12_SHADER_RESOURCE_VIEW_DESC desc;
                ZeroMemory(&desc, sizeof(desc));
                desc.Buffer.FirstElement = i;
                desc.Buffer.NumElements = m_hairInstances[i].m_strandCount;
                desc.Buffer.StructureByteStride = sizeof(XMUINT2);
                desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
                desc.Format = DXGI_FORMAT_UNKNOWN;
                desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
                desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                m_Awesome->Device()->CreateShaderResourceView(m_hairInstances[i].m_strandsBuffer, &desc, handle);

                
            }
            handle.ptr += offset;
            m_Awesome->Device()->CreateUnorderedAccessView(m_hairOutput->resource, nullptr, nullptr, handle);


            constants.standCount = m_hairInstances[i].m_strandCount;
            void* mapped = nullptr;
            m_hairConstantBuffer->Map(0, nullptr, &mapped);
            memcpy(mapped, &constants, sizeof(HairConstants));
            m_hairConstantBuffer->Unmap(0, nullptr);

            m_Awesome->GetCommandList()->SetComputeRootDescriptorTable(0, m_hairCbvSrvUavDescHeap->GetGPUDescriptorHandleForHeapStart());

            uint32 dispatchX = 1 + ((m_hairInstances[i].m_strandCount - 1) / 8);
            uint32 dispatchY = 1 + ((m_Awesome->GetHeight() - 1) / 8);

            m_Awesome->GetCommandList()->Dispatch(dispatchX, 1, 1);
        }


        m_Awesome->TransitionResource(m_hairOutput, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
        m_Awesome->TransitionResource(m_Awesome->GetCurrentRenderTarget(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_DEST);

        {
            PIXScopedEvent(m_Awesome->GetCommandList(), 0, "Hair System Copy Output To Frame");
            D3D12_TEXTURE_COPY_LOCATION destination = {};
            destination.pResource = m_Awesome->GetCurrentRenderTarget();
            destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            destination.SubresourceIndex = 0;

            D3D12_TEXTURE_COPY_LOCATION source = {};
            source.pResource = m_hairOutput;
            source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            source.SubresourceIndex = 0;

            m_Awesome->GetCommandList()->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
        }

        m_Awesome->TransitionResource(m_Awesome->GetTAA()->GetOutput(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        m_Awesome->TransitionResource(m_Awesome->GetDepthStencilBuffer(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);
        m_Awesome->TransitionResource(m_Awesome->GetCurrentRenderTarget(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_RENDER_TARGET);
        m_Awesome->TransitionResource(m_hairOutput->resource, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);


    }
    */
}

ID3D12Resource* HairSystem::GetOutputBuffer()
{
    return m_hairOutput->resource;
}

uint32 HairSystem::CreateHair(HairData& inHair)
{

    m_hairInstances.emplace_back();
    Hair& h = m_hairInstances.back();
    h.m_data = uint32(m_hairData.size());
    m_hairData.emplace_back(inHair);
    DebugPrint("Creating Hair %d.\n", m_hairData.size());


    m_Awesome->BeginFrame(false);
    // Point Buffer
    uint32 pBufferSize = uint32(inHair.points.size()) * sizeof(XMFLOAT4);
    {
        D3D12_RESOURCE_DESC resDesc = {};
        resDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        resDesc.Alignment = 0;
        resDesc.Width = pBufferSize;
        resDesc.Height = 1;
        resDesc.DepthOrArraySize = 1;
        resDesc.MipLevels = 1;
        resDesc.Format = DXGI_FORMAT_UNKNOWN;
        resDesc.SampleDesc.Count = 1;
        resDesc.SampleDesc.Quality = 0;
        resDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        resDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

        wchar_t name[256];
        swprintf(name, 256, L"Awesome Hair %zd Point Buffer", m_hairData.size());

        h.m_pointsBuffer = m_Awesome->CreateBuffer(resDesc, name, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COMMON);

        const uint8* srcData = reinterpret_cast<uint8*>(&inHair.points.front());
        m_Awesome->UploadBuffer(h.m_pointsBuffer, resDesc, pBufferSize, srcData);

        m_Awesome->TransitionResource(h.m_pointsBuffer, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    }

    // Strand Buffer
    uint32 strandBbufferSize = uint32(inHair.strands.size()) * sizeof(XMUINT2);
    {
        D3D12_RESOURCE_DESC resDesc = {};
        resDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        resDesc.Alignment = 0;
        resDesc.Width = strandBbufferSize;
        resDesc.Height = 1;
        resDesc.DepthOrArraySize = 1;
        resDesc.MipLevels = 1;
        resDesc.Format = DXGI_FORMAT_UNKNOWN;
        resDesc.SampleDesc.Count = 1;
        resDesc.SampleDesc.Quality = 0;
        resDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        resDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

        wchar_t name[256];
        swprintf(name, 256, L"Awesome Hair %zd Strands Buffer", m_hairData.size());
        h.m_strandsBuffer = m_Awesome->CreateBuffer(resDesc, name, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COMMON);

        const uint8* srcData = reinterpret_cast<uint8*>(&inHair.strands.front());
        m_Awesome->UploadBuffer(h.m_strandsBuffer, resDesc, strandBbufferSize, srcData);
        m_Awesome->TransitionResource(h.m_strandsBuffer, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }

    m_Awesome->EndFrame(false);

    h.m_pointCount = uint32(inHair.points.size());
    h.m_strandCount = uint32(inHair.strands.size());
    h.m_materialIndex = inHair.materialIndex;

    return h.m_data;
}

void HairSystem::InitResources()
{
/*
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
        desc.Format = m_Awesome->GetSwapChainFormat();

        m_hairOutput = m_Awesome->CreateBuffer(desc, L"Hair System Output Buffer", D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr);
    
    }

    uint32 offset = m_Awesome->Device()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    auto handle = m_hairCbvSrvUavDescHeap->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += offset; // CBV
    // Shader Resource Views
    {
        m_Awesome->Device()->CreateShaderResourceView(m_Awesome->GetTAA()->GetOutput(), nullptr, handle);
        handle.ptr += offset;

        D3D12_SHADER_RESOURCE_VIEW_DESC dsDesc;
        ZeroMemory(&dsDesc, sizeof(dsDesc));
        dsDesc.Texture2D.MipLevels = 1;
        dsDesc.Texture2D.MostDetailedMip = 0;
        dsDesc.Format = DXGI_FORMAT_R32_FLOAT;
        dsDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        dsDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;       
        
        m_Awesome->Device()->CreateShaderResourceView(m_Awesome->GetDepthStencilBuffer(), &dsDesc, handle);
        handle.ptr += offset;
    }

    // Output UAV
    {
        handle.ptr += offset * 2;
        m_Awesome->Device()->CreateUnorderedAccessView(m_hairOutput->resource, nullptr, nullptr, handle);
    }
    */
}

void HairSystem::FreeResources()
{
    m_Awesome->GetResourcePool()->Free(m_hairOutput);
    m_Awesome->GetResourcePool()->Free(m_hairConstantBuffer);

}

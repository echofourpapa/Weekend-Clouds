#include "MipGenerator.h"
#include "Util.h"
#include "Compute.h"

#include "renderdoc_app.h"

//RENDERDOC_API_1_6_0* rdoc_api = NULL;

using namespace Awesome;

MipGenerator::MipGenerator(AwesomeGraphics* Awesome)
    : m_Awesome(Awesome)
    , m_rootSignature(nullptr)
    , m_mgCbvSrvUavDescHeap(nullptr)
    , m_mgConstantBuffer(nullptr)
    , m_mgComputePSO(invalidIndex32)
    , m_mgCubeComputePSO(invalidIndex32)
{
    //if (HMODULE mod = GetModuleHandleA("renderdoc.dll"))
    //{
    //    pRENDERDOC_GetAPI RENDERDOC_GetAPI =
    //        (pRENDERDOC_GetAPI)GetProcAddress(mod, "RENDERDOC_GetAPI");
    //    int ret = RENDERDOC_GetAPI(eRENDERDOC_API_Version_1_6_0, (void**)&rdoc_api);
    //    assert(ret == 1);
    //}
}

MipGenerator::~MipGenerator()
{
}

bool MipGenerator::StartUp()
{
    // CBV/SRV/UAV Descriptor Heap
    {
        D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
        heapDesc.NumDescriptors = 64;
        heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        if (FAILED(m_Awesome->Device()->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_mgCbvSrvUavDescHeap))))
            return false;
        m_mgCbvSrvUavDescHeap->SetName(L"Mip Generator CBV/SRV/UAV Descriptor Heap");
    }
    // Constant Buffer & View
    {
        D3D12_RESOURCE_DESC resDesc = {};
        resDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        resDesc.Alignment = 0;
        resDesc.Width = TAAConstantsAlignedSize * 32;
        resDesc.Height = 1;
        resDesc.DepthOrArraySize = 1;
        resDesc.MipLevels = 1;
        resDesc.Format = DXGI_FORMAT_UNKNOWN;
        resDesc.SampleDesc.Count = 1;
        resDesc.SampleDesc.Quality = 0;
        resDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        resDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

        m_mgConstantBuffer = m_Awesome->CreateBuffer(resDesc, L"IMip Generator Constant Buffer", D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);

        D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
        cbvDesc.BufferLocation = m_mgConstantBuffer->GetGPUVirtualAddress();
        cbvDesc.SizeInBytes = TAAConstantsAlignedSize;

        auto handle = m_mgCbvSrvUavDescHeap->GetCPUDescriptorHandleForHeapStart();

        m_Awesome->Device()->CreateConstantBufferView(&cbvDesc, handle);
    }
    // Create Root Signature
    {
        D3D12_DESCRIPTOR_RANGE  descriptorTableRanges[2];

        descriptorTableRanges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        descriptorTableRanges[0].NumDescriptors = 1;
        descriptorTableRanges[0].BaseShaderRegister = 0;
        descriptorTableRanges[0].RegisterSpace = 0;
        descriptorTableRanges[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        descriptorTableRanges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        descriptorTableRanges[1].NumDescriptors = 1;
        descriptorTableRanges[1].BaseShaderRegister = 0;
        descriptorTableRanges[1].RegisterSpace = 0;
        descriptorTableRanges[1].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        // create a descriptor table
        D3D12_ROOT_DESCRIPTOR_TABLE descriptorTable;
        descriptorTable.NumDescriptorRanges = _countof(descriptorTableRanges);
        descriptorTable.pDescriptorRanges = descriptorTableRanges;

        D3D12_ROOT_PARAMETER  rootParameters[2];
        rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        rootParameters[0].Descriptor = { 0,0 };
        rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rootParameters[1].DescriptorTable = descriptorTable;
        rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        D3D12_ROOT_SIGNATURE_DESC desc = {};
        desc.NumParameters = _countof(rootParameters);
        desc.pParameters = rootParameters;
        desc.NumStaticSamplers = _countof(c_computeSamplers);
        desc.pStaticSamplers = c_computeSamplers;
        desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

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

    {
        std::vector<D3D_SHADER_MACRO*> permuations;
        D3D_SHADER_MACRO* defines = new D3D_SHADER_MACRO[1];
        defines[0] = { NULL, NULL };
        permuations.push_back(defines);

        uint32 shader = m_Awesome->GetComputeSystem()->CompileShader(L"MipGenerator-c", permuations);
        m_mgComputePSO = m_Awesome->GetComputeSystem()->CreatePipeline(shader, m_rootSignature);
    }

    {
        std::vector<D3D_SHADER_MACRO*> permuations;
        D3D_SHADER_MACRO* defines = new D3D_SHADER_MACRO[1];
        defines[0] = { NULL, NULL };
        permuations.push_back(defines);

        uint32 shader = m_Awesome->GetComputeSystem()->CompileShader(L"MipGeneratorCube-c", permuations);
        m_mgCubeComputePSO = m_Awesome->GetComputeSystem()->CreatePipeline(shader, m_rootSignature);
    }
    
    return true;
}

bool MipGenerator::TearDown()
{
    SafeRelease(m_rootSignature);
    SafeRelease(m_mgCbvSrvUavDescHeap);
    SafeRelease(m_mgConstantBuffer);
    return true;
}

void MipGenerator::GenerateMips(ID3D12Resource* texture, D3D12_RESOURCE_STATES state)
{
    D3D12_RESOURCE_DESC srcDesc = texture->GetDesc();
    bool isCubeMap = srcDesc.DepthOrArraySize > 1;
    uint32 mipCount = srcDesc.MipLevels;

    D3D12_RESOURCE_DESC dstDesc = {};
    dstDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    dstDesc.Alignment = srcDesc.Alignment;
    dstDesc.Width = srcDesc.Width;
    dstDesc.Height = srcDesc.Height;
    dstDesc.DepthOrArraySize = srcDesc.DepthOrArraySize;
    dstDesc.MipLevels = mipCount;
    dstDesc.Format = srcDesc.Format;
    dstDesc.SampleDesc.Count = srcDesc.SampleDesc.Count;
    dstDesc.SampleDesc.Quality = srcDesc.SampleDesc.Quality;
    dstDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    dstDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    ID3D12Resource* mipOut = m_Awesome->CreateBuffer(dstDesc, L"Mip Output Buffer", D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST, nullptr);

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = isCubeMap ? D3D12_SRV_DIMENSION_TEXTURE2DARRAY : D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Format = srcDesc.Format;

    if (isCubeMap)
    {
        srvDesc.Texture2DArray.MipLevels = 1;
        srvDesc.Texture2DArray.ResourceMinLODClamp = 0.0f;
        srvDesc.Texture2DArray.ArraySize = srcDesc.DepthOrArraySize;
    }
    else  
    {
        srvDesc.Texture2D.MipLevels = 1;
        srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
    }

    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.ViewDimension = isCubeMap ? D3D12_UAV_DIMENSION_TEXTURE2DARRAY : D3D12_UAV_DIMENSION_TEXTURE2D;
    uavDesc.Format = srcDesc.Format;
    if (isCubeMap)
    {
        uavDesc.Texture2DArray.ArraySize = srcDesc.DepthOrArraySize;
    }

    //if (rdoc_api) rdoc_api->StartFrameCapture(NULL, NULL);
    m_Awesome->BeginFrame(false);
    {
        PIXScopedEvent(m_Awesome->GetCommandList(), 0, "Compute Mip Generation");

        // THis is for copying the first mip
        m_Awesome->TransitionResource(texture, state, D3D12_RESOURCE_STATE_COPY_SOURCE);
        {
            for (uint32 i = 0; i < srcDesc.DepthOrArraySize; ++i)
            {
                D3D12_TEXTURE_COPY_LOCATION destination = {};
                destination.pResource = mipOut;
                destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                destination.SubresourceIndex = i * mipCount;

                D3D12_TEXTURE_COPY_LOCATION source = {};
                source.pResource = texture;
                source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                source.SubresourceIndex = i * mipCount;

                m_Awesome->GetCommandList()->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
            }
        }
        m_Awesome->TransitionResource(mipOut, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        m_Awesome->GetCommandList()->SetComputeRootSignature(m_rootSignature);

        ID3D12DescriptorHeap* descriptorHeaps[] = { m_mgCbvSrvUavDescHeap };
        m_Awesome->GetCommandList()->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);
        m_Awesome->GetComputeSystem()->SetPSO(isCubeMap ? m_mgCubeComputePSO : m_mgComputePSO);
        void* mapped = nullptr;
        m_mgConstantBuffer->Map(0, nullptr, &mapped);

        uint32 offset = m_Awesome->Device()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        auto handle = m_mgCbvSrvUavDescHeap->GetCPUDescriptorHandleForHeapStart();
        auto gpuHandle = m_mgCbvSrvUavDescHeap->GetGPUDescriptorHandleForHeapStart();
        for (uint32 mip = 0; mip < mipCount-1; ++mip)
        {
            char msg[256];
            sprintf_s(msg, "Mip %d Generation ", mip);
            PIXScopedEvent(m_Awesome->GetCommandList(), 0, msg);
            uint32 width = std::max(uint32(srcDesc.Width) >> (mip+1), 1u);
            uint32 height = std::max(uint32(srcDesc.Height) >> (mip+1), 1u);

            if (isCubeMap)
            {
                srvDesc.Texture2DArray.MostDetailedMip = mip;
            }
            else
            {
                srvDesc.Texture2D.MostDetailedMip = mip;
            }
            // Input Mip
            {
                m_Awesome->Device()->CreateShaderResourceView(mipOut, &srvDesc, handle);
                handle.ptr += offset;
            }

            if (isCubeMap)
            {
                uavDesc.Texture2DArray.MipSlice = mip + 1;
            }
            else
            {
                uavDesc.Texture2D.MipSlice = mip + 1;
            }
            // Output Mip
            {
                m_Awesome->Device()->CreateUnorderedAccessView(mipOut, nullptr, &uavDesc, handle);
                handle.ptr += offset;
               
            }

            TAAConstants constants = {};
            constants.screenSize = {
                1.0f / float(width),
                1.0f / float(height),
                float(width),
                float(height) };
            uint32 moffset = TAAConstantsAlignedSize * mip;
            uint8* map = reinterpret_cast<uint8*>(mapped);
            memcpy(map + moffset, &constants, sizeof(TAAConstants));
            
            m_Awesome->GetCommandList()->SetComputeRootConstantBufferView(0, m_mgConstantBuffer->GetGPUVirtualAddress() + moffset);
            m_Awesome->GetCommandList()->SetComputeRootDescriptorTable(1, gpuHandle);
            gpuHandle.ptr += offset * 2;

            uint32 dispatchX = std::max(width / c_compute_thread_x, 1u);
            uint32 dispatchY = std::max(height / c_compute_thread_y, 1u);

            m_Awesome->GetCommandList()->Dispatch(dispatchX, dispatchY, 6);
            {
                D3D12_RESOURCE_BARRIER barrier = {};
                barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
                barrier.Transition.pResource = mipOut;
                m_Awesome->GetCommandList()->ResourceBarrier(1, &barrier);
            }
        }
        m_mgConstantBuffer->Unmap(0, nullptr);
        m_Awesome->TransitionResource(mipOut, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);

        m_Awesome->TransitionResource(texture, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
        {
            for (uint32 i = 0; i < mipCount * srcDesc.DepthOrArraySize; ++i)
            {
                D3D12_TEXTURE_COPY_LOCATION destination = {};
                destination.pResource = texture;
                destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                destination.SubresourceIndex = i;

                D3D12_TEXTURE_COPY_LOCATION source = {};
                source.pResource = mipOut;
                source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                source.SubresourceIndex = i;
                m_Awesome->GetCommandList()->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
            }
        }
        m_Awesome->TransitionResource(texture, D3D12_RESOURCE_STATE_COPY_DEST, state);

    }
    m_Awesome->EndFrame(false);
    //if (rdoc_api) rdoc_api->EndFrameCapture(NULL, NULL);
    SafeRelease(mipOut);
}

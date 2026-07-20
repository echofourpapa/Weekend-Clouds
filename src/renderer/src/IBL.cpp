#include "IBL.h"
#include "Util.h"
#include "Compute.h"
#include <filesystem>
#include "Texture.h"
#include "Deferred.h"
#include "renderdoc_app.h"
#include "pix3.h"

using namespace Awesome;

const uint32 c_iblSamples = 64;
const uint32 c_iblIrradianceSize = 64;

//RENDERDOC_API_1_6_0* rdoc_api = NULL;

IBLProcessor::IBLProcessor(AwesomeGraphics* Awesome)
	: m_Awesome(Awesome)
	, m_rootSignature(nullptr)
	, m_iblCbvSrvUavDescHeap(nullptr)
    , m_iblConstantBuffer(nullptr)
	, m_iblComputePSO(invalidIndex32)
    , m_iblPreFilterPSO(invalidIndex32)
    , m_iblIrradiancePSO(invalidIndex32)
    , m_splitSumComputePSO(invalidIndex32)
{
    //if (HMODULE mod = GetModuleHandleA("renderdoc.dll"))
    //{
    //    pRENDERDOC_GetAPI RENDERDOC_GetAPI = (pRENDERDOC_GetAPI)GetProcAddress(mod, "RENDERDOC_GetAPI");
    //    int ret = RENDERDOC_GetAPI(eRENDERDOC_API_Version_1_6_0, (void**)&rdoc_api);
    //    assert(ret == 1);
    //}
}

IBLProcessor::~IBLProcessor()
{
}

bool IBLProcessor::StartUp()
{

    // CBV/SRV/UAV Descriptor Heap
    {
        D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
        heapDesc.NumDescriptors = 64;
        heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        if (FAILED(m_Awesome->Device()->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_iblCbvSrvUavDescHeap))))
            return false;
        m_iblCbvSrvUavDescHeap->SetName(L"IBL Process CBV/SRV/UAV Descriptor Heap");
    }
    // Constant Buffer & View
    {
        D3D12_RESOURCE_DESC resDesc = {};
        resDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        resDesc.Alignment = 0;
        resDesc.Width = IBLConstantsAlignedSize * 32;
        resDesc.Height = 1;
        resDesc.DepthOrArraySize = 1;
        resDesc.MipLevels = 1;
        resDesc.Format = DXGI_FORMAT_UNKNOWN;
        resDesc.SampleDesc.Count = 1;
        resDesc.SampleDesc.Quality = 0;
        resDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        resDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

        m_iblConstantBuffer = m_Awesome->CreateBuffer(resDesc, L"IBL Process Constant Buffer", D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);

        D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
        cbvDesc.BufferLocation = m_iblConstantBuffer->GetGPUVirtualAddress();
        cbvDesc.SizeInBytes = IBLConstantsAlignedSize;

        auto handle = m_iblCbvSrvUavDescHeap->GetCPUDescriptorHandleForHeapStart();

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

    uint32 shader = m_Awesome->GetComputeSystem()->CompileShader(L"IBLProcess-c", permuations);
    m_iblComputePSO = m_Awesome->GetComputeSystem()->CreatePipeline(shader, m_rootSignature);

    shader = m_Awesome->GetComputeSystem()->CompileShader(L"IBLSpecFilter-c", permuations);
    m_iblPreFilterPSO = m_Awesome->GetComputeSystem()->CreatePipeline(shader, m_rootSignature);

    shader = m_Awesome->GetComputeSystem()->CompileShader(L"IBLIrradiance-c", permuations);
    m_iblIrradiancePSO = m_Awesome->GetComputeSystem()->CreatePipeline(shader, m_rootSignature);

    shader = m_Awesome->GetComputeSystem()->CompileShader(L"SplitSumGenerator-c", permuations);
    m_splitSumComputePSO = m_Awesome->GetComputeSystem()->CreatePipeline(shader, m_rootSignature);

	return true;
}

bool IBLProcessor::TearDown()
{
	SafeRelease(m_rootSignature);
	SafeRelease(m_iblCbvSrvUavDescHeap);
    SafeRelease(m_iblConstantBuffer);
	return true;
}

bool IBLProcessor::AddIBL(const char* path, IBLData& iblData)
{
    ImageData img = {};
    {
        std::string name = std::string(std::filesystem::path(path).filename().string().c_str());
        DebugPrint("Reading Image: %s\n", name.c_str());

        FILE* fp;
        fopen_s(&fp, path, "rb");
        if (!fp)
        {
            DebugPrint("Couldn't read file: %s\n", path);
            return invalidIndex32;
        }


        bool res = TextureSystem::ReadStb(fp, false, img);

        fclose(fp);
        if (!res)
            return false;
        img.name = name;
    }

    uint32 offset = m_Awesome->Device()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    auto handle = m_iblCbvSrvUavDescHeap->GetCPUDescriptorHandleForHeapStart();

    ID3D12Resource* iblInput = nullptr;
    ID3D12Resource* iblOutput = nullptr;
    ID3D12Resource* irrOutput = nullptr;

    m_Awesome->BeginFrame(false);
    {
        DebugPrint("Loading Image: %s\n", img.name.c_str());
        D3D12_RESOURCE_DESC tDesc = {};
        tDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        tDesc.Alignment = 0; // may be 0, 4KB, 64KB, or 4MB. 0 will let runtime decide between 64KB and 4MB (4MB for multi-sampled textures)
        tDesc.Width = img.width; // width of the texture
        tDesc.Height = img.height; // height of the texture
        tDesc.DepthOrArraySize = 1; // if 3d image, depth of 3d image. Otherwise an array of 1D or 2D textures (we only have one image, so we set 1)
        tDesc.MipLevels = img.mipCount; // Number of mipmaps. We are not generating mipmaps for this texture, so we have only one level
        tDesc.Format = img.format; // This is the dxgi format of the image (format of the pixels)
        tDesc.SampleDesc.Count = 1; // This is the number of samples per pixel, we just want 1 sample
        tDesc.SampleDesc.Quality = 0; // The quality level of the samples. Higher is better quality, but worse performance
        tDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN; // The arrangement of the pixels. Setting to unknown lets the driver choose the most efficient one
        tDesc.Flags = D3D12_RESOURCE_FLAG_NONE; // no flags

        wchar_t name[256];
        swprintf(name, 256, L"%S", img.name.c_str());
        iblInput = m_Awesome->CreateBuffer(tDesc, name, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST);

        m_Awesome->UploadTexture(iblInput, tDesc, 0, img);
        m_Awesome->TransitionResource(iblInput, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }
    m_Awesome->EndFrame(false);
    uint32 faceSize = img.height / 2;
    uint32 mipCount = 1 + uint32(floor(log2(faceSize)));
    // Radiance Output Buffer
    {
        
        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Alignment = 0;
        desc.Width = faceSize;
        desc.Height = faceSize;
        desc.DepthOrArraySize = 6;
        desc.MipLevels = mipCount;
        desc.SampleDesc.Count = 1;
        desc.SampleDesc.Quality = 0;
        desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        desc.Format = c_iblFormat;

        iblOutput = m_Awesome->CreateBuffer(desc, L"IBL Radiance Output Buffer", D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr);
    }

    // Irradiance Output Buffer
    {

        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Alignment = 0;
        desc.Width = c_iblIrradianceSize;
        desc.Height = c_iblIrradianceSize;
        desc.DepthOrArraySize = 6;
        desc.MipLevels = 1;
        desc.SampleDesc.Count = 1;
        desc.SampleDesc.Quality = 0;
        desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        desc.Format = c_iblFormat;

        irrOutput = m_Awesome->CreateBuffer(desc, L"IBL Irradiance Output Buffer", D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr);
    }

    // Spec IBL
    {
        m_Awesome->Device()->CreateShaderResourceView(iblInput, nullptr, handle);
    }

    // Output UAV
    {
        handle.ptr += offset;
        m_Awesome->Device()->CreateUnorderedAccessView(iblOutput, nullptr, nullptr, handle);
    }

    m_Awesome->BeginFrame(false);
    // Equirectangular to Cubemap
    {
        PIXScopedEvent(m_Awesome->GetCommandList(), 0, "IBL Process Lighting Dispatch");
        m_Awesome->GetCommandList()->SetComputeRootSignature(m_rootSignature);

        ID3D12DescriptorHeap* descriptorHeaps[] = { m_iblCbvSrvUavDescHeap };
        m_Awesome->GetCommandList()->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);
        m_Awesome->GetComputeSystem()->SetPSO(m_iblComputePSO);

        IBLConstants constants = {};

        constants.screenSize = {
            1.0f / float(img.width),
            1.0f / float(img.height),
            1.0f / float(faceSize),
            1.0f / float(faceSize) };

        constants.sampleCount = c_iblSamples;
        void* mapped = nullptr;
        m_iblConstantBuffer->Map(0, nullptr, &mapped);
        memcpy(mapped, &constants, sizeof(IBLConstants));
        m_iblConstantBuffer->Unmap(0, nullptr);

        m_Awesome->GetCommandList()->SetComputeRootConstantBufferView(0, m_iblConstantBuffer->GetGPUVirtualAddress());
        m_Awesome->GetCommandList()->SetComputeRootDescriptorTable(1, m_iblCbvSrvUavDescHeap->GetGPUDescriptorHandleForHeapStart());

        uint32 dispatchX = std::max(faceSize / c_compute_thread_x, 1u);
        uint32 dispatchY = std::max(faceSize / c_compute_thread_y, 1u);

        m_Awesome->GetCommandList()->Dispatch(dispatchX, dispatchY, 6);

        m_Awesome->TransitionResource(iblOutput, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);

        {
            D3D12_RESOURCE_DESC desc = {};
            desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            desc.Alignment = 0;
            desc.Width = faceSize;
            desc.Height = faceSize;
            desc.DepthOrArraySize = 6;
            desc.MipLevels = mipCount;
            desc.SampleDesc.Count = 1;
            desc.SampleDesc.Quality = 0;
            desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
            desc.Flags = D3D12_RESOURCE_FLAG_NONE;
            desc.Format = c_iblFormat;

            iblData.specProbe = m_Awesome->CreateBuffer(desc, L"IBL Radiance Buffer", D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST, nullptr);
        }
        iblData.mipCount = (float)mipCount;
        
        for (uint32 m = 0; m < mipCount; ++m)
        {
            for (uint32 i = 0; i < 6; ++i)
            {
                D3D12_TEXTURE_COPY_LOCATION destination = {};
                destination.pResource = iblData.specProbe;
                destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                destination.SubresourceIndex = m + (i * mipCount);

                D3D12_TEXTURE_COPY_LOCATION source = {};
                source.pResource = iblOutput;
                source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                source.SubresourceIndex = m + (i * mipCount);

                m_Awesome->GetCommandList()->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
            }
        }

        {
            D3D12_RESOURCE_DESC desc = {};
            desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            desc.Alignment = 0;
            desc.Width = c_iblIrradianceSize;
            desc.Height = c_iblIrradianceSize;
            desc.DepthOrArraySize = 6;
            desc.MipLevels = 1;
            desc.SampleDesc.Count = 1;
            desc.SampleDesc.Quality = 0;
            desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
            desc.Flags = D3D12_RESOURCE_FLAG_NONE;
            desc.Format = c_iblFormat;
            iblData.irradiance = m_Awesome->CreateBuffer(desc, L"IBL Irradiance Buffer", D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST, nullptr);
        }

        m_Awesome->TransitionResource(iblData.specProbe, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }
    m_Awesome->EndFrame(false);

    // Generate Mips
    m_Awesome->GetTextureSystem()->GenerateMips(iblData.specProbe, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    m_Awesome->BeginFrame(false);
    // Radiance
    {
        PIXScopedEvent(m_Awesome->GetCommandList(), 0, "IBL Filter Radiance Dispatch");

        m_Awesome->TransitionResource(iblOutput, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        m_Awesome->GetCommandList()->SetComputeRootSignature(m_rootSignature);

        ID3D12DescriptorHeap* descriptorHeaps[] = { m_iblCbvSrvUavDescHeap };
        m_Awesome->GetCommandList()->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);
        m_Awesome->GetComputeSystem()->SetPSO(m_iblPreFilterPSO);

        void* mapped = nullptr;
        m_iblConstantBuffer->Map(0, nullptr, &mapped);


        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
        srvDesc.Format = c_iblFormat;
        srvDesc.TextureCube.MipLevels = mipCount;
        srvDesc.TextureCube.ResourceMinLODClamp = 0.0f;
        srvDesc.TextureCube.MostDetailedMip = 0;

        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
        uavDesc.Format = c_iblFormat;
        uavDesc.Texture2DArray.ArraySize = 6;

        auto gpuHandle = m_iblCbvSrvUavDescHeap->GetGPUDescriptorHandleForHeapStart();
        handle = m_iblCbvSrvUavDescHeap->GetCPUDescriptorHandleForHeapStart();
        for (uint32 m = 0; m < mipCount; ++m)
        {
            IBLConstants constants = {};
            constants.mipCount = (float)mipCount;
            uint32 size = std::max(uint32(faceSize) >> m, 1u);

            constants.screenSize = {
                1.0f / float(size),
                1.0f / float(size),
                float(faceSize),
                float(faceSize) };

            constants.sampleCount = c_iblSamples;
            constants.roughness = float(m) / float(mipCount-1);
            uint32 moffset = (IBLConstantsAlignedSize * m);
            uint8* map = reinterpret_cast<uint8*>(mapped);
            memcpy(map + moffset, &constants, sizeof(IBLConstants));
           
            uavDesc.Texture2DArray.MipSlice = m;

            {
                m_Awesome->Device()->CreateShaderResourceView(iblData.specProbe, &srvDesc, handle);
                handle.ptr += offset;
            }

            {
                m_Awesome->Device()->CreateUnorderedAccessView(iblOutput, nullptr, &uavDesc, handle);
                handle.ptr += offset;
            }

            m_Awesome->GetCommandList()->SetComputeRootConstantBufferView(0, m_iblConstantBuffer->GetGPUVirtualAddress() + moffset);
            m_Awesome->GetCommandList()->SetComputeRootDescriptorTable(1, gpuHandle);
            gpuHandle.ptr += offset * 2;

            uint32 dispatchX = std::max(uint32(size / c_compute_thread_x ), 1u);
            uint32 dispatchY = std::max(uint32(size / c_compute_thread_y), 1u);

            m_Awesome->GetCommandList()->Dispatch(dispatchX, dispatchY, 6);
            {
                D3D12_RESOURCE_BARRIER barrier = {};
                barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
                barrier.Transition.pResource = iblOutput;
                m_Awesome->GetCommandList()->ResourceBarrier(1, &barrier);
            }
        }
        m_iblConstantBuffer->Unmap(0, nullptr);

        m_Awesome->TransitionResource(iblOutput, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
        m_Awesome->TransitionResource(iblData.specProbe, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);

        for (uint32 m = 0; m < mipCount; ++m)
        {
            for (uint32 i = 0; i < 6; ++i)
            {
                D3D12_TEXTURE_COPY_LOCATION destination = {};
                destination.pResource = iblData.specProbe;
                destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                destination.SubresourceIndex = m + (i * mipCount);

                D3D12_TEXTURE_COPY_LOCATION source = {};
                source.pResource = iblOutput;
                source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                source.SubresourceIndex = m + (i * mipCount);

                m_Awesome->GetCommandList()->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
            }
        }

        m_Awesome->TransitionResource(iblData.specProbe, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }
    m_Awesome->EndFrame(false);
    
    //if (rdoc_api) rdoc_api->StartFrameCapture(NULL, NULL);
    //std::filesystem::path ibl_name = std::filesystem::path(path).filename().replace_extension(L"wpix");
    //std::filesystem::path cap_root_path = std::filesystem::path(L"D:/captures/Awesome/");
    //std::filesystem::path pat_path = cap_root_path / ibl_name;
    //PIXCaptureParameters captureParams = {};
    //captureParams.GpuCaptureParameters.FileName = pat_path.c_str();
    //PIXBeginCapture(PIX_CAPTURE_GPU, &captureParams);

    m_Awesome->BeginFrame(false);
    // Irrandiance
    {
        PIXScopedEvent(m_Awesome->GetCommandList(), 0, "IBL Irradiance Dispatch");
        m_Awesome->GetCommandList()->SetComputeRootSignature(m_rootSignature);

        ID3D12DescriptorHeap* descriptorHeaps[] = { m_iblCbvSrvUavDescHeap };
        m_Awesome->GetCommandList()->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);
        m_Awesome->GetComputeSystem()->SetPSO(m_iblIrradiancePSO);

        void* mapped = nullptr;
        m_iblConstantBuffer->Map(0, nullptr, &mapped);

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
        srvDesc.Format = c_iblFormat;
        srvDesc.TextureCube.MipLevels = mipCount;
        srvDesc.TextureCube.ResourceMinLODClamp = 0.0f;
        srvDesc.TextureCube.MostDetailedMip = 0;

        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
        uavDesc.Format = c_iblFormat;
        uavDesc.Texture2DArray.ArraySize = 6;

        auto gpuHandle = m_iblCbvSrvUavDescHeap->GetGPUDescriptorHandleForHeapStart();
        handle = m_iblCbvSrvUavDescHeap->GetCPUDescriptorHandleForHeapStart();

        IBLConstants constants = {};
        constants.mipCount = (float)mipCount;

        constants.screenSize = {
            1.0f / float(c_iblIrradianceSize),
            1.0f / float(c_iblIrradianceSize),
            float(c_iblIrradianceSize),
            float(c_iblIrradianceSize) };

        constants.sampleCount = c_iblSamples;
        constants.roughness = 0;

        uint8* map = reinterpret_cast<uint8*>(mapped);
        memcpy(map, &constants, sizeof(IBLConstants));

        uavDesc.Texture2DArray.MipSlice = 0;

        {
            m_Awesome->Device()->CreateShaderResourceView(iblData.specProbe, &srvDesc, handle);
            handle.ptr += offset;
        }

        {
            m_Awesome->Device()->CreateUnorderedAccessView(irrOutput, nullptr, &uavDesc, handle);
            handle.ptr += offset;
        }

        m_Awesome->GetCommandList()->SetComputeRootConstantBufferView(0, m_iblConstantBuffer->GetGPUVirtualAddress());
        m_Awesome->GetCommandList()->SetComputeRootDescriptorTable(1, gpuHandle);
        gpuHandle.ptr += offset * 2;

        uint32 dispatchX = std::max(uint32(c_iblIrradianceSize / c_compute_thread_x), 1u);
        uint32 dispatchY = std::max(uint32(c_iblIrradianceSize / c_compute_thread_y), 1u);
        m_Awesome->GetCommandList()->Dispatch(dispatchX, dispatchY, 6);

        m_Awesome->TransitionResource(irrOutput, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);

        for (uint32 i = 0; i < 6; ++i)
        {
            D3D12_TEXTURE_COPY_LOCATION destination = {};
            destination.pResource = iblData.irradiance;
            destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            destination.SubresourceIndex = i;

            D3D12_TEXTURE_COPY_LOCATION source = {};
            source.pResource = irrOutput;
            source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            source.SubresourceIndex = i;

            m_Awesome->GetCommandList()->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
        }


        m_Awesome->TransitionResource(iblData.irradiance, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    }
    m_Awesome->EndFrame(false);

    //PIXEndCapture(FALSE);
    //if (rdoc_api) rdoc_api->EndFrameCapture(NULL, NULL);

    SafeRelease(iblInput);
    SafeRelease(iblOutput);
    SafeRelease(irrOutput);

	return true;
}

ID3D12Resource* Awesome::IBLProcessor::GenerateSplitSum()
{
    ID3D12Resource* splitSumOut = nullptr;
    ID3D12Resource* splitSum = nullptr;
    uint32 offset = m_Awesome->Device()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    auto handle = m_iblCbvSrvUavDescHeap->GetCPUDescriptorHandleForHeapStart();

    uint32 width = 256;
    uint32 height = 256;

    // Deferred Output Buffer
    {
        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Alignment = 0;
        desc.Width = width;
        desc.Height = height;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.SampleDesc.Count = 1;
        desc.SampleDesc.Quality = 0;
        desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        desc.Format = c_splitSumFormat;

        splitSumOut = m_Awesome->CreateBuffer(desc, L"IBL Split Sum Output Buffer", D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr);
    }

    // Output UAV
    {
        handle.ptr += offset;
        m_Awesome->Device()->CreateUnorderedAccessView(splitSumOut, nullptr, nullptr, handle);
    }
    //if (rdoc_api) rdoc_api->StartFrameCapture(NULL, NULL);
    //std::filesystem::path ibl_name = std::filesystem::path(L"split_sum").replace_extension(L"wpix");
    //std::filesystem::path cap_root_path = std::filesystem::path(L"D:/captures/Awesome/");
    //std::filesystem::path pat_path = cap_root_path / ibl_name;
    //PIXCaptureParameters captureParams = {};
    //captureParams.GpuCaptureParameters.FileName = pat_path.c_str();
    //PIXBeginCapture(PIX_CAPTURE_GPU, &captureParams);
    m_Awesome->BeginFrame(false);
    {
        PIXScopedEvent(m_Awesome->GetCommandList(), 0, "IBL Generate Split Sum Texture Output");
        m_Awesome->GetCommandList()->SetComputeRootSignature(m_rootSignature);

        ID3D12DescriptorHeap* descriptorHeaps[] = { m_iblCbvSrvUavDescHeap };
        m_Awesome->GetCommandList()->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);
        m_Awesome->GetComputeSystem()->SetPSO(m_splitSumComputePSO);
        IBLConstants constants = {};
        constants.screenSize = {
            1.0f / float(width),
            1.0f / float(height),
            float(width),
            float(height) };

        constants.sampleCount = c_iblSamples;
        void* mapped = nullptr;
        m_iblConstantBuffer->Map(0, nullptr, &mapped);
        memcpy(mapped, &constants, sizeof(IBLConstants));
        m_iblConstantBuffer->Unmap(0, nullptr);

        m_Awesome->GetCommandList()->SetComputeRootConstantBufferView(0, m_iblConstantBuffer->GetGPUVirtualAddress());
        m_Awesome->GetCommandList()->SetComputeRootDescriptorTable(1, m_iblCbvSrvUavDescHeap->GetGPUDescriptorHandleForHeapStart());
        uint32 dispatchX = width / c_compute_thread_x;
        uint32 dispatchY = height / c_compute_thread_y;

        m_Awesome->GetCommandList()->Dispatch(dispatchX, dispatchY, 1);
        m_Awesome->TransitionResource(splitSumOut, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);

        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Alignment = 0;
        desc.Width = width;
        desc.Height = height;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.SampleDesc.Count = 1;
        desc.SampleDesc.Quality = 0;
        desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        desc.Flags = D3D12_RESOURCE_FLAG_NONE;
        desc.Format = c_splitSumFormat;

        splitSum = m_Awesome->CreateBuffer(desc, L"IBL Split Sum Texture", D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST, nullptr);

        {
            D3D12_TEXTURE_COPY_LOCATION destination = {};
            destination.pResource = splitSum;
            destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            destination.SubresourceIndex = 0;

            D3D12_TEXTURE_COPY_LOCATION source = {};
            source.pResource = splitSumOut;
            source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            source.SubresourceIndex = 0;

            m_Awesome->GetCommandList()->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
        }


        m_Awesome->TransitionResource(splitSum, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }
    m_Awesome->EndFrame(false);
    //if (rdoc_api) rdoc_api->EndFrameCapture(NULL, NULL);
    //PIXEndCapture(FALSE);
    SafeRelease(splitSumOut);
    return splitSum;
}

IBLSystem::IBLSystem(AwesomeGraphics* Awesome)
	: m_Awesome(Awesome)
	, m_iblProbes(0)
	, m_iblNames(0)
	, m_iblproccessor(new IBLProcessor(m_Awesome))
	, m_currentIBL(invalidIndex32)
    , m_splitSumTexture(nullptr)
    , m_skyBlur(0.0f)
{
}

IBLSystem::~IBLSystem()
{
}

bool IBLSystem::StartUp()
{
    if (!m_iblproccessor->StartUp())
        return false;

    m_splitSumTexture = m_iblproccessor->GenerateSplitSum();

	return true;
}

bool IBLSystem::TearDown()
{
    for (uint32 i = 0; i < uint32(m_iblProbes.size()); ++i)
    {
        SafeRelease(m_iblProbes[i].specProbe);
        SafeRelease(m_iblProbes[i].irradiance);
    }
	m_iblProbes.clear();
	m_iblNames.clear();

    SafeRelease(m_splitSumTexture);

	return m_iblproccessor->TearDown();
}

uint32 IBLSystem::AddIBL(const char* path, const char* name)
{
    DebugPrint("Adding IBL %s\n", name);
    IBLData ibl;
    uint32 index = invalidIndex32;
    if (m_iblproccessor->AddIBL(path, ibl))
    {
        index = uint32(m_iblProbes.size());
        m_iblProbes.push_back(ibl);
        m_iblNames.push_back(std::string(name));
    }

	return uint32();
}

void IBLSystem::SetCurrentIBL(uint32 ibl) 
{ 
    m_currentIBL = ibl; 
    //m_Awesome->GetDeferredRenderer()->SetIBL(m_currentIBL);
}

uint32 IBLSystem::GetIBLByName(std::string name)
{
    for (uint32 i = 0; i < uint32(m_iblNames.size()); ++i)
    {
        if (name == m_iblNames[i])
            return i;
    }
    return invalidIndex32;
}

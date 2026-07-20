#include "Material.h"
#include "Awesome.h"
#include "Compute.h"
#include "Util.h"
#include <fstream>
#include <filesystem>
#include <d3d12.h>
#include <d3dcompiler.h>
#include "pix3.h"
#include "Scene.h"
#include "Deferred.h"
#include "TAA.h"
#include "Resource.h"
#include "ResourcePool.h"
#include "DescriptorHeap.h"
#include "Texture.h"

using namespace Awesome;

MaterialSystem::MaterialSystem(AwesomeGraphics* Awesome)
    : m_Awesome(Awesome)
    , m_rootSignature(nullptr)
    , m_lastPSO(invalidIndex32)
{
    m_vertConstantBufferUploadHeap = nullptr;
    m_shadowConstantBufferUploadHeap = nullptr;
    m_pixelConstantBufferUploadHeap = nullptr;
    
    m_vertexConstantsGPUAddress = nullptr;
    m_shadowConstantsGPUAddress = nullptr;
    m_pixelConstantsGPUAddress = nullptr;

    m_materialData.reserve(20);
    m_forwardPSOs.reserve(20);
    m_deferredPSOs.reserve(20);
    m_shaderData.reserve(20);
}

MaterialSystem::~MaterialSystem()
{
}

bool Awesome::MaterialSystem::StartUp()
{
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
        descriptorTable.pDescriptorRanges = &descriptorTableRanges[0];

        D3D12_ROOT_PARAMETER  rootParameters[3];
        rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        rootParameters[0].Descriptor = {0,0};
        rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

        rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        rootParameters[1].Descriptor = { 1,0 };
        rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rootParameters[2].DescriptorTable = descriptorTable;
        rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        // create static samplers
        D3D12_STATIC_SAMPLER_DESC samplers[1];
        samplers[0].Filter = D3D12_FILTER_ANISOTROPIC;
        samplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        samplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        samplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        samplers[0].MipLODBias = 0;
        samplers[0].MaxAnisotropy = 8;
        samplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
        samplers[0].BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
        samplers[0].MinLOD = 0.0f;
        samplers[0].MaxLOD = D3D12_FLOAT32_MAX;
        samplers[0].ShaderRegister = 0;
        samplers[0].RegisterSpace = 0;
        samplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        D3D12_ROOT_SIGNATURE_DESC desc = {};
        desc.NumParameters = _countof(rootParameters);
        desc.pParameters = rootParameters;
        desc.NumStaticSamplers = _countof(samplers);
        desc.pStaticSamplers = samplers;
        desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
            | D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS
            | D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS
            | D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

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

    return true;
}

bool MaterialSystem::TearDown()
{
    for (uint32 i = 0; i < m_forwardPSOs.size(); ++i)
    {
        SafeRelease(m_forwardPSOs[i]);
    }
    for (uint32 i = 0; i < m_deferredPSOs.size(); ++i)
    {
        SafeRelease(m_deferredPSOs[i]);
    }
    SafeRelease(m_rootSignature);
    FreeResources();

    m_vertexConstantsGPUAddress = nullptr;
    m_shadowConstantsGPUAddress = nullptr;
    m_pixelConstantsGPUAddress = nullptr;

    return true;
}

void MaterialSystem::SetupRender()
{
    PIXScopedEvent(m_Awesome->GetCommandList(), 0, "Material System Setup");
    SetupResources();

    m_Awesome->GetCommandList()->SetGraphicsRootSignature(m_rootSignature);
    m_lastPSO = invalidIndex32;
}

void MaterialSystem::SetupMaterial(const uint32 instance, const uint32 index, const XMMATRIX xform, const XMMATRIX viewProj, const XMMATRIX prevViewProj, const  XMMATRIX jitter)
{
    MaterialData& data = m_materialData[index];

    uint32 idx = data.m_psoIndex + ShaderPermute::Base;
    if (m_lastPSO != idx)
    {
        m_Awesome->GetCommandList()->SetPipelineState(m_deferredPSOs[idx]);
        m_lastPSO = idx;
    }

    XMMATRIX w = xform;
    XMMATRIX wvp = w * viewProj;
    XMMATRIX prevWvp = w * prevViewProj;
    
    VertexConstants vertexConstants = {};
    XMStoreFloat4x4(&vertexConstants.worldViewProj, wvp);
    XMStoreFloat4x4(&vertexConstants.PrevWorldViewProj, prevWvp);
    XMStoreFloat4x4(&vertexConstants.world, w);

    XMMATRIX jitteredWVP = wvp * jitter;
    XMStoreFloat4x4(&vertexConstants.jitterMatrix, jitteredWVP);

    memcpy(m_vertexConstantsGPUAddress + m_vcOffset, &vertexConstants, sizeof(vertexConstants));

    m_Awesome->GetCommandList()->SetGraphicsRootConstantBufferView(0, m_vertConstantBufferUploadHeap->resource->GetGPUVirtualAddress() + m_vcOffset);

    m_vcOffset += VertexConstantsAlignedSize;

    Texture mainTex = m_Awesome->GetTextureSystem()->GetTexture(data.m_textures[0]);

    PixelConstants pixelConstants = {};
    pixelConstants.fun = {1,0,1,1};
    if (m_Awesome->GetTAAEnabled())
    {
        XMFLOAT4X4 j = m_Awesome->GetTAA()->GetFrameJitter();
        pixelConstants.fun.x = j._41 * float(m_Awesome->GetWidth());
        pixelConstants.fun.x = j._42 * float(m_Awesome->GetHeight());
    }
    // Init first
    for(uint32 i = 0; i < 6; i++)
        pixelConstants.texInfo[0] = {0,0,0,0};

    for (uint32 t = 0; t < data.m_texturesCount; t++)
    {
        Texture tex = m_Awesome->GetTextureSystem()->GetTexture(data.m_textures[t]);
        pixelConstants.texInfo[t] = { tex.sizeInfo[0], tex.sizeInfo[1], tex.sizeInfo[2], tex.sizeInfo[3] };
    }

    memcpy(m_pixelConstantsGPUAddress + m_pcOffset, &pixelConstants, sizeof(pixelConstants));
    m_Awesome->GetCommandList()->SetGraphicsRootConstantBufferView(1, m_pixelConstantBufferUploadHeap->resource->GetGPUVirtualAddress() + m_pcOffset);
    m_pcOffset += PixelConstantsAlignedSize;

    m_Awesome->GetCommandList()->SetGraphicsRootDescriptorTable(2, mainTex.handle.gpuHandle);
}

void MaterialSystem::SetupShadow(const uint32 instance, const uint32 index, const uint32 cascade, const XMMATRIX& viewProj)
{
    MaterialData& data = m_materialData[index];

    uint32 idx = data.m_psoIndex + ShaderPermute::Shadow;
    if (m_lastPSO != idx)
    {
        m_Awesome->GetCommandList()->SetPipelineState(m_deferredPSOs[idx]);
        m_lastPSO = idx;
    }

    ShadowConstants shadowConstants = {};
    XMStoreFloat4x4(&shadowConstants.shadowMatrix, viewProj);

    uint32 voffset = ShadowConstantsAlignedSize * (cascade * 1024 + index);
    memcpy(m_shadowConstantsGPUAddress + voffset, &shadowConstants, sizeof(shadowConstants));

    m_Awesome->GetCommandList()->SetGraphicsRootConstantBufferView(0, m_shadowConstantBufferUploadHeap->resource->GetGPUVirtualAddress() + voffset);

    uint32 poffset = PixelConstantsAlignedSize * index;
    m_Awesome->GetCommandList()->SetGraphicsRootConstantBufferView(1, m_pixelConstantBufferUploadHeap->resource->GetGPUVirtualAddress() + poffset);

    Texture tex = m_Awesome->GetTextureSystem()->GetTexture(data.m_textures[0]);

    m_Awesome->GetCommandList()->SetGraphicsRootDescriptorTable(2, tex.handle.gpuHandle);
}

void MaterialSystem::PostRender()
{
    m_vertConstantBufferUploadHeap->resource->Unmap(0, nullptr);
    m_shadowConstantBufferUploadHeap->resource->Unmap(0, nullptr);
    m_pixelConstantBufferUploadHeap->resource->Unmap(0, nullptr);
    FreeResources();
}

uint32 MaterialSystem::CreateMaterial(uint32 pso, const std::vector<uint32>& textures)
{
    MaterialData data = {};
    data.m_psoIndex = pso;
    data.m_texturesCount = std::min(uint32(textures.size()), 12u);
    for (uint32 i = 0; i < data.m_texturesCount; ++i)
    {
        data.m_textures[i] = textures[i];
    }
    uint32 index = uint32(m_materialData.size());
    m_materialData.push_back(data);
    return index;
}

uint32 MaterialSystem::CreatePipeline(uint32 vertex, uint32 pixel, const D3D12_INPUT_LAYOUT_DESC& meshLayout, bool twosided)
{
    // Pipeline State Object

    uint32 dindex = uint32(m_deferredPSOs.size());

    for(uint32 i = 0; i < ShaderPermute::Count; i++)
    {
        ID3D12PipelineState* pipelineStateObject;
        {
            DXGI_SAMPLE_DESC sampleDesc = {};
            sampleDesc.Count = 1;

            D3D12_RASTERIZER_DESC rasterDesc = {};
            rasterDesc.FillMode = D3D12_FILL_MODE_SOLID;
            rasterDesc.CullMode = twosided ? D3D12_CULL_MODE_NONE : D3D12_CULL_MODE_BACK;
            rasterDesc.FrontCounterClockwise = FALSE;
            rasterDesc.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
            rasterDesc.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
            rasterDesc.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
            rasterDesc.DepthClipEnable = TRUE;
            rasterDesc.MultisampleEnable = FALSE;
            rasterDesc.AntialiasedLineEnable = FALSE;
            rasterDesc.ForcedSampleCount = 0;
            rasterDesc.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

            D3D12_BLEND_DESC blendDesc = {};
            blendDesc.AlphaToCoverageEnable = FALSE;
            blendDesc.IndependentBlendEnable = FALSE;
            blendDesc.RenderTarget[0].BlendEnable = FALSE;
            blendDesc.RenderTarget[0].LogicOpEnable = FALSE;
            blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
            blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_ZERO;
            blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
            blendDesc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
            blendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
            blendDesc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
            blendDesc.RenderTarget[0].LogicOp = D3D12_LOGIC_OP_NOOP;
            blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

            D3D12_DEPTH_STENCILOP_DESC dsOpDesc = {};
            dsOpDesc.StencilFailOp = D3D12_STENCIL_OP_KEEP;
            dsOpDesc.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
            dsOpDesc.StencilPassOp = D3D12_STENCIL_OP_KEEP;
            dsOpDesc.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;

            D3D12_DEPTH_STENCIL_DESC depthDesc = {};
            depthDesc.DepthEnable = TRUE;
            depthDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
            depthDesc.DepthFunc = i == ShaderPermute::Shadow ? D3D12_COMPARISON_FUNC_LESS : D3D12_COMPARISON_FUNC_GREATER;
            depthDesc.StencilEnable = FALSE;
            depthDesc.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
            depthDesc.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;
            depthDesc.FrontFace = dsOpDesc;
            depthDesc.BackFace = dsOpDesc;

            D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
            desc.InputLayout = meshLayout;
            desc.pRootSignature = m_rootSignature;
            desc.VS = m_shaderData[vertex + i].shader;
            desc.PS = m_shaderData[pixel + i].shader;
            desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            desc.SampleDesc = sampleDesc;
            desc.SampleMask = 0xffffffff;
            desc.RasterizerState = rasterDesc;
            desc.BlendState = blendDesc;

            uint32 rtCount = i == ShaderPermute::Base ? uint32(DeferredBuffers::Count) : 0;

            desc.NumRenderTargets = rtCount;
            for (uint32 i = 0; i < rtCount; ++i)
                desc.RTVFormats[i] = c_deferredFormats[i];
            desc.DepthStencilState = depthDesc;
            desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
            if (FAILED(m_Awesome->Device()->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pipelineStateObject))))
                return -1;
        }

        m_deferredPSOs.push_back(pipelineStateObject);
    }

    return dindex;
}

uint32 MaterialSystem::CreateUIPipeline(uint32 vertex, uint32 pixel, const D3D12_INPUT_LAYOUT_DESC& meshLayout, ID3D12RootSignature* rootSignature, bool hdr)
{
    // Pipeline State Object

    uint32 dindex = uint32(m_deferredPSOs.size());

    ID3D12PipelineState* pipelineStateObject;
    {
        DXGI_SAMPLE_DESC sampleDesc = {};
        sampleDesc.Count = 1;

        D3D12_RASTERIZER_DESC rasterDesc = {};
        rasterDesc.FillMode = D3D12_FILL_MODE_SOLID;
        rasterDesc.CullMode = D3D12_CULL_MODE_BACK;
        rasterDesc.FrontCounterClockwise = FALSE;
        rasterDesc.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
        rasterDesc.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
        rasterDesc.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
        rasterDesc.DepthClipEnable = TRUE;
        rasterDesc.MultisampleEnable = FALSE;
        rasterDesc.AntialiasedLineEnable = FALSE;
        rasterDesc.ForcedSampleCount = 0;
        rasterDesc.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

        D3D12_BLEND_DESC blendDesc = {};
        blendDesc.AlphaToCoverageEnable = FALSE;
        blendDesc.IndependentBlendEnable = FALSE;
        blendDesc.RenderTarget[0].BlendEnable = TRUE;
        blendDesc.RenderTarget[0].LogicOpEnable = FALSE;
        blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
        blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
        blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
        blendDesc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
        blendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
        blendDesc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
        blendDesc.RenderTarget[0].LogicOp = D3D12_LOGIC_OP_CLEAR;
        blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

        D3D12_DEPTH_STENCILOP_DESC dsOpDesc = {};
        dsOpDesc.StencilFailOp = D3D12_STENCIL_OP_KEEP;
        dsOpDesc.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
        dsOpDesc.StencilPassOp = D3D12_STENCIL_OP_KEEP;
        dsOpDesc.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;

        D3D12_DEPTH_STENCIL_DESC depthDesc = {};
        depthDesc.DepthEnable = FALSE;
        depthDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
        depthDesc.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        depthDesc.StencilEnable = FALSE;
        depthDesc.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
        depthDesc.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;
        depthDesc.FrontFace = dsOpDesc;
        depthDesc.BackFace = dsOpDesc;

        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
        desc.InputLayout = meshLayout;
        desc.pRootSignature = rootSignature;
        desc.VS = m_shaderData[vertex].shader;
        desc.PS = m_shaderData[pixel].shader;
        desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        desc.SampleDesc = sampleDesc;
        desc.SampleMask = 0xffffffff;
        desc.RasterizerState = rasterDesc;
        desc.BlendState = blendDesc;

        desc.NumRenderTargets = 1;
        desc.RTVFormats[0] = hdr ? m_Awesome->GetSwapChainFormat() : m_Awesome->GetUIFormat();
        desc.DepthStencilState = depthDesc;
        desc.DSVFormat = DXGI_FORMAT_UNKNOWN;
        if (FAILED(m_Awesome->Device()->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pipelineStateObject))))
            return -1;


        m_deferredPSOs.push_back(pipelineStateObject);
    }

    return dindex;
}

void MaterialSystem::SetPSO(uint32 pso)
{
    m_Awesome->GetCommandList()->SetPipelineState(m_deferredPSOs[pso]);
}

#define D3D_COMPILE_STANDARD_FILE_INCLUDE ((ID3DInclude*)(UINT_PTR)1)

uint32 MaterialSystem::LoadShader(const wchar_t* path, ShaderType type, const std::vector< D3D_SHADER_MACRO*>& permuations)
{
    auto filePath = std::filesystem::path(path);
    if (filePath.is_relative())
        filePath = L"shaders" / filePath;

    std::string ext = filePath.extension().string();

    if (ext == "")
    {
        filePath.replace_extension(L"cso");
        if (!std::filesystem::exists(filePath))
        {
            filePath.replace_extension(L"hlsl");
            if (!std::filesystem::exists(filePath))
            {
                DebugPrint("Can't find valid shader file for %S\n", path);
                return invalidIndex32;
            }
        }
    }

    ext = filePath.extension().string();

    if (ext == ".cso")
        return LoadCompiledShader(filePath.c_str(), type, permuations);
    else if (ext == ".hlsl")
        return CompileShader(filePath.c_str(), type, permuations);
    else
    {
        {
            DebugPrint("Bad extension for %S\n", filePath.c_str());
            return invalidIndex32;
        }
    }
}

uint32 MaterialSystem::CompileShader(const wchar_t* path, ShaderType type, const std::vector< D3D_SHADER_MACRO*>& permuations)
{
    DebugPrint("Compiling shader %S\n", path);
    uint32 index = uint32(m_shaderData.size());
    for (uint32 i = 0; i < uint32(permuations.size()); ++i)
    {
        ShaderData data = {};
        data.type = type;
        data.shader = {};

        ID3DBlob* shader;
        {
            ID3DBlob* errorBuff;
            if (FAILED(D3DCompileFromFile(path,
                permuations[i],
                D3D_COMPILE_STANDARD_FILE_INCLUDE,
                "main",
                ShaderTypeToTarget(type),
                D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION,
                0,
                &shader,
                &errorBuff)))
            {
                DebugPrint((char*)errorBuff->GetBufferPointer());
                return invalidIndex32;
            }
        }

        data.shader.BytecodeLength = shader->GetBufferSize();
        data.shader.pShaderBytecode = shader->GetBufferPointer();

        m_shaderData.push_back(data);
    }
    return index;
}

uint32 MaterialSystem::LoadCompiledShader(const wchar_t* path, ShaderType type, const std::vector<D3D_SHADER_MACRO*>& permuations)
{
    DebugPrint("Loading compiled shader %S\n", path);
    uint32 index = uint32(m_shaderData.size());
    for (uint32 i = 0; i < uint32(permuations.size()); ++i)
    {
        std::filesystem::path f_path = std::filesystem::path(path);
        auto perm = permuations[i]->Name;
        if (perm)
        {
            std::string f_name = f_path.filename().string();
            f_name = perm + std::string("_") + f_name;
            f_path = f_path.replace_filename(f_name);
        }
            
        ShaderData data = {};
        data.type = type;
        data.shader = {};

        // open the file:
        std::streampos fileSize;
        std::ifstream file(f_path, std::ios::binary);

        // get its size:
        file.seekg(0, std::ios::end);
        fileSize = file.tellg();
        file.seekg(0, std::ios::beg);

        // read the data:
        data.shader.BytecodeLength = fileSize;
        data.shader.pShaderBytecode = (void*)(new char[data.shader.BytecodeLength]);
        file.read((char*)data.shader.pShaderBytecode, fileSize);

        m_shaderData.push_back(data);
    }
    return index;
}

const ShaderData& MaterialSystem::GetShader(uint32 index)
{
    assert(index < m_shaderData.size());
    return m_shaderData[index];
}

uint32 MaterialSystem::GetMaterialCount()
{
    return (uint32)m_materialData.size();
}

void MaterialSystem::SetupResources()
{   
    m_vcOffset = 0;
    m_scOffset = 0;
    m_pcOffset = 0;
    // Vertex Constants
    {
        D3D12_RESOURCE_DESC resDesc = {};
        resDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        resDesc.Alignment = 0;
        resDesc.Width = 2048 * VertexConstantsAlignedSize;
        resDesc.Height = 1;
        resDesc.DepthOrArraySize = 1;
        resDesc.MipLevels = 1;
        resDesc.Format = DXGI_FORMAT_UNKNOWN;
        resDesc.SampleDesc.Count = 1;
        resDesc.SampleDesc.Quality = 0;
        resDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        resDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

        wchar_t name[256];
        swprintf(name, 256, L"Vertex Constant Buffer Upload Resource Heap");

        ResourceDesc rDesc = {};
        rDesc.desc = resDesc;
        rDesc.state = D3D12_RESOURCE_STATE_GENERIC_READ;
        m_vertConstantBufferUploadHeap = m_Awesome->GetResourcePool()->Allocate(rDesc, name, D3D12_HEAP_TYPE_UPLOAD);

        // TODO:  Maybe make this work?
        //D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
        //cbvDesc.BufferLocation = m_vertConstantBufferUploadHeap->resource->GetGPUVirtualAddress();
        //cbvDesc.SizeInBytes = VertexConstantsAlignedSize;
        //m_Awesome->Device()->CreateConstantBufferView(&cbvDesc, m_mainDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

        if (FAILED(m_vertConstantBufferUploadHeap->resource->Map(0, nullptr, reinterpret_cast<void**>(&m_vertexConstantsGPUAddress))))
            return;
    }

    // Shadow Constants
    {
        D3D12_RESOURCE_DESC resDesc = {};
        resDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        resDesc.Alignment = 0;
        resDesc.Width = 2048 * c_maxCascadeCount * ShadowConstantsAlignedSize;
        resDesc.Height = 1;
        resDesc.DepthOrArraySize = 1;
        resDesc.MipLevels = 1;
        resDesc.Format = DXGI_FORMAT_UNKNOWN;
        resDesc.SampleDesc.Count = 1;
        resDesc.SampleDesc.Quality = 0;
        resDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        resDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

        wchar_t name[256];
        swprintf(name, 256, L"Shadow Constant Buffer Upload Resource Heap");
        ResourceDesc rDesc = {};
        rDesc.desc = resDesc;
        rDesc.state = D3D12_RESOURCE_STATE_GENERIC_READ;
        m_shadowConstantBufferUploadHeap = m_Awesome->GetResourcePool()->Allocate(rDesc, name, D3D12_HEAP_TYPE_UPLOAD);

        // TODO:  Maybe make this work?
        //D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
        //cbvDesc.BufferLocation = m_shadowConstantBufferUploadHeap->resource->GetGPUVirtualAddress();
        //cbvDesc.SizeInBytes = ShadowConstantsAlignedSize;
        //m_Awesome->Device()->CreateConstantBufferView(&cbvDesc, m_mainDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

        if (FAILED(m_shadowConstantBufferUploadHeap->resource->Map(0, nullptr, reinterpret_cast<void**>(&m_shadowConstantsGPUAddress))))
            return;
    }

    // Pixel Constants
    {
        D3D12_RESOURCE_DESC resDesc = {};
        resDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        resDesc.Alignment = 0;
        resDesc.Width = 2048 * PixelConstantsAlignedSize;
        resDesc.Height = 1;
        resDesc.DepthOrArraySize = 1;
        resDesc.MipLevels = 1;
        resDesc.Format = DXGI_FORMAT_UNKNOWN;
        resDesc.SampleDesc.Count = 1;
        resDesc.SampleDesc.Quality = 0;
        resDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        resDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

        wchar_t name[256];
        swprintf(name, 256, L"Pixel Constant Buffer Upload Resource Heap");
        ResourceDesc rDesc = {};
        rDesc.desc = resDesc;
        rDesc.state = D3D12_RESOURCE_STATE_GENERIC_READ;
        m_pixelConstantBufferUploadHeap = m_Awesome->GetResourcePool()->Allocate(rDesc, name, D3D12_HEAP_TYPE_UPLOAD);

        // TODO:  Maybe make this work?
        //D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
        //cbvDesc.BufferLocation = m_pixelConstantBufferUploadHeap->resource->GetGPUVirtualAddress();
        //cbvDesc.SizeInBytes = PixelConstantsAlignedSize;
        //m_Awesome->Device()->CreateConstantBufferView(&cbvDesc, m_mainDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

        if (FAILED(m_pixelConstantBufferUploadHeap->resource->Map(0, nullptr, reinterpret_cast<void**>(&m_pixelConstantsGPUAddress))))
            return;
    }
}

void MaterialSystem::FreeResources()
{
    m_Awesome->GetResourcePool()->Free(m_vertConstantBufferUploadHeap);
    m_Awesome->GetResourcePool()->Free(m_shadowConstantBufferUploadHeap);
    m_Awesome->GetResourcePool()->Free(m_pixelConstantBufferUploadHeap);
}

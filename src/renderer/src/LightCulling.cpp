#include "LightCulling.h"
#include "Awesome.h"
#include "pix3.h"
#include "Scene.h"
#include "Util.h"
#include "Compute.h"
#include "Material.h"
#include "Resource.h"
#include "TAA.h"
#include "ResourcePool.h"
#include "DescriptorHeap.h"
#include <algorithm>
#include <random>
#include <stack>

using namespace Awesome;

TileLightCull::TileLightCull(AwesomeGraphics* awesome)
    : m_Awesome(awesome)
    , m_lightCullRS(nullptr)
    , m_lightCullPSO(invalidIndex32)
    , m_constants(nullptr)
    , m_bvhBuffer(nullptr)
    , m_tileBuffer(nullptr)
    , m_nodeCount(0)
{
}

TileLightCull::~TileLightCull()
{

}

bool TileLightCull::StartUp()
{
    // Create Root Signature
    {
        D3D12_DESCRIPTOR_RANGE  descriptorTableRanges[3];
        // CBV
        descriptorTableRanges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
        descriptorTableRanges[0].NumDescriptors = 1;
        descriptorTableRanges[0].BaseShaderRegister = 0;
        descriptorTableRanges[0].RegisterSpace = 0;
        descriptorTableRanges[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        // SRV (Depth + LightBVH)
        descriptorTableRanges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        descriptorTableRanges[1].NumDescriptors = 2;
        descriptorTableRanges[1].BaseShaderRegister = 0;
        descriptorTableRanges[1].RegisterSpace = 0;
        descriptorTableRanges[1].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        // UAV (Output Tiles)
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
            DebugPrint((char*)errorBuff->GetBufferPointer());
            return false;
        }

        if (FAILED(m_Awesome->Device()->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_lightCullRS))))
            return false;
    }

    std::vector<D3D_SHADER_MACRO*> permuations;
    D3D_SHADER_MACRO* defines = new D3D_SHADER_MACRO[1];
    defines[0] = { NULL, NULL };
    permuations.push_back(defines);

    uint32 shader = m_Awesome->GetComputeSystem()->CompileShader(L"light_culling-c", permuations);
    m_lightCullPSO = m_Awesome->GetComputeSystem()->CreatePipeline(shader, m_lightCullRS);

    InitResources();

    return true;
}

bool TileLightCull::TearDown()
{
    FreeResources();
    SafeRelease(m_lightCullRS);
    m_constants = nullptr;
    m_bvhBuffer = nullptr;
    m_tileBuffer = nullptr;
    return true;
}

void TileLightCull::UpdateMissIndices(uint32 nodeIdx, uint32 missIdx)
{
    m_lightBVH[nodeIdx].missIdx = missIdx;
    
    if (m_lightBVH[nodeIdx].lightIdx != invalidIndex32)
    {
        return;
    }
    
    uint32 leftChildIdx = nodeIdx + 1;
    uint32 rightChildIdx = m_lightBVH[leftChildIdx].missIdx;
    
    UpdateMissIndices(leftChildIdx, rightChildIdx);
    UpdateMissIndices(rightChildIdx, missIdx);
}

void TileLightCull::BuildLightBVH(Scene* scene)
{
    if (!scene->LightsAreDirty())
        return;

    uint32 lightCount = scene->GetLightCount(LightType::Point);
    m_lightBVH.clear();
    m_nodeCount = 0;

    if (lightCount == 0)
        return;

    std::vector<Light> lights;
    for (uint32 l = 0; l < lightCount; l++)
    {
        Light* light = scene->GetPointLight(l);
        if(light->intensity > 0.0f)
            lights.push_back(Light(*light));
    }

    if (lights.size() == 0)
        return;

    m_lightBVH.resize(lights.size() * 2 - 1);

    BuildBVH(lights, 0, (uint32)lights.size());
    
    UpdateMissIndices(0, invalidIndex32);
    
    scene->MarkLightsClean();
}

void TileLightCull::BulldLightTiles()
{
    PIXScopedEvent(m_Awesome->GetCommandList(), 0, "Lighting Tiles");
    
    InitResources();

    SetupHeapViews();

    if (m_nodeCount > 0)
    {
        // Upload BVH Data
        void* mapped = nullptr;
        m_bvhBuffer->resource->Map(0, nullptr, &mapped);

        for (uint32 i = 0; i < m_lightBVH.size(); i++)
        {
            BVHNode& node = m_lightBVH[i];

            if (node.missIdx == 0 && node.lightIdx == 0)
                break;

            GPULight glight = {};

            glight.leftIdx = node.missIdx;

            // Bounds
            if (node.lightIdx == invalidIndex32)
            {
                glight.positionIntensity.x = node.bbox.min.x;
                glight.positionIntensity.y = node.bbox.min.y;
                glight.positionIntensity.z = node.bbox.min.z;
                glight.positionIntensity.w = -1;

                glight.color = node.bbox.max;
            }
            else // Leaf
            {
                Light* sLight = m_Awesome->GetCurrentScene()->GetPointLight(node.lightIdx);
                glight.positionIntensity.x = sLight->position.x;
                glight.positionIntensity.y = sLight->position.y;
                glight.positionIntensity.z = sLight->position.z;
                // REVERTED: Upload Intensity as Intensity
                glight.positionIntensity.w = sLight->intensity; 

                glight.color = sLight->color;
            }

            uint32 offset = sizeof(GPULight) * i;
            uint8* map = reinterpret_cast<uint8*>(mapped);
            memcpy(map + offset, &glight, sizeof(GPULight));
        }

        m_bvhBuffer->resource->Unmap(0, nullptr);

        uint32 tilesX = 1 + ((m_Awesome->GetWidth() - 1) / 16);
        uint32 tilesY = 1 + ((m_Awesome->GetHeight() - 1) / 16);

        m_Awesome->GetCommandList()->SetComputeRootSignature(m_lightCullRS);

        Camera* cam = m_Awesome->GetCurrentScene()->GetCamera();

        LightCullConstants constants = {};
        constants.screenSize = {
            1.0f / float(m_Awesome->GetWidth()),
            1.0f / float(m_Awesome->GetHeight()),
            float(m_Awesome->GetWidth()),
            float(m_Awesome->GetHeight()) };
        
        constants.tileSize = {
            0,
            cam->nearClip,
            float(tilesX),
            float(tilesY) };

        XMMATRIX viewProjMtx = cam->GetViewProjectionSpaceMatrix();
        XMMATRIX invViewProjMtx= XMMatrixInverse(nullptr, viewProjMtx);
        XMMATRIX v = cam->GetViewSpaceMatrix();

        XMStoreFloat4x4(&constants.invViewProjMtx, invViewProjMtx);
        XMStoreFloat4x4(&constants.viewMtx, v);
        constants.cameraPos = cam->transform.position;

        void* constMapped = nullptr;
        m_constants->resource->Map(0, nullptr, &constMapped);
        memcpy(constMapped, &constants, sizeof(LightCullConstants));
        m_constants->resource->Unmap(0, nullptr);

        {
            PIXScopedEvent(m_Awesome->GetCommandList(), 0, "Build & Cull Tiles");

            m_Awesome->TransitionResource(m_tileBuffer->resource, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            
            m_Awesome->GetComputeSystem()->SetPSO(m_lightCullPSO);
            m_Awesome->GetCommandList()->SetComputeRootDescriptorTable(0, m_handles[0].gpuHandle);
            m_Awesome->GetCommandList()->Dispatch(tilesX, tilesY, 1);
        }
    }

    FreeResources();
}

ID3D12Resource* TileLightCull::GetBVHBuffer() { return m_bvhBuffer->resource; }
ID3D12Resource* TileLightCull::GetTileBuffer() { return m_tileBuffer->resource; }
uint32 TileLightCull::GetBVHCount() { return std::max(1u, m_nodeCount); }

std::default_random_engine generator;

void TileLightCull::BuildBVH(std::vector<Light>& lights, uint32 start, uint32 end)
{
    if (end > (uint32)lights.size()) return;
    uint32 currentIdx = m_nodeCount;
    BVHNode& gpuLight = m_lightBVH[m_nodeCount];
    gpuLight.missIdx = invalidIndex32;
    gpuLight.lightIdx = invalidIndex32;
    for (uint32 i = start; i < end; i++)
        gpuLight.bbox.Expand(AABB::FromLight(lights[i]));

    m_nodeCount++;

    uint32 span = end - start;

    if (span == 1)
    {
        gpuLight.missIdx = invalidIndex32;
        gpuLight.lightIdx = lights[start].baseIndex;
    }
    else if (span == 2)
    {
        BVHNode& lightA = m_lightBVH[m_nodeCount];
        lightA.bbox = AABB::FromLight(lights[start]);
        lightA.missIdx = ++m_nodeCount;
        lightA.lightIdx = lights[start].baseIndex;

        BVHNode& lightB = m_lightBVH[m_nodeCount];
        lightB.bbox = AABB::FromLight(lights[start+1]);
        lightB.missIdx = invalidIndex32;
        lightB.lightIdx = lights[start+1].baseIndex;
        m_nodeCount++;
    }
    else {
        std::uniform_int_distribution<uint32> axisPick(0, 2);
        uint32 axisToSort = axisPick(generator);

        std::sort(lights.begin() + start, lights.begin() + end, [axisToSort](Light a, Light b) {
                if (axisToSort == 0) return a.position.x < b.position.x;
                else if (axisToSort == 1) return a.position.y < b.position.y;
                else return a.position.z < b.position.z;
            });

        uint32 mid = start + (span / 2);
        BuildBVH(lights, start, mid);
        m_lightBVH[currentIdx+1].missIdx = m_nodeCount;
        BuildBVH(lights, mid, end);
    }
}

void TileLightCull::InitResources()
{
    // Constant Buffer
    {
        D3D12_RESOURCE_DESC resDesc = {};
        resDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        resDesc.Alignment = 0;
        resDesc.Width = LightCullConstantsAlignedSize;
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
        m_constants = m_Awesome->GetResourcePool()->Allocate(rDesc, L"Light Culling Constant Buffer", D3D12_HEAP_TYPE_UPLOAD);
    }

    // BVH buffer
    {
        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Alignment = 0;
        desc.Width = GetBVHCount() * sizeof(GPULight);
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
        rDesc.state = D3D12_RESOURCE_STATE_COMMON;
        m_bvhBuffer = m_Awesome->GetResourcePool()->Allocate(rDesc, L"BVH Buffer", D3D12_HEAP_TYPE_UPLOAD);
    }

    uint32 tilesX = 1 + ((m_Awesome->GetWidth() - 1) / 16);
    uint32 tilesY = 1 + ((m_Awesome->GetHeight() - 1) / 16);

    // Tile Output Buffer
    {
        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Alignment = 0;
        desc.Width = tilesX * tilesY * sizeof(LightTileData);
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
        m_tileBuffer = m_Awesome->GetResourcePool()->Allocate(rDesc, L"Light Tile Output Buffer", D3D12_HEAP_TYPE_DEFAULT);
    }
}

void TileLightCull::SetupHeapViews()
{
    m_Awesome->GetMainDescHeap()->AllocateBlock(m_handles, 4);

    // Constant View
    {
        D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
        cbvDesc.BufferLocation = m_constants->resource->GetGPUVirtualAddress();
        cbvDesc.SizeInBytes = TonemapConstantsAlignedSize;
        m_Awesome->Device()->CreateConstantBufferView(&cbvDesc, m_handles[0].cpuHandle);
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

        m_Awesome->Device()->CreateShaderResourceView(m_Awesome->GetDepthStencilBuffer(), &dsDesc, m_handles[1].cpuHandle);
    }

    // Light BVH SRV
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC desc = {};
        desc.Format = DXGI_FORMAT_UNKNOWN;
        desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        desc.Buffer.NumElements = GetBVHCount();
        desc.Buffer.StructureByteStride = sizeof(GPULight);
        m_Awesome->Device()->CreateShaderResourceView(m_bvhBuffer->resource, &desc, m_handles[2].cpuHandle);
    }

    uint32 tilesX = 1 + ((m_Awesome->GetWidth() - 1) / 16);
    uint32 tilesY = 1 + ((m_Awesome->GetHeight() - 1) / 16);

    // Output tiles UAV
    {
        D3D12_UNORDERED_ACCESS_VIEW_DESC desc = {};
        desc.Format = DXGI_FORMAT_UNKNOWN;
        desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        desc.Buffer.NumElements = tilesX * tilesY;
        desc.Buffer.StructureByteStride = sizeof(LightTileData);
        m_Awesome->Device()->CreateUnorderedAccessView(m_tileBuffer->resource, nullptr, &desc, m_handles[3].cpuHandle);
    }
}

void TileLightCull::FreeResources()
{
    m_Awesome->GetResourcePool()->Free(m_constants);
    m_Awesome->GetResourcePool()->Free(m_bvhBuffer);
    m_Awesome->GetResourcePool()->Free(m_tileBuffer);
    for (auto& handle : m_handles)
        m_Awesome->GetMainDescHeap()->Free(handle);
    m_handles.clear();
}

void AABB::Expand(const AABB& other)
{
    this->min.x = std::min(this->min.x, other.min.x);
    this->min.y = std::min(this->min.y, other.min.y);
    this->min.z = std::min(this->min.z, other.min.z);

    this->max.x = std::max(this->max.x, other.max.x);
    this->max.y = std::max(this->max.y, other.max.y);
    this->max.z = std::max(this->max.z, other.max.z);
}

bool AABB::Intersects(const AABB& other)
{
    bool x_overlap = this->max.x >= other.min.x && other.max.x >= this->min.x;
    bool y_overlap = this->max.y >= other.min.y && other.max.y >= this->min.y;
    bool z_overlap = this->max.z >= other.min.z && other.max.z >= this->min.z;

    return x_overlap && y_overlap && z_overlap;
}

AABB AABB::FromLight(Light light)
{
    AABB aabb = {};

    float radius = light.LightRadius();

    aabb.min.x = light.position.x - radius;
    aabb.min.y = light.position.y - radius;
    aabb.min.z = light.position.z - radius;
    aabb.max.x = light.position.x + radius;
    aabb.max.y = light.position.y + radius;
    aabb.max.z = light.position.z + radius;

    return aabb;
}
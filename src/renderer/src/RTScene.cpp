#include "RTScene.h"
#include "Awesome.h"
#include "Util.h"
#include <string.h>

using namespace Awesome;

RTScene::RTScene(AwesomeGraphics* Awesome) : m_Awesome(Awesome) {}
RTScene::~RTScene() {}

bool RTScene::StartUp()
{
    // Resources are sized and created lazily on the first Build() (we need the
    // prebuild info, which depends on the max primitive count).
    return true;
}

bool RTScene::TearDown()
{
    SafeRelease(m_blas);
    SafeRelease(m_tlas);
    SafeRelease(m_scratch);
    SafeRelease(m_instanceBuf);
    return true;
}

ID3D12Resource* RTScene::CreateUAVBuffer(uint64 size, D3D12_RESOURCE_STATES state, const wchar_t* name)
{
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = size;
    desc.Height = 1; desc.DepthOrArraySize = 1; desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    desc.Format = DXGI_FORMAT_UNKNOWN;
    return m_Awesome->CreateBuffer(desc, name, D3D12_HEAP_TYPE_DEFAULT, state);
}

void RTScene::Build(ID3D12Resource* aabbBuffer, uint32 count)
{
    ID3D12Device5* dev = m_Awesome->Device5();
    ID3D12GraphicsCommandList4* cl = m_Awesome->GetCommandList4();
    if (!dev || !cl || !aabbBuffer || count == 0)
        return;

    // --- BLAS: one procedural-primitive geometry over the macro AABBs ---
    D3D12_RAYTRACING_GEOMETRY_DESC geom = {};
    geom.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_PROCEDURAL_PRIMITIVE_AABBS;
    geom.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
    geom.AABBs.AABBCount = count;
    geom.AABBs.AABBs.StartAddress = aabbBuffer->GetGPUVirtualAddress();
    geom.AABBs.AABBs.StrideInBytes = sizeof(D3D12_RAYTRACING_AABB);

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS blasInputs = {};
    blasInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
    blasInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    blasInputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
    blasInputs.NumDescs = 1;
    blasInputs.pGeometryDescs = &geom;

    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO blasInfo = {};
    dev->GetRaytracingAccelerationStructurePrebuildInfo(&blasInputs, &blasInfo);

    // --- TLAS: single identity instance referencing the BLAS ---
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS tlasInputs = {};
    tlasInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    tlasInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    tlasInputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
    tlasInputs.NumDescs = 1;

    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO tlasInfo = {};
    dev->GetRaytracingAccelerationStructurePrebuildInfo(&tlasInputs, &tlasInfo);

    // (Re)allocate AS + scratch when the first build or when a bigger size is needed.
    if (!m_blas || blasInfo.ResultDataMaxSizeInBytes > m_blasSize)
    {
        SafeRelease(m_blas);
        m_blasSize = blasInfo.ResultDataMaxSizeInBytes;
        m_blas = CreateUAVBuffer(m_blasSize, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, L"Cloud BLAS");
    }
    if (!m_tlas || tlasInfo.ResultDataMaxSizeInBytes > m_tlasSize)
    {
        SafeRelease(m_tlas);
        m_tlasSize = tlasInfo.ResultDataMaxSizeInBytes;
        m_tlas = CreateUAVBuffer(m_tlasSize, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, L"Cloud TLAS");
    }
    uint64 needScratch = blasInfo.ScratchDataSizeInBytes > tlasInfo.ScratchDataSizeInBytes ? blasInfo.ScratchDataSizeInBytes : tlasInfo.ScratchDataSizeInBytes;
    if (!m_scratch || needScratch > m_scratchSize)
    {
        SafeRelease(m_scratch);
        m_scratchSize = needScratch;
        m_scratch = CreateUAVBuffer(m_scratchSize, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, L"Cloud AS Scratch");
    }
    if (!m_instanceBuf)
    {
        D3D12_RESOURCE_DESC idesc = {};
        idesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        idesc.Width = sizeof(D3D12_RAYTRACING_INSTANCE_DESC);
        idesc.Height = 1; idesc.DepthOrArraySize = 1; idesc.MipLevels = 1;
        idesc.SampleDesc.Count = 1;
        idesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        idesc.Format = DXGI_FORMAT_UNKNOWN;
        m_instanceBuf = m_Awesome->CreateBuffer(idesc, L"Cloud AS Instance", D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
    }
    if (!m_blas || !m_tlas || !m_scratch || !m_instanceBuf)
        return;

    // Fill the instance (identity transform, references the BLAS).
    D3D12_RAYTRACING_INSTANCE_DESC inst = {};
    inst.Transform[0][0] = 1.0f; inst.Transform[1][1] = 1.0f; inst.Transform[2][2] = 1.0f;
    inst.InstanceMask = 0xFF;
    inst.AccelerationStructure = m_blas->GetGPUVirtualAddress();
    void* mapped = nullptr;
    m_instanceBuf->Map(0, nullptr, &mapped);
    memcpy(mapped, &inst, sizeof(inst));
    m_instanceBuf->Unmap(0, nullptr);

    // Build BLAS.
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC blasDesc = {};
    blasDesc.Inputs = blasInputs;
    blasDesc.ScratchAccelerationStructureData = m_scratch->GetGPUVirtualAddress();
    blasDesc.DestAccelerationStructureData = m_blas->GetGPUVirtualAddress();
    cl->BuildRaytracingAccelerationStructure(&blasDesc, 0, nullptr);

    D3D12_RESOURCE_BARRIER uav = {};
    uav.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    uav.UAV.pResource = m_blas;
    cl->ResourceBarrier(1, &uav);

    // Build TLAS (reuses scratch after the barrier).
    tlasInputs.InstanceDescs = m_instanceBuf->GetGPUVirtualAddress();
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC tlasDesc = {};
    tlasDesc.Inputs = tlasInputs;
    tlasDesc.ScratchAccelerationStructureData = m_scratch->GetGPUVirtualAddress();
    tlasDesc.DestAccelerationStructureData = m_tlas->GetGPUVirtualAddress();
    cl->BuildRaytracingAccelerationStructure(&tlasDesc, 0, nullptr);

    D3D12_RESOURCE_BARRIER tuav = {};
    tuav.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    tuav.UAV.pResource = m_tlas;
    cl->ResourceBarrier(1, &tuav);

    m_built = true;
}

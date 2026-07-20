#include "MeshData.h"
#include "Awesome.h"
#include "Util.h"
#include <d3d12.h>
#include <d3dcompiler.h>
#include "pix3.h"

using namespace Awesome;

MeshSystem::MeshSystem(AwesomeGraphics* Awesome)
    : m_Awesome(Awesome)
    , m_meshElementDesc{}
    , m_meshLayoutDesc{}
{
    m_meshDataBlob.reserve(200);
    m_meshes.reserve(200);
    m_prims.reserve(20);
}

MeshSystem::~MeshSystem()
{
    m_Awesome = nullptr;
}

bool MeshSystem::StartUp()
{
    m_meshElementDesc =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TANGENT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 40, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT, 0, 48, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };

    // Layout
    m_meshLayoutDesc = {};
    m_meshLayoutDesc.NumElements = uint32(m_meshElementDesc.size());
    m_meshLayoutDesc.pInputElementDescs = &m_meshElementDesc.front();

    return true;
}

bool MeshSystem::TearDown()
{
    for (uint32 i = 0; i < m_meshes.size(); ++i)
    {
        SafeRelease(m_meshes[i].m_vertexBuffer);
        SafeRelease(m_meshes[i].m_indexBuffer);
    }
    m_meshes.clear();
    m_meshDataBlob.clear();
    return true;
}

void MeshSystem::Render(const uint32 instance, const uint32 index, const Transform& xform, const XMMATRIX viewProj, const XMMATRIX prevViewProj, const  XMMATRIX jitter)
{
    // skip decals for now
    if (m_meshDataBlob[index].name.find("decal") != std::string::npos)
        return;

    // Skip invidible meshes
    if (!m_meshDataBlob[index].visible)
        return;

    // Draw!
    PIXScopedEvent(m_Awesome->GetCommandList(), 0, m_meshDataBlob[index].name.c_str());

    auto p = xform.GetTransformationMatrix();
    auto m = m_meshDataBlob[index].transform.GetTransformationMatrix();
    auto w = m * p;
    m_Awesome->GetMaterialSystem()->SetupMaterial(instance, m_meshes[index].m_materialIndex, w, viewProj, prevViewProj, jitter);

    m_Awesome->GetCommandList()->IASetVertexBuffers(0, 1, &m_meshes[index].m_vertexBufferView);
    m_Awesome->GetCommandList()->IASetIndexBuffer(&m_meshes[index].m_indexBufferView);
    m_Awesome->GetCommandList()->DrawIndexedInstanced(m_meshes[index].m_indexCount, 1, 0, 0, 0);
}

void MeshSystem::RenderPrim(const uint32 instance, const uint32 index, const Transform& xform, const XMMATRIX viewProj, const XMMATRIX prevViewProj, const  XMMATRIX jitter)
{

    uint32 meshIdx = m_prims[index];
    // Draw!
    PIXScopedEvent(m_Awesome->GetCommandList(), 0, m_meshDataBlob[meshIdx].name.c_str());

    auto p = xform.GetTransformationMatrix();

    m_Awesome->GetMaterialSystem()->SetupMaterial(instance, m_meshes[meshIdx].m_materialIndex, p, viewProj, prevViewProj, jitter);

    m_Awesome->GetCommandList()->IASetVertexBuffers(0, 1, &m_meshes[meshIdx].m_vertexBufferView);
    m_Awesome->GetCommandList()->IASetIndexBuffer(&m_meshes[meshIdx].m_indexBufferView);
    m_Awesome->GetCommandList()->DrawIndexedInstanced(m_meshes[meshIdx].m_indexCount, 1, 0, 0, 0);
}

void MeshSystem::RenderShadows(const uint32 instance, const uint32 index, const uint32 cascade, const XMMATRIX xform, const XMMATRIX viewProj)
{
    // skip decals for now
    if (m_meshDataBlob[index].name.find("decal") != std::string::npos)
        return;

    // Draw!
    PIXScopedEvent(m_Awesome->GetCommandList(), 0, m_meshDataBlob[index].name.c_str());

    XMMATRIX m = m_meshDataBlob[index].transform.GetTransformationMatrix();
    m =  (m * xform) * viewProj;

    m_Awesome->GetMaterialSystem()->SetupShadow(instance, m_meshes[index].m_materialIndex, cascade, m);

    m_Awesome->GetCommandList()->IASetVertexBuffers(0, 1, &m_meshes[index].m_vertexBufferView);
    m_Awesome->GetCommandList()->IASetIndexBuffer(&m_meshes[index].m_indexBufferView);
    m_Awesome->GetCommandList()->DrawIndexedInstanced(m_meshes[index].m_indexCount, 1, 0, 0, 0);
}

void MeshSystem::SetupRender()
{
    m_Awesome->GetCommandList()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

uint32 MeshSystem::CreateMesh(MeshData& inMesh, bool prim)
{
    m_meshes.emplace_back();
    Mesh& m = m_meshes.back();
    m.m_data = uint32(m_meshDataBlob.size());

    m_meshDataBlob.emplace_back(inMesh);

    m.m_indexCount = uint32(inMesh.indices.size());
    DebugPrint("Creating Mesh %s.\n", m_meshDataBlob.back().name.c_str());

    m_Awesome->BeginFrame(false);
    // Vertex Buffer
    uint32 vBufferSize = uint32(inMesh.vertices.size()) * sizeof(VertexData);
    {
        D3D12_RESOURCE_DESC resDesc = {};
        resDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        resDesc.Alignment = 0;
        resDesc.Width = vBufferSize;
        resDesc.Height = 1;
        resDesc.DepthOrArraySize = 1;
        resDesc.MipLevels = 1;
        resDesc.Format = DXGI_FORMAT_UNKNOWN;
        resDesc.SampleDesc.Count = 1;
        resDesc.SampleDesc.Quality = 0;
        resDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        resDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

        wchar_t name[512];
        swprintf(name, 512, L"%S Vertex Buffer", m_meshDataBlob.back().name.c_str());

        m.m_vertexBuffer = m_Awesome->CreateBuffer(resDesc, name, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COMMON);

        const uint8* srcData = reinterpret_cast<uint8*>(&inMesh.vertices.front());
        m_Awesome->UploadBuffer(m.m_vertexBuffer, resDesc, vBufferSize, srcData);

        m_Awesome->TransitionResource(m.m_vertexBuffer, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
    }

    // Index Buffer
    uint32 iBbufferSize = uint32(inMesh.indices.size()) * sizeof(uint32);
    {
        D3D12_RESOURCE_DESC resDesc = {};
        resDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        resDesc.Alignment = 0;
        resDesc.Width = iBbufferSize;
        resDesc.Height = 1;
        resDesc.DepthOrArraySize = 1;
        resDesc.MipLevels = 1;
        resDesc.Format = DXGI_FORMAT_UNKNOWN;
        resDesc.SampleDesc.Count = 1;
        resDesc.SampleDesc.Quality = 0;
        resDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        resDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

        wchar_t name[512];
        swprintf(name, 512, L"%S Index Buffer", m_meshDataBlob.back().name.c_str());
        m.m_indexBuffer = m_Awesome->CreateBuffer(resDesc, name, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COMMON);

        const uint8* srcData = reinterpret_cast<uint8*>(&inMesh.indices.front());
        m_Awesome->UploadBuffer(m.m_indexBuffer, resDesc, iBbufferSize, srcData);
        m_Awesome->TransitionResource(m.m_indexBuffer, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_INDEX_BUFFER);
    }
    m_Awesome->EndFrame(false);

    m.m_vertexBufferView.BufferLocation = m.m_vertexBuffer->GetGPUVirtualAddress();
    m.m_vertexBufferView.StrideInBytes = sizeof(VertexData);
    m.m_vertexBufferView.SizeInBytes = vBufferSize;

    m.m_indexBufferView.BufferLocation = m.m_indexBuffer->GetGPUVirtualAddress();
    m.m_indexBufferView.Format = DXGI_FORMAT_R32_UINT;
    m.m_indexBufferView.SizeInBytes = iBbufferSize;

    m.m_materialIndex = inMesh.materialIndex;

    if (prim)
        m_prims.push_back(m.m_data);

    return m.m_data;
}

const D3D12_INPUT_LAYOUT_DESC& MeshSystem::GetMeshLayout()
{
    return m_meshLayoutDesc;
}

MeshData* MeshSystem::GetMeshData(uint32 index)
{
    if (index < m_meshDataBlob.size())
        return &m_meshDataBlob[index];
    return nullptr;
}
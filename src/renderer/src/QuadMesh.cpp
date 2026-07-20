#include "QuadMesh.h"
#include "Util.h"
#include <DirectXMath.h>

using namespace Awesome;
using namespace DirectX;

QuadMesh::QuadMesh(AwesomeGraphics* Awesome)
	: m_Awesome(Awesome)
	, m_vertexBuffer(nullptr)
	, m_indexBuffer(nullptr)
    , m_vertexBufferView{}
    , m_indexBufferView{}
    , m_meshElement{}
    , m_meshLayout{}
{
    m_meshElement[0] = { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 };
    m_meshElement[1] = { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 };

    m_meshLayout.NumElements = _countof(m_meshElement);
    m_meshLayout.pInputElementDescs = m_meshElement;

}

QuadMesh::~QuadMesh()
{
    TearDown();
}

bool QuadMesh::StartUp()
{
    m_Awesome->BeginFrame(false);

    QuadMeshVert verts[] = {
        { {-1.0f,-1.0f, 0.0f }, {0,1} },
        { {-1.0f, 1.0f, 0.0f }, {0,0} },
        { { 1.0f, 1.0f, 0.0f }, {1,0} },
        { { 1.0f,-1.0f, 0.0f }, {1,1} },
    };

    uint32 indices[] = {0,1,2, 2,3,0};

    // Vertex Buffer
    uint32 vBufferSize = uint32(_countof(verts)) * sizeof(QuadMeshVert);
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

        m_vertexBuffer = m_Awesome->CreateBuffer(resDesc, L"Quad Mesh Vertex Buffer", D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COMMON);

        const uint8* srcData = reinterpret_cast<uint8*>(verts);
        m_Awesome->UploadBuffer(m_vertexBuffer, resDesc, vBufferSize, srcData);

        m_Awesome->TransitionResource(m_vertexBuffer, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);

    }

    // Index Buffer
    uint32 iBbufferSize = uint32(_countof(indices)) * sizeof(uint32);
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

        m_indexBuffer = m_Awesome->CreateBuffer(resDesc, L"Quad Mesh Index Buffer", D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COMMON);

        const uint8* srcData = reinterpret_cast<uint8*>(indices);
        m_Awesome->UploadBuffer(m_indexBuffer, resDesc, iBbufferSize, srcData);
        m_Awesome->TransitionResource(m_indexBuffer, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_INDEX_BUFFER);

    }
    m_Awesome->EndFrame(false);

    m_vertexBufferView.BufferLocation = m_vertexBuffer->GetGPUVirtualAddress();
    m_vertexBufferView.StrideInBytes = sizeof(QuadMeshVert);
    m_vertexBufferView.SizeInBytes = vBufferSize;

    m_indexBufferView.BufferLocation = m_indexBuffer->GetGPUVirtualAddress();
    m_indexBufferView.Format = DXGI_FORMAT_R32_UINT;
    m_indexBufferView.SizeInBytes = iBbufferSize;

	return true;
}

bool QuadMesh::TearDown()
{
	SafeRelease(m_vertexBuffer);
	SafeRelease(m_indexBuffer);
	return true;
}

void QuadMesh::PreRender()
{
    m_Awesome->GetCommandList()->IASetVertexBuffers(0, 1, &m_vertexBufferView);
    m_Awesome->GetCommandList()->IASetIndexBuffer(&m_indexBufferView);
}

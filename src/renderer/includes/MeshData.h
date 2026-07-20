#pragma once

#include <d3d12.h>
#include <vector>
#include <DirectXMath.h>
#include <string>

#include "types.h"

#include "Transform.h"
#include "Material.h"

using namespace DirectX;

namespace Awesome
{

    class AwesomeGraphics;

    struct VertexData
    {
        XMFLOAT3 position;
        XMFLOAT3 normal;
        XMFLOAT4 tangent;
        XMFLOAT2 uv[2];
    };

    struct MeshData
    {
        std::string name;
        uint32 materialIndex;
        bool visible;
        Transform transform;
        std::vector<VertexData> vertices;
        std::vector<uint32> indices;
    };

    struct Mesh
    {
        uint32 m_data;
        uint32 m_indexCount;
        ID3D12Resource* m_vertexBuffer;
        ID3D12Resource* m_indexBuffer;
        D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView;
        D3D12_INDEX_BUFFER_VIEW m_indexBufferView;
        uint32 m_materialIndex;
    };

    class MeshSystem
    {
    public:
        MeshSystem(AwesomeGraphics* Awesome);
        ~MeshSystem();
        bool StartUp();
        bool TearDown();
        void Render(const uint32 instance, const uint32 index, const Transform& xform, const XMMATRIX viewProj, const XMMATRIX preViewProj, const  XMMATRIX jitter);
        void RenderPrim(const uint32 instance, const uint32 index, const Transform& xform, const XMMATRIX viewProj, const XMMATRIX preViewProj, const  XMMATRIX jitter);
        void RenderShadows(const uint32 instance, const uint32 index, const uint32 cascade, const XMMATRIX xform, const XMMATRIX viewProj);
        void SetupRender();
        uint32 CreateMesh(MeshData& inMesh, bool prim);
        const D3D12_INPUT_LAYOUT_DESC& GetMeshLayout();
        MeshData* GetMeshData(uint32 index);

    private:
        AwesomeGraphics* m_Awesome;
        //MaterialSystem* m_materialSystem;

        std::vector<Mesh> m_meshes;
        std::vector<MeshData> m_meshDataBlob;

        std::vector<D3D12_INPUT_ELEMENT_DESC> m_meshElementDesc;

        D3D12_INPUT_LAYOUT_DESC m_meshLayoutDesc;
        
        std::vector<uint32> m_prims;

    };
};
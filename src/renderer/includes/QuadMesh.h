#pragma once
#include "types.h"
#include "Awesome.h"
#include <DirectXMath.h>

namespace Awesome
{
    using namespace DirectX;

    struct QuadMeshVert
    {
        XMFLOAT3 position;
        XMFLOAT2 uv;
    };

    class QuadMesh
    {
    public:

        QuadMesh(AwesomeGraphics* Awesome);
        ~QuadMesh();
        bool StartUp();
        bool TearDown();
        void PreRender();
        const D3D12_INPUT_LAYOUT_DESC& GetQuadMeshLayout() { return m_meshLayout; }

    private:
        AwesomeGraphics* m_Awesome;
        ID3D12Resource* m_vertexBuffer;
        ID3D12Resource* m_indexBuffer;
        D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView;
        D3D12_INDEX_BUFFER_VIEW m_indexBufferView;
        D3D12_INPUT_ELEMENT_DESC m_meshElement[2];
        D3D12_INPUT_LAYOUT_DESC m_meshLayout;
    };
};
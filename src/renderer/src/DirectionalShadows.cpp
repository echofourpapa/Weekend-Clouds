#include "DirectionalShadows.h"
#include "Util.h"
#include "Compute.h"
#include "Material.h"
#include "Deferred.h"
#include "Scene.h"
#include "ResourcePool.h"

/*
https://learn.microsoft.com/en-us/windows/win32/dxtecharts/cascaded-shadow-maps
https://gen-graphics.blogspot.com/2018/07/implementing-cascaded-shadow-maps-in.html

*/

using namespace Awesome;

DirectionalShadows::DirectionalShadows(AwesomeGraphics* Awesome)
	: m_Awesome(Awesome)
    , m_castShadowsDsDescHeap(nullptr)
	, m_castShadowsOutput{ nullptr }
    , m_settings{}
{
}

DirectionalShadows::~DirectionalShadows()
{
}

bool DirectionalShadows::StartUp()
{
    // Create Depth/Stencil Buffer Descriptor Heap
    {
        // create a depth stencil descriptor heap so we can get a pointer to the depth stencil buffer
        D3D12_DESCRIPTOR_HEAP_DESC desc = {};
        desc.NumDescriptors = c_frameBufferCount * c_maxCascadeCount;
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        if (FAILED(m_Awesome->Device()->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_castShadowsDsDescHeap))))
            return false;

        m_castShadowsDsDescHeap->SetName(L"Depth/Stencil Resource Heap");
    }

    // Just set a default
    InitResources();

	return true;
}

bool DirectionalShadows::TearDown()
{
    SafeRelease(m_castShadowsDsDescHeap);
    for(uint32 i = 0; i < c_frameBufferCount * c_maxCascadeCount; i++)
	    SafeRelease(m_castShadowsOutput[i]);
	return true;
}

void DirectionalShadows::Resize(uint32 size)
{
    if (size == 0)
    {
        DebugPrint("Can't create shadow map of size %d\n", size);
        return;
    }
    m_settings.size = size;
    for (uint32 i = 0; i < c_frameBufferCount * c_maxCascadeCount; i++)
        SafeRelease(m_castShadowsOutput[i]);
    InitResources();
}

void DirectionalShadows::PreRender()
{
    // Fill out the Viewport
    D3D12_VIEWPORT viewport;
    viewport.TopLeftX = 0;
    viewport.TopLeftY = 0;
    viewport.Width = (float)m_settings.size;
    viewport.Height = (float)m_settings.size;
    viewport.MinDepth = D3D12_MIN_DEPTH;
    viewport.MaxDepth = D3D12_MAX_DEPTH;

    // Fill out a scissor rect
    D3D12_RECT scissorRect;
    scissorRect.left = 0;
    scissorRect.top = 0;
    scissorRect.right = m_settings.size;
    scissorRect.bottom = m_settings.size;

    m_Awesome->GetCommandList()->RSSetViewports(1, &viewport);
    m_Awesome->GetCommandList()->RSSetScissorRects(1, &scissorRect);
}

void DirectionalShadows::SetupRender(uint32 cascade)
{
    PIXScopedEvent(m_Awesome->GetCommandList(), 0, "Setup Cascade");

    auto handle = m_castShadowsDsDescHeap->GetCPUDescriptorHandleForHeapStart();
    uint32 offset = m_Awesome->Device()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    handle.ptr += (cascade + m_Awesome->GetCurrentFrameIndex() * m_settings.cascades) * offset;
    m_Awesome->GetCommandList()->OMSetRenderTargets(0, nullptr, true, &handle);
    m_Awesome->GetCommandList()->ClearDepthStencilView(handle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
}

ID3D12Resource* DirectionalShadows::GetOutputBuffer()
{
    return m_castShadowsOutput[m_Awesome->GetCurrentFrameIndex()];
}

XMMATRIX DirectionalShadows::GetCascadeMatrix(uint32 cascade, ViewFrustum frustum, Light* light)
{
    // https://gen-graphics.blogspot.com/2018/07/implementing-cascaded-shadow-maps-in.html

    XMFLOAT3 frustumCorners[8] = {};
    frustumCorners[0] = frustum.nearPoints[0];
    frustumCorners[1] = frustum.nearPoints[1];
    frustumCorners[2] = frustum.nearPoints[2];
    frustumCorners[3] = frustum.nearPoints[3];
    frustumCorners[4] = frustum.farPoints[0];
    frustumCorners[5] = frustum.farPoints[1];
    frustumCorners[6] = frustum.farPoints[2];
    frustumCorners[7] = frustum.farPoints[3];

    XMFLOAT3 frustumCenter = {};

    for (size_t i = 0; i < 8; i++)
    {
        frustumCenter.x += frustumCorners[i].x;
        frustumCenter.y += frustumCorners[i].y;
        frustumCenter.z += frustumCorners[i].z;
    }

    //calculate frustum's center position
    frustumCenter.x /= 8.0f;
    frustumCenter.y /= 8.0f;
    frustumCenter.z /= 8.0f;

    float radius = 0.0f;
    XMVECTOR centerV = XMLoadFloat3(&frustumCenter);
    for (int i = 0; i < 8; i++)
    {
        XMVECTOR corner = XMLoadFloat3(&frustumCorners[i]);
        XMVECTOR dist = XMVector3Length(corner - centerV);
        float d;
        XMStoreFloat(&d, dist);
        radius = std::max(radius, d);
    }
    float texelUnits = (float)m_settings.size / (radius * 2.0f);

    XMMATRIX scalar = XMMatrixScaling(texelUnits, texelUnits, texelUnits);

    XMVECTOR center = XMLoadFloat3(&frustumCenter);
    XMVECTOR eye = XMLoadFloat3(&light->position);
    XMVECTOR up = XMLoadFloat3(&Transform::Up);
    XMVECTOR zero = XMLoadFloat3(&Transform::Zero);
    XMVECTOR worldCenter = XMVectorZero();

    XMMATRIX viewMatrix = scalar * XMMatrixLookAtLH(worldCenter + eye, worldCenter, up);

    center = XMVector3Transform(center, viewMatrix);
    XMStoreFloat3(&frustumCenter, center);
    frustumCenter.x = floorf(frustumCenter.x);
    frustumCenter.y = floorf(frustumCenter.y);
    center = XMLoadFloat3(&frustumCenter);

    XMMATRIX invView = XMMatrixInverse(nullptr, scalar * XMMatrixLookAtLH(eye, zero, up));
    center = XMVector3Transform(center, invView);

    viewMatrix = XMMatrixLookAtLH(worldCenter + eye, worldCenter, up);

    // I don't want to lose this, but also don't want to use it either.
#if 0
    float minX = (std::numeric_limits<float>::max)();
    float maxX = (std::numeric_limits<float>::min)();
    float minY = (std::numeric_limits<float>::max)();
    float maxY = (std::numeric_limits<float>::min)();
    float minZ = (std::numeric_limits<float>::max)();
    float maxZ = (std::numeric_limits<float>::min)();

    for (uint32 i = 0; i < 8; i++) {
        // Transform the frustum coordinate from world to light space
        XMVECTOR frustumCornerVector = XMLoadFloat3(&frustumCorners[i]);
        frustumCornerVector = XMVector3Transform(frustumCornerVector, viewMatrix);

        XMStoreFloat3(&frustumCorners[i], frustumCornerVector);

        minX = std::min(minX, frustumCorners[i].x);
        maxX = std::max(maxX, frustumCorners[i].x);
        minY = std::min(minY, frustumCorners[i].y);
        maxY = std::max(maxY, frustumCorners[i].y);
        minZ = std::min(minZ, frustumCorners[i].z);
        maxZ = std::max(maxZ, frustumCorners[i].z);
    }

    XMMATRIX projMatrix = XMMatrixOrthographicLH(maxX - minX, maxY - minY, minZ-100, maxZ);
#else
    float fixedNear = -200.0f;
    float fixedFar = 200.0f;
    XMMATRIX projMatrix = XMMatrixOrthographicOffCenterLH(-radius, radius, -radius, radius, fixedNear , fixedFar );
#endif

    XMMATRIX viewProjMatrix = viewMatrix * projMatrix;
    return viewProjMatrix;
}

void DirectionalShadows::SetSettings(DirectionalShadowSettings settings) 
{
    bool rebuild = (m_settings.size != settings.size || m_settings.cascades != settings.cascades);

    m_settings = settings;

    if (rebuild)
    {
        for (uint32 i = 0; i < c_frameBufferCount * c_maxCascadeCount; i++)
            SafeRelease(m_castShadowsOutput[i]);
        InitResources();
    }
}

void DirectionalShadows::InitResources()
{
    auto handle = m_castShadowsDsDescHeap->GetCPUDescriptorHandleForHeapStart();
    uint32 offset = m_Awesome->Device()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    // Cast Shadow Output Buffer
    for (uint32 f = 0; f < c_frameBufferCount; f++)
    {
        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Alignment = 0;
        desc.Width = m_settings.size;
        desc.Height = m_settings.size;
        desc.DepthOrArraySize = m_settings.cascades;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_D32_FLOAT;
        desc.SampleDesc.Count = 1;
        desc.SampleDesc.Quality = 0;
        desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

        D3D12_CLEAR_VALUE depthOptimizedClearValue = {};
        depthOptimizedClearValue.Format = DXGI_FORMAT_D32_FLOAT;
        depthOptimizedClearValue.DepthStencil.Depth = 1.0f;
        depthOptimizedClearValue.DepthStencil.Stencil = 0;

        wchar_t name[256];
        swprintf(name, 256, L"Directional Shadows Buffer Frame %d", f);

        m_castShadowsOutput[f] = m_Awesome->CreateBuffer(desc, name, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_DEPTH_WRITE, &depthOptimizedClearValue);

        for (uint32 c = 0; c < m_settings.cascades; c++) // lol
        {
            D3D12_DEPTH_STENCIL_VIEW_DESC depthStencilDesc = {};
            depthStencilDesc.Format = DXGI_FORMAT_D32_FLOAT;
            depthStencilDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
            depthStencilDesc.Flags = D3D12_DSV_FLAG_NONE;
            depthStencilDesc.Texture2DArray.ArraySize = 1;
            depthStencilDesc.Texture2DArray.FirstArraySlice = c;
            depthStencilDesc.Texture2DArray.MipSlice = 0;

            m_Awesome->Device()->CreateDepthStencilView(m_castShadowsOutput[f], &depthStencilDesc, handle);
            handle.ptr += offset;
        }
    }
}
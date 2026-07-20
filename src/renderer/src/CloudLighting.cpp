#include "CloudLighting.h"
#include "CloudSystem.h"
#include "Awesome.h"
#include "Compute.h"
#include "Util.h"

using namespace Awesome;

CloudLighting::CloudLighting(AwesomeGraphics* Awesome, CloudSystem* clouds)
    : m_Awesome(Awesome), m_clouds(clouds) {}
CloudLighting::~CloudLighting() {}

bool CloudLighting::StartUp()
{
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
    desc.Width = c_dimX;
    desc.Height = c_dimY;
    desc.DepthOrArraySize = c_dimZ;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    desc.Format = DXGI_FORMAT_R16_FLOAT;
    m_cache = m_Awesome->CreateBuffer(desc, L"Cloud Light Cache", D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    if (!m_cache) return false;

    {
        D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
        srv.Format = DXGI_FORMAT_R16_FLOAT;
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
        srv.Texture3D.MipLevels = 1;
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        m_clouds->WriteSRV(SRV_LightCache, m_cache, &srv);

        D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
        uav.Format = DXGI_FORMAT_R16_FLOAT;
        uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D;
        uav.Texture3D.WSize = c_dimZ;
        m_clouds->WriteUAV(UAV_LightCache, m_cache, &uav);
    }

    std::vector<D3D_SHADER_MACRO*> perms;
    perms.push_back(new D3D_SHADER_MACRO[1]{ { NULL, NULL } });
    uint32 shader = m_Awesome->GetComputeSystem()->CompileShader(L"CloudLightCache-c", perms);
    if (shader == invalidIndex32) return false;
    m_buildPSO = m_Awesome->GetComputeSystem()->CreatePipeline(shader, m_clouds->GetRootSignature());
    return m_buildPSO != (uint32)-1;
}

bool CloudLighting::TearDown()
{
    SafeRelease(m_cache);
    return true;
}

void CloudLighting::Build()
{
    ID3D12GraphicsCommandList* cl = m_Awesome->GetCommandList();
    m_Awesome->TransitionResource(m_cache, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    m_Awesome->GetComputeSystem()->SetPSO(m_buildPSO);
    cl->Dispatch(c_dimX / 4, c_dimY / 4, c_slabZ / 4);
    m_Awesome->TransitionResource(m_cache, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
}

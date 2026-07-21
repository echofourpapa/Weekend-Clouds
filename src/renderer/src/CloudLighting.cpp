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
    desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;   // sun uses .r; six-way uses all four
    m_cache = m_Awesome->CreateBuffer(desc, L"Cloud Light Cache", D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    m_cache1 = m_Awesome->CreateBuffer(desc, L"Cloud Light Cache Z", D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    if (!m_cache || !m_cache1) return false;

    {
        D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
        srv.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
        srv.Texture3D.MipLevels = 1;
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        m_clouds->WriteSRV(SRV_LightCache, m_cache, &srv);   // t8
        m_clouds->WriteSRV(SRV_Weather, m_cache1, &srv);     // t3 (six-way +Z,-Z)

        D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
        uav.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D;
        uav.Texture3D.WSize = c_dimZ;
        m_clouds->WriteUAV(UAV_LightCache, m_cache, &uav);   // u6
        m_clouds->WriteUAV(UAV_Weather, m_cache1, &uav);     // u3
    }

    // Canonical scattering transfer table (2D LUT, baked once on the first frame).
    m_scatterLUT = m_clouds->CreateTex2D(64, 64, DXGI_FORMAT_R16G16B16A16_FLOAT, L"Cloud Scatter LUT");
    if (!m_scatterLUT) return false;
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
        srv.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Texture2D.MipLevels = 1;
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        m_clouds->WriteSRV(SRV_BlueNoise, m_scatterLUT, &srv);   // t7
        D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
        uav.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        m_clouds->WriteUAV(UAV_Kernel, m_scatterLUT, &uav);      // u0
    }

    std::vector<D3D_SHADER_MACRO*> perms;
    perms.push_back(new D3D_SHADER_MACRO[1]{ { NULL, NULL } });
    uint32 shader = m_Awesome->GetComputeSystem()->CompileShader(L"CloudLightCache-c", perms);
    uint32 bake = m_Awesome->GetComputeSystem()->CompileShader(L"CloudScatterBake-c", perms);
    if (shader == invalidIndex32 || bake == invalidIndex32) return false;
    m_buildPSO = m_Awesome->GetComputeSystem()->CreatePipeline(shader, m_clouds->GetRootSignature());
    m_scatterBakePSO = m_Awesome->GetComputeSystem()->CreatePipeline(bake, m_clouds->GetRootSignature());
    return m_buildPSO != (uint32)-1 && m_scatterBakePSO != (uint32)-1;
}

bool CloudLighting::TearDown()
{
    SafeRelease(m_cache);
    SafeRelease(m_cache1);
    SafeRelease(m_scatterLUT);
    return true;
}

void CloudLighting::Build()
{
    ID3D12GraphicsCommandList* cl = m_Awesome->GetCommandList();

    // One-time canonical scattering LUT bake (needs an open command list + bound tables).
    if (!m_scatterBaked)
    {
        m_Awesome->TransitionResource(m_scatterLUT, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        m_Awesome->GetComputeSystem()->SetPSO(m_scatterBakePSO);
        cl->Dispatch(8, 8, 1);   // 64x64 / 8
        m_Awesome->TransitionResource(m_scatterLUT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        m_scatterBaked = true;
    }

    m_Awesome->TransitionResource(m_cache, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    m_Awesome->TransitionResource(m_cache1, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    m_Awesome->GetComputeSystem()->SetPSO(m_buildPSO);
    cl->Dispatch(c_dimX / 4, c_dimY / 4, c_slabZ / 4);
    m_Awesome->TransitionResource(m_cache, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    m_Awesome->TransitionResource(m_cache1, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
}

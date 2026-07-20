#include "SkyAtmosphere.h"
#include "CloudSystem.h"
#include "Awesome.h"
#include "Compute.h"
#include "Util.h"

using namespace Awesome;

SkyAtmosphere::SkyAtmosphere(AwesomeGraphics* Awesome, CloudSystem* clouds)
    : m_Awesome(Awesome)
    , m_clouds(clouds)
{
}

SkyAtmosphere::~SkyAtmosphere()
{
}

bool SkyAtmosphere::StartUp()
{
    m_transLUT = m_clouds->CreateTex2D(c_transW, c_transH, DXGI_FORMAT_R11G11B10_FLOAT, L"Sky Transmittance LUT");
    m_skyViewLUT = m_clouds->CreateTex2D(c_viewW, c_viewH, DXGI_FORMAT_R11G11B10_FLOAT, L"Sky View LUT");
    if (!m_transLUT || !m_skyViewLUT)
        return false;

    std::vector<D3D_SHADER_MACRO*> perms;
    D3D_SHADER_MACRO* def = new D3D_SHADER_MACRO[1]{ { NULL, NULL } };
    perms.push_back(def);

    uint32 transShader = m_Awesome->GetComputeSystem()->CompileShader(L"SkyTransLUT-c", perms);
    uint32 viewShader = m_Awesome->GetComputeSystem()->CompileShader(L"SkyViewLUT-c", perms);
    if (transShader == invalidIndex32 || viewShader == invalidIndex32)
        return false;
    m_transPSO = m_Awesome->GetComputeSystem()->CreatePipeline(transShader, m_clouds->GetRootSignature());
    m_viewPSO = m_Awesome->GetComputeSystem()->CreatePipeline(viewShader, m_clouds->GetRootSignature());
    if (m_transPSO == (uint32)-1 || m_viewPSO == (uint32)-1)
        return false;

    // Register persistent views into the shared tables (t4/u4 trans, t5/u5 view).
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
        srv.Format = DXGI_FORMAT_R11G11B10_FLOAT;
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Texture2D.MipLevels = 1;
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        m_clouds->WriteSRV(SRV_SkyTrans, m_transLUT, &srv);
        m_clouds->WriteSRV(SRV_SkyView, m_skyViewLUT, &srv);

        D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
        uav.Format = DXGI_FORMAT_R11G11B10_FLOAT;
        uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        m_clouds->WriteUAV(UAV_SkyTrans, m_transLUT, &uav);
        m_clouds->WriteUAV(UAV_SkyView, m_skyViewLUT, &uav);
    }

    return true;
}

bool SkyAtmosphere::TearDown()
{
    SafeRelease(m_transLUT);
    SafeRelease(m_skyViewLUT);
    return true;
}

void SkyAtmosphere::Render()
{
    // Regenerate only when the sun/turbidity changed (CPU-side dirty flag).
    if (!m_dirty)
        return;
    m_dirty = false;

    ID3D12GraphicsCommandList* cl = m_Awesome->GetCommandList();

    // Pass 0: transmittance LUT (u4). Resource NON_PIXEL -> UAV -> NON_PIXEL.
    m_Awesome->TransitionResource(m_transLUT, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    m_Awesome->GetComputeSystem()->SetPSO(m_transPSO);
    cl->Dispatch((c_transW + 7) / 8, (c_transH + 7) / 8, 1);
    m_Awesome->TransitionResource(m_transLUT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    // Pass 1: sky-view LUT (u5), reads transmittance (t4).
    m_Awesome->TransitionResource(m_skyViewLUT, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    m_Awesome->GetComputeSystem()->SetPSO(m_viewPSO);
    cl->Dispatch((c_viewW + 7) / 8, (c_viewH + 7) / 8, 1);
    m_Awesome->TransitionResource(m_skyViewLUT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
}

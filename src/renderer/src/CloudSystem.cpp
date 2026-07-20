#include "CloudSystem.h"
#include "CloudShaderCompiler.h"
#include "SkyAtmosphere.h"
#include "CloudGenerator.h"
#include "CloudLighting.h"
#include "Awesome.h"
#include "Compute.h"
#include "Deferred.h"
#include "Scene.h"
#include "Util.h"

using namespace Awesome;
using namespace DirectX;

static const uint32 c_cloudSrvSlots = SRV_Count;   // 15
static const uint32 c_cloudUavSlots = UAV_Count;   // 15

CloudSystem::CloudSystem(AwesomeGraphics* Awesome)
    : m_Awesome(Awesome)
    , m_shaderCompiler(new CloudShaderCompiler(Awesome))
    , m_sky(new SkyAtmosphere(Awesome, this))
    , m_generator(new CloudGenerator(Awesome, this))
    , m_lighting(new CloudLighting(Awesome, this))
{
}

CloudSystem::~CloudSystem()
{
    delete m_lighting;
    delete m_generator;
    delete m_sky;
    delete m_shaderCompiler;
}

uint32 CloudSystem::CurBlock() const
{
    return m_Awesome->GetCurrentFrameIndex();
}

ID3D12Resource* CloudSystem::CreateTex2D(uint32 w, uint32 h, DXGI_FORMAT fmt, const wchar_t* name)
{
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = w;
    desc.Height = h;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    desc.Format = fmt;
    return m_Awesome->CreateBuffer(desc, name, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
}

void CloudSystem::WriteSRV(uint32 slot, ID3D12Resource* res, const D3D12_SHADER_RESOURCE_VIEW_DESC* desc)
{
    for (uint32 f = 0; f < c_frameBufferCount; ++f)
        for (uint32 p = 0; p < 2; ++p)
            m_Awesome->Device()->CreateShaderResourceView(res, desc, m_srvBlocks[f][p][slot].cpuHandle);
}

void CloudSystem::WriteUAV(uint32 slot, ID3D12Resource* res, const D3D12_UNORDERED_ACCESS_VIEW_DESC* desc)
{
    for (uint32 f = 0; f < c_frameBufferCount; ++f)
        for (uint32 p = 0; p < 2; ++p)
            m_Awesome->Device()->CreateUnorderedAccessView(res, nullptr, desc, m_uavBlocks[f][p][slot].cpuHandle);
}

void CloudSystem::FillNullDescriptors()
{
    D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
    srv.Format = DXGI_FORMAT_R32_FLOAT;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Texture2D.MipLevels = 1;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

    D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
    uav.Format = DXGI_FORMAT_R32_FLOAT;
    uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;

    for (uint32 s = 0; s < c_cloudSrvSlots; ++s)
        WriteSRV(s, m_nullTex, &srv);
    for (uint32 s = 0; s < c_cloudUavSlots; ++s)
        WriteUAV(s, m_nullTex, &uav);
}

bool CloudSystem::StartUp()
{
    m_shaderCompiler->StartUp();

    // Shared root signature (docs/PLAN.md 3.5).
    {
        D3D12_DESCRIPTOR_RANGE srvRange = {};
        srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srvRange.NumDescriptors = c_cloudSrvSlots;
        srvRange.BaseShaderRegister = 0;
        srvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_DESCRIPTOR_RANGE uavRange = {};
        uavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        uavRange.NumDescriptors = c_cloudUavSlots;
        uavRange.BaseShaderRegister = 0;
        uavRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_ROOT_PARAMETER params[4] = {};
        params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        params[0].Descriptor = { 0, 0 };
        params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[1].DescriptorTable = { 1, &srvRange };
        params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[2].DescriptorTable = { 1, &uavRange };
        params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        params[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;   // TLAS (RQ paths)
        params[3].Descriptor = { 15, 0 };
        params[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        D3D12_ROOT_SIGNATURE_DESC desc = {};
        desc.NumParameters = _countof(params);
        desc.pParameters = params;
        desc.NumStaticSamplers = _countof(c_computeSamplers);
        desc.pStaticSamplers = c_computeSamplers;
        desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

        ID3DBlob* signature = nullptr;
        ID3DBlob* errorBuff = nullptr;
        if (FAILED(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &errorBuff)))
        {
            if (errorBuff) DebugPrint((char*)errorBuff->GetBufferPointer());
            return false;
        }
        HRESULT hr = m_Awesome->Device()->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature));
        SafeRelease(signature);
        SafeRelease(errorBuff);
        if (FAILED(hr)) return false;
        m_rootSignature->SetName(L"Cloud Root Signature");
    }

    // Persistently mapped constant buffers, one per frame index (DOD C4).
    {
        const uint64 cbSize = (sizeof(CloudConstants) + 255) & ~255ull;
        for (uint32 i = 0; i < c_frameBufferCount; ++i)
        {
            D3D12_RESOURCE_DESC desc = {};
            desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            desc.Width = cbSize;
            desc.Height = 1;
            desc.DepthOrArraySize = 1;
            desc.MipLevels = 1;
            desc.SampleDesc.Count = 1;
            desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            desc.Format = DXGI_FORMAT_UNKNOWN;
            m_constantBuffer[i] = m_Awesome->CreateBuffer(desc, L"Cloud Constants", D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
            if (!m_constantBuffer[i]) return false;
            D3D12_RANGE noRead = { 0, 0 };
            if (FAILED(m_constantBuffer[i]->Map(0, &noRead, (void**)&m_constantMapped[i]))) return false;
        }
    }

    // Persistent descriptor blocks (Assets section) — docs/PLAN.md 3.5.
    for (uint32 f = 0; f < c_frameBufferCount; ++f)
        for (uint32 p = 0; p < 2; ++p)
        {
            m_Awesome->GetMainDescHeap()->AllocateBlock(m_srvBlocks[f][p], c_cloudSrvSlots, DescriptorSection::Assets);
            m_Awesome->GetMainDescHeap()->AllocateBlock(m_uavBlocks[f][p], c_cloudUavSlots, DescriptorSection::Assets);
            if (m_srvBlocks[f][p].size() != c_cloudSrvSlots || m_uavBlocks[f][p].size() != c_cloudUavSlots)
            {
                DebugPrint("CloudSystem: descriptor block allocation failed\n");
                return false;
            }
        }

    // 1x1 null texture to initialise every otherwise-unused descriptor slot.
    m_nullTex = CreateTex2D(1, 1, DXGI_FORMAT_R32_FLOAT, L"Cloud Null Tex");
    if (!m_nullTex) return false;
    FillNullDescriptors();

    if (!m_sky->StartUp())
        return false;
    if (!m_generator->StartUp())
        return false;
    if (!m_lighting->StartUp())
        return false;

    // Trace targets (P1.4 is full-res; P5 moves the trace to 1280x720). Sized to
    // the current window; a window resize is not yet handled for cloud targets.
    m_traceW = m_Awesome->GetWidth();
    m_traceH = m_Awesome->GetHeight();
    m_tileCountX = (m_traceW + c_cloudTilePx - 1) / c_cloudTilePx;
    m_tileCountY = (m_traceH + c_cloudTilePx - 1) / c_cloudTilePx;
    m_scatterTex = CreateTex2D(m_traceW, m_traceH, DXGI_FORMAT_R16G16B16A16_FLOAT, L"Cloud Scatter");
    m_cloudDepthTex = CreateTex2D(m_traceW, m_traceH, DXGI_FORMAT_R16_FLOAT, L"Cloud Depth");
    if (!m_scatterTex || !m_cloudDepthTex) return false;

    // Tile buffer: one CloudTile per 16x16 tile (macro lists), rebuilt each frame.
    {
        uint32 tiles = m_tileCountX * m_tileCountY;
        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = (uint64)tiles * sizeof(CloudTile);
        desc.Height = 1; desc.DepthOrArraySize = 1; desc.MipLevels = 1;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        desc.Format = DXGI_FORMAT_UNKNOWN;
        m_tileBuf = m_Awesome->CreateBuffer(desc, L"Cloud Tile Buffer", D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        if (!m_tileBuf) return false;

        D3D12_SHADER_RESOURCE_VIEW_DESC tsrv = {};
        tsrv.Format = DXGI_FORMAT_UNKNOWN;
        tsrv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        tsrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        tsrv.Buffer.NumElements = tiles;
        tsrv.Buffer.StructureByteStride = sizeof(CloudTile);
        WriteSRV(SRV_Tile, m_tileBuf, &tsrv);

        D3D12_UNORDERED_ACCESS_VIEW_DESC tuav = {};
        tuav.Format = DXGI_FORMAT_UNKNOWN;
        tuav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        tuav.Buffer.NumElements = tiles;
        tuav.Buffer.StructureByteStride = sizeof(CloudTile);
        WriteUAV(UAV_Tile, m_tileBuf, &tuav);
    }
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Texture2D.MipLevels = 1;
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        WriteSRV(SRV_Scatter, m_scatterTex, &srv);
        srv.Format = DXGI_FORMAT_R16_FLOAT;
        WriteSRV(SRV_CloudDepth, m_cloudDepthTex, &srv);   // consumed by Phase 5 reproject

        D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
        uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        uav.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        WriteUAV(UAV_Scatter, m_scatterTex, &uav);
        uav.Format = DXGI_FORMAT_R16_FLOAT;
        WriteUAV(UAV_CloudDepth, m_cloudDepthTex, &uav);
    }

    // PSOs
    {
        std::vector<D3D_SHADER_MACRO*> perms;
        perms.push_back(new D3D_SHADER_MACRO[1]{ { NULL, NULL } });
        uint32 comp = m_Awesome->GetComputeSystem()->CompileShader(L"CloudComposite-c", perms);
        uint32 brute = m_Awesome->GetComputeSystem()->CompileShader(L"CloudTraceBrute-c", perms);
        uint32 bin = m_Awesome->GetComputeSystem()->CompileShader(L"CloudTileBin-c", perms);
        uint32 trace = m_Awesome->GetComputeSystem()->CompileShader(L"CloudTrace-c", perms);
        if (comp == invalidIndex32 || brute == invalidIndex32 || bin == invalidIndex32 || trace == invalidIndex32) return false;
        m_compositePSO = m_Awesome->GetComputeSystem()->CreatePipeline(comp, m_rootSignature);
        m_brutePSO = m_Awesome->GetComputeSystem()->CreatePipeline(brute, m_rootSignature);
        m_binPSO = m_Awesome->GetComputeSystem()->CreatePipeline(bin, m_rootSignature);
        m_tracePSO = m_Awesome->GetComputeSystem()->CreatePipeline(trace, m_rootSignature);
        if (m_compositePSO == (uint32)-1 || m_brutePSO == (uint32)-1 || m_binPSO == (uint32)-1 || m_tracePSO == (uint32)-1) return false;
    }

    return true;
}

bool CloudSystem::TearDown()
{
    m_lighting->TearDown();
    m_generator->TearDown();
    m_sky->TearDown();
    SafeRelease(m_scatterTex);
    SafeRelease(m_cloudDepthTex);
    SafeRelease(m_tileBuf);
    for (uint32 i = 0; i < c_frameBufferCount; ++i)
    {
        if (m_constantBuffer[i])
        {
            m_constantBuffer[i]->Unmap(0, nullptr);
            SafeRelease(m_constantBuffer[i]);
        }
        for (uint32 p = 0; p < 2; ++p)
        {
            for (DescriptorHandle& h : m_srvBlocks[i][p]) m_Awesome->GetMainDescHeap()->Free(h);
            for (DescriptorHandle& h : m_uavBlocks[i][p]) m_Awesome->GetMainDescHeap()->Free(h);
            m_srvBlocks[i][p].clear();
            m_uavBlocks[i][p].clear();
        }
    }
    SafeRelease(m_nullTex);
    SafeRelease(m_rootSignature);
    m_shaderCompiler->TearDown();
    return true;
}

void CloudSystem::ReloadShaders()
{
    if (!m_shaderCompiler) return;
    m_Awesome->FlushGPU();
    DebugPrint("CloudSystem: shader reload requested (offline .cso PSOs; hot reload wires in later)\n");
}

void CloudSystem::UpdateConstants(float delta)
{
    m_timeSeconds += delta;
    Camera* cam = m_Awesome->GetCurrentScene()->GetCamera();

    // Sun direction from time of day (simple east-west arc), and drive the
    // engine sun light so cascades/deferred agree with the clouds.
    float dayT = (m_timeOfDay - 6.0f) / 12.0f;                 // 0 at 6h, 1 at 18h
    float elev = sinf(dayT * 3.14159265f) * 1.4f - 0.05f;      // radians above horizon
    float azim = (dayT - 0.5f) * 2.2f;
    XMVECTOR sunDir = XMVector3Normalize(XMVectorSet(sinf(azim) * cosf(elev), sinf(elev), -cosf(azim) * cosf(elev), 0.0f));
    XMFLOAT3 sd; XMStoreFloat3(&sd, sunDir);

    Light* sun = m_Awesome->GetCurrentScene()->GetSunLight();
    sun->direction = { -sd.x, -sd.y, -sd.z };                  // engine stores direction of travel (from sun)

    XMMATRIX viewProj = cam->GetViewProjectionSpaceMatrix();   // unjittered
    XMStoreFloat4x4(&m_constants.invViewProj, XMMatrixInverse(nullptr, viewProj));
    XMStoreFloat4x4(&m_constants.prevViewProj, cam->prevViewProjMatrix);

    m_constants.camPosWS = { cam->transform.position.x, cam->transform.position.y, cam->transform.position.z, m_timeSeconds };
    m_constants.sunDirWS = { sd.x, sd.y, sd.z, 0.004625f };
    float sunUp = sd.y * 4.0f; sunUp = sunUp < 0.0f ? 0.0f : (sunUp > 1.0f ? 1.0f : sunUp);
    m_constants.sunRadiance = { m_sunIntensity * sunUp, m_sunIntensity * sunUp * 0.9f, m_sunIntensity * sunUp * 0.75f, 1.0f };

    float w = (float)m_Awesome->GetWidth();
    float h = (float)m_Awesome->GetHeight();
    float tw = (float)(m_traceW ? m_traceW : m_Awesome->GetWidth());
    float th = (float)(m_traceH ? m_traceH : m_Awesome->GetHeight());
    m_constants.traceSize = { tw, th, 1.0f / tw, 1.0f / th };
    m_constants.outputSize = { w, h, 1.0f / w, 1.0f / h };

    m_constants.mode[0] = m_debugView;
    m_constants.mode[1] = m_lightMode;
    m_constants.mode[2] = m_traversalMode;
    m_constants.mode[3] = (uint32)m_Awesome->GetCurrentFrame();

    // Wind: whole-field advection (ray-origin offset) + per-octave phase drift.
    // Both are per-frame in the trace; neither triggers a regen.
    float wr = m_windDir * 0.0174533f;
    m_constants.windOffset.x += cosf(wr) * m_windSpeed * delta;
    m_constants.windOffset.z += sinf(wr) * m_windSpeed * delta;
    m_constants.windOffset.w = m_windSpeed;
    m_constants.windPhaseVel = { 0.05f, 0.12f, 0.25f, 0.5f };

    m_constants.scatterParams = { 0.85f, -0.15f, 0.7f, 3.0f };
    m_constants.ambientParams = { 1.0f, 0.3f, 1200.0f, 3200.0f };
    m_constants.lodParams = { 2.0f * tanf(cam->verticalFOV * 0.5f) / h, 0.02f, 1.0f, 0.05f };
    m_constants.erosionParams = { 0.7f, 4.0f, 20.0f, 2000.0f };
    uint32 macroCount = m_generator->GetMacroCount();
    m_constants.counts[0] = macroCount;
    m_constants.counts[1] = m_generator->GetKernelCount();
    m_constants.counts[2] = m_tileCountX; m_constants.counts[3] = m_tileCountY;
    float cloudsActive = macroCount > 0 ? 1.0f : 0.0f;
    m_constants.skyParams = { m_turbidity, 0.0f, 0.0f, cloudsActive };

    // Light-cache box: cubic 256 m voxels centred on the camera in macro space
    // (camPos + windOffset), XZ snapped to the voxel grid; Y from ground up.
    const float voxel = 256.0f;
    float mcx = cam->transform.position.x + m_constants.windOffset.x;
    float mcz = cam->transform.position.z + m_constants.windOffset.z;
    float ox = floorf(mcx / voxel) * voxel - (CloudLighting::c_dimX / 2) * voxel;
    float oz = floorf(mcz / voxel) * voxel - (CloudLighting::c_dimZ / 2) * voxel;
    m_constants.cacheOriginWS = { ox, 0.0f, oz, 1.0f / voxel };
    m_cacheSlice = (m_cacheSlice + 1) % (CloudLighting::c_dimZ / CloudLighting::c_slabZ);
    m_constants.genParams[3] = m_cacheSlice;

    memcpy(m_constantMapped[m_Awesome->GetCurrentFrameIndex()], &m_constants, sizeof(CloudConstants));

    // Dirty the sky when the sun elevation changes appreciably.
    if (fabsf(sd.y - m_lastSunY) > 0.001f)
    {
        m_lastSunY = sd.y;
        m_sky->MarkDirty();
    }
}

void CloudSystem::BindCommon()
{
    ID3D12GraphicsCommandList* cl = m_Awesome->GetCommandList();
    uint32 f = CurBlock();
    cl->SetComputeRootSignature(m_rootSignature);
    cl->SetComputeRootConstantBufferView(0, m_constantBuffer[f]->GetGPUVirtualAddress());
    cl->SetComputeRootDescriptorTable(1, m_srvBlocks[f][0][0].gpuHandle);
    cl->SetComputeRootDescriptorTable(2, m_uavBlocks[f][0][0].gpuHandle);
    // param 3 (TLAS) left unset until RQ paths exist (P2); those shaders don't read t15.
}

void CloudSystem::Render(float delta)
{
    if (!m_enabled)
        return;

    PIXScopedEvent(m_Awesome->GetCommandList(), 0, "Clouds");

    // Regenerate (CPU, on param change) + upload before constants so counts are fresh.
    m_generator->EnsureUploaded();
    UpdateConstants(delta);

    uint32 f = CurBlock();

    // Per-frame view refresh for engine-managed resources whose ID3D12Resource*
    // is not stable: scene depth (recreated on resize) and the pool-recycled
    // deferred HDR output. One CreateView each — see docs/STATUS.md deviations.
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC dsrv = {};
        dsrv.Format = DXGI_FORMAT_R32_FLOAT;   // D32 read as R32
        dsrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        dsrv.Texture2D.MipLevels = 1;
        dsrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        m_Awesome->Device()->CreateShaderResourceView(m_Awesome->GetDepthStencilBuffer(), &dsrv, m_srvBlocks[f][0][SRV_Depth].cpuHandle);

        D3D12_UNORDERED_ACCESS_VIEW_DESC ouav = {};
        ouav.Format = DXGI_FORMAT_R11G11B10_FLOAT;
        ouav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        m_Awesome->Device()->CreateUnorderedAccessView(m_Awesome->GetDeferredRenderer()->GetOutputBuffer(), nullptr, &ouav, m_uavBlocks[f][0][UAV_HdrOut].cpuHandle);
    }

    BindCommon();

    // Sky LUTs (regenerate only when dirty).
    {
        PIXScopedEvent(m_Awesome->GetCommandList(), 0, "Cloud Sky");
        m_sky->Render();
    }

    // Sun-transmittance field cache: refresh one Z-slab per frame (analytic).
    if (m_generator->GetMacroCount() > 0)
    {
        PIXScopedEvent(m_Awesome->GetCommandList(), 0, "Cloud Light Cache");
        m_lighting->Build();
    }

    // Scene depth is read as an SRV by both the trace and the composite; move it
    // out of DEPTH_WRITE for the duration of the cloud passes, then restore.
    m_Awesome->TransitionResource(m_Awesome->GetDepthStencilBuffer(), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    // Trace -> scatter (rgb inscatter, a transmittance) + depth. Tiled path
    // (bin + tiled trace) or the brute A/B path, per traversalMode.
    if (m_generator->GetMacroCount() > 0)
    {
        bool tiled = (m_traversalMode == 0);
        ID3D12GraphicsCommandList* cl = m_Awesome->GetCommandList();

        if (tiled)
        {
            PIXScopedEvent(cl, 0, "Cloud Tile Bin");
            m_Awesome->TransitionResource(m_tileBuf, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            m_Awesome->GetComputeSystem()->SetPSO(m_binPSO);
            uint32 tiles = m_tileCountX * m_tileCountY;
            cl->Dispatch((tiles + 63) / 64, 1, 1);
            m_Awesome->TransitionResource(m_tileBuf, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        }

        PIXScopedEvent(cl, 0, tiled ? "Cloud Trace (tiled)" : "Cloud Trace (brute)");
        m_Awesome->TransitionResource(m_scatterTex, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        m_Awesome->TransitionResource(m_cloudDepthTex, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        m_Awesome->GetComputeSystem()->SetPSO(tiled ? m_tracePSO : m_brutePSO);
        cl->Dispatch((m_traceW + 7) / 8, (m_traceH + 7) / 8, 1);
        m_Awesome->TransitionResource(m_scatterTex, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        m_Awesome->TransitionResource(m_cloudDepthTex, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }

    // Composite: sky behind geometry + cloud blend, in-place on the deferred
    // HDR output (which is already in UNORDERED_ACCESS here).
    {
        PIXScopedEvent(m_Awesome->GetCommandList(), 0, "Cloud Composite");
        m_Awesome->GetComputeSystem()->SetPSO(m_compositePSO);
        uint32 gx = ((uint32)m_constants.outputSize.x + 7) / 8;
        uint32 gy = ((uint32)m_constants.outputSize.y + 7) / 8;
        m_Awesome->GetCommandList()->Dispatch(gx, gy, 1);
    }

    m_Awesome->TransitionResource(m_Awesome->GetDepthStencilBuffer(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);
}

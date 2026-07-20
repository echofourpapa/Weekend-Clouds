#include "Awesome.h"
#include "Scene.h"
#include "Util.h"
#include "pix3.h"
#include <algorithm>

#include "QuadMesh.h"
#include "MeshData.h"
#include "Material.h"
#include "Texture.h"
#include "Compute.h"
#include "Deferred.h"
#include "TAA.h"
#include "IBL.h"
#include "ScreenSpaceShadows.h"
//#include "Hair.h"
#include "PostFX.h"
#include "UI.h"
#include "DirectionalShadows.h"
#include "LightCulling.h"
#include "GTAO.h"
#include "AwesomeProfiler.h"
#include "ResourcePool.h"
#include "DescriptorHeap.h"

#include "imgui.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx12.h"

#ifdef _DEBUG
#define DX12_ENABLE_DEBUG_LAYER
// #define USE_NSIGHT_AFTERMATH
#endif

#ifdef DX12_ENABLE_DEBUG_LAYER
#include <dxgidebug.h>
#endif
#include <dxgi1_6.h>

#ifdef USE_NSIGHT_AFTERMATH

#include <iomanip>
#include <string>
#include <sstream>
#include <fstream>

#include "GFSDK_Aftermath.h"
#include "GFSDK_Aftermath_GpuCrashDump.h"
#include "GFSDK_Aftermath_GpuCrashDumpDecoding.h"

namespace std
{
    template<typename T>
    inline std::string to_hex_string(T n)
    {
        std::stringstream stream;
        stream << std::setfill('0') << std::setw(2 * sizeof(T)) << std::hex << n;
        return stream.str();
    }

    inline std::string to_string(GFSDK_Aftermath_Result result)
    {
        return std::string("0x") + to_hex_string(static_cast<uint32_t>(result));
    }

    //inline std::string to_string(const GFSDK_Aftermath_ShaderDebugInfoIdentifier& identifier)
    //{
    //    return to_hex_string(identifier.id[0]) + "-" + to_hex_string(identifier.id[1]);
    //}

    //inline std::string to_string(const GFSDK_Aftermath_ShaderBinaryHash& hash)
    //{
    //    return to_hex_string(hash.hash);
    //}
} // namespace std

inline std::string  AftermathErrorMessage(GFSDK_Aftermath_Result result)
{
    switch (result)
    {
    case GFSDK_Aftermath_Result_FAIL_DriverVersionNotSupported:
        return "Unsupported driver version - requires an NVIDIA R495 display driver or newer.";
    default:
        return "Aftermath Error 0x" + std::to_hex_string(result);
    }
}

// Helper macro for checking Nsight Aftermath results and throwing exception
// in case of a failure.
#ifdef _WIN32
#define AFTERMATH_CHECK_ERROR(FC)                                                                       \
[&]() {                                                                                                 \
    GFSDK_Aftermath_Result _result = FC;                                                                \
    if (!GFSDK_Aftermath_SUCCEED(_result))                                                              \
    {                                                                                                   \
        MessageBoxA(0, AftermathErrorMessage(_result).c_str(), "Aftermath Error", MB_OK);               \
        exit(1);                                                                                        \
    }                                                                                                   \
}()
#else
#define AFTERMATH_CHECK_ERROR(FC)                                                                       \
[&]() {                                                                                                 \
    GFSDK_Aftermath_Result _result = FC;                                                                \
    if (!GFSDK_Aftermath_SUCCEED(_result))                                                              \
    {                                                                                                   \
        printf("%s\n", AftermathErrorMessage(_result).c_str());                                         \
        fflush(stdout);                                                                                 \
        exit(1);                                                                                        \
    }                                                                                                   \
}()
#endif


// Static wrapper for the shader debug information handler. See the 'Handling Shader Debug Information callbacks' section for details.
void ShaderDebugInfoCallback(const void* pShaderDebugInfo, const uint32_t shaderDebugInfoSize, void* pUserData)
{
    //GpuCrashTracker* pGpuCrashTracker = reinterpret_cast<GpuCrashTracker*>(pUserData);
    //pGpuCrashTracker->OnShaderDebugInfo(pShaderDebugInfo, shaderDebugInfoSize);
}

// Static wrapper for the GPU crash dump description handler. See the 'Handling GPU Crash Dump Description Callbacks' section for details.
void CrashDumpDescriptionCallback(PFN_GFSDK_Aftermath_AddGpuCrashDumpDescription addDescription, void* pUserData)
{
    //GpuCrashTracker* pGpuCrashTracker = reinterpret_cast<GpuCrashTracker*>(pUserData);
    //pGpuCrashTracker->OnDescription(addDescription);
}

// Static wrapper for the resolve marker handler. See the 'Handling Marker Resolve Callbacks' section for details.
void ResolveMarkerCallback(const void* pMarkerData, const uint32_t markerDataSize, void* pUserData, void** ppResolvedMarkerData, uint32_t* pResolvedMarkerDataSize)
{
    //GpuCrashTracker* pGpuCrashTracker = reinterpret_cast<GpuCrashTracker*>(pUserData);
    //pGpuCrashTracker->OnResolveMarker(pMarkerData, markerDataSize, ppResolvedMarkerData, pResolvedMarkerDataSize);
}

void ShaderSourceDebugInfoLookupCallback(
    const GFSDK_Aftermath_ShaderDebugName* pShaderDebugName,
    PFN_GFSDK_Aftermath_SetData setShaderBinary,
    void* pUserData)
{
    //GpuCrashTracker* pGpuCrashTracker = reinterpret_cast<GpuCrashTracker*>(pUserData);
    //pGpuCrashTracker->OnShaderSourceDebugInfoLookup(*pShaderDebugName, setShaderBinary);
}

// Static callback wrapper for OnShaderDebugInfoLookup
void ShaderDebugInfoLookupCallback(
    const GFSDK_Aftermath_ShaderDebugInfoIdentifier* pIdentifier,
    PFN_GFSDK_Aftermath_SetData setShaderDebugInfo,
    void* pUserData)
{
    //GpuCrashTracker* pGpuCrashTracker = reinterpret_cast<GpuCrashTracker*>(pUserData);
    //pGpuCrashTracker->OnShaderDebugInfoLookup(*pIdentifier, setShaderDebugInfo);
}

// Static callback wrapper for OnShaderLookup
void ShaderLookupCallback(
    const GFSDK_Aftermath_ShaderBinaryHash* pShaderHash,
    PFN_GFSDK_Aftermath_SetData setShaderBinary,
    void* pUserData)
{
    //GpuCrashTracker* pGpuCrashTracker = reinterpret_cast<GpuCrashTracker*>(pUserData);
    //pGpuCrashTracker->OnShaderLookup(*pShaderHash, setShaderBinary);
}


// Static wrapper for the GPU crash dump handler. See the 'Handling GPU crash dump Callbacks' section for details.
void GpuCrashDumpCallback(const void* pGpuCrashDump, const uint32_t gpuCrashDumpSize, void* pUserData)
{
    //GpuCrashTracker* pGpuCrashTracker = reinterpret_cast<GpuCrashTracker*>(pUserData);
    //pGpuCrashTracker->OnCrashDump(pGpuCrashDump, gpuCrashDumpSize);

        // Create a GPU crash dump decoder object for the GPU crash dump.
    GFSDK_Aftermath_GpuCrashDump_Decoder decoder = {};
    AFTERMATH_CHECK_ERROR(GFSDK_Aftermath_GpuCrashDump_CreateDecoder(
        GFSDK_Aftermath_Version_API,
        pGpuCrashDump,
        gpuCrashDumpSize,
        &decoder));

    // Use the decoder object to read basic information, like application
    // name, PID, etc. from the GPU crash dump.
    GFSDK_Aftermath_GpuCrashDump_BaseInfo baseInfo = {};
    AFTERMATH_CHECK_ERROR(GFSDK_Aftermath_GpuCrashDump_GetBaseInfo(decoder, &baseInfo));

    // Use the decoder object to query the application name that was set
    // in the GPU crash dump description.
    //uint32_t applicationNameLength = 0;
    //AFTERMATH_CHECK_ERROR(GFSDK_Aftermath_GpuCrashDump_GetDescriptionSize(
    //    decoder,
    //    GFSDK_Aftermath_GpuCrashDumpDescriptionKey_ApplicationName,
    //    &applicationNameLength));

    //std::vector<char> applicationName(applicationNameLength, '\0');

    //AFTERMATH_CHECK_ERROR(GFSDK_Aftermath_GpuCrashDump_GetDescription(
    //    decoder,
    //    GFSDK_Aftermath_GpuCrashDumpDescriptionKey_ApplicationName,
    //    uint32_t(applicationName.size()),
    //    applicationName.data()));

    // Create a unique file name for writing the crash dump data to a file.
    // Note: due to an Nsight Aftermath bug (will be fixed in an upcoming
    // driver release) we may see redundant crash dumps. As a workaround,
    // attach a unique count to each generated file name.
    static int count = 0;
    const std::string baseFileName =
        std::string("Awsome_Thing")
        + "-"
        + std::to_string(baseInfo.pid)
        + "-"
        + std::to_string(++count);

    // Write the crash dump data to a file using the .nv-gpudmp extension
    // registered with Nsight Graphics.
    const std::string crashDumpFileName = baseFileName + ".nv-gpudmp";
    std::ofstream dumpFile(crashDumpFileName, std::ios::out | std::ios::binary);
    if (dumpFile)
    {
        dumpFile.write((const char*)pGpuCrashDump, gpuCrashDumpSize);
        dumpFile.close();
    }

    // Decode the crash dump to a JSON string.
    // Step 1: Generate the JSON and get the size.
    uint32_t jsonSize = 0;
    AFTERMATH_CHECK_ERROR(GFSDK_Aftermath_GpuCrashDump_GenerateJSON(
        decoder,
        GFSDK_Aftermath_GpuCrashDumpDecoderFlags_ALL_INFO,
        GFSDK_Aftermath_GpuCrashDumpFormatterFlags_NONE,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        &jsonSize));
    // Step 2: Allocate a buffer and fetch the generated JSON.
    std::vector<char> json(jsonSize);
    AFTERMATH_CHECK_ERROR(GFSDK_Aftermath_GpuCrashDump_GetJSON(
        decoder,
        uint32_t(json.size()),
        json.data()));

    // Write the crash dump data as JSON to a file.
    const std::string jsonFileName = crashDumpFileName + ".json";
    std::ofstream jsonFile(jsonFileName, std::ios::out | std::ios::binary);
    if (jsonFile)
    {
        // Write the JSON to the file (excluding string termination)
        jsonFile.write(json.data(), json.size() - 1);
        jsonFile.close();
    }

    // Destroy the GPU crash dump decoder object.
    AFTERMATH_CHECK_ERROR(GFSDK_Aftermath_GpuCrashDump_DestroyDecoder(decoder));

}

#endif

using namespace Awesome;

AwesomeGraphics::AwesomeGraphics(HWND hwnd, uint16 width, uint16 height)
    : m_hwnd(hwnd)
    , m_width(width)
    , m_height(height)
    , m_fullscreen(false)
    , m_hasImgui(true)
    , m_taaEnabled(true)
    , m_hairEnabled(false)
    , m_vsyncEnabled(false)
    , m_postFXEnabled(true)
    , m_sSShadowsEnabled(true)
    , m_hdr(false)
    , m_hdrSupported(false)
    , m_playWorms(false)
    , m_maxNits(600.0)
    , m_currentNits(m_maxNits)
    , m_uiPaperWhite(200.0)
    , m_vsyncInverval(1)
    , m_lightingFlags(0)
    , m_debugFlags(DebugFlags::Lit)
    , m_lastCompleted{}
    , m_frameIndex(0)
    , m_currentFrame(0)
    , m_resourcePool(new ResourcePool(this))
    , m_scene(new Scene(this))
    , m_device(nullptr)
    , m_adapter(nullptr)
    , m_swapChain(nullptr)
    , m_renderTargets{nullptr}
    , m_rtvDescHeap(nullptr)
    , m_depthStencilBuffer{nullptr}
    , m_dsDescHeap(nullptr)
    , m_mainCbvSrvUavDescHeap(new DescriptorHeap(this))
    , m_commandQueue(nullptr)
    , m_commandList{nullptr}
    , m_commandAllocator{nullptr}
    , m_fence(nullptr)
    , m_quad(new QuadMesh(this))
    , m_materialSystem(new MaterialSystem(this))
    , m_meshSystem(new MeshSystem(this))
    , m_textureSystem(new TextureSystem(this))
    , m_computeSystem(new ComputeSystem(this))
    , m_deferredRenderer(new DeferredRenderer(this))
    , m_taa(new TemporalAntiAliasing(this))
    , m_iblSystem(new IBLSystem(this))
    , m_screenSpaceShadows(new ScreenSpaceShadows(this))
    //, m_hairSystem(new HairSystem(this))
    , m_postFX(new PostFX(this))
    , m_UISystem(new UISystem(this, m_quad))
    , m_directShadows(new DirectionalShadows(this))
    , m_gtao(new GTAO(this))
    , m_tileLightCulling(new TileLightCull(this))
    , m_profiler(new AwesomeProfiler(this))
    
{
    m_lightingFlags = (uint32)(
          DeferredRenderer::LightingFlags::DirectDiffuse 
        | DeferredRenderer::LightingFlags::DirectSpecular 
        | DeferredRenderer::LightingFlags::IndirectDiffuse 
        | DeferredRenderer::LightingFlags::IndirectSpecular 
        | DeferredRenderer::LightingFlags::DirectionalShadows 
        | DeferredRenderer::LightingFlags::SSShadows
        | DeferredRenderer::LightingFlags::GTAO
        | DeferredRenderer::LightingFlags::HBIL
        | DeferredRenderer::LightingFlags::Sheen
        );
    m_scenes.reserve(10);

    Awesome::Camera* cam = m_scene->GetCamera();

    cam->width = m_width;
    cam->height = m_height;
    cam->aspectRatio = (float)m_width / (float)m_height;
    cam->horizontalFOV = 90.0f;
    cam->verticalFOV = cam->CalcVerticalFOV();
    cam->nearClip = 0.01f;
    cam->farClip = 5000.0f;
    cam->type = Awesome::CameraType::Perspevtive;
    cam->transform.position = { 0.0f, 45.0f, 90.0f };
    cam->transform.rotation = { };
    cam->target = Awesome::Transform::Zero;
    cam->distance = 100.f;
    cam->StartFrame();
    cam->EndFrame();

    m_scene->SetName("Empty");

    m_scenes.push_back(*m_scene);
}

AwesomeGraphics::~AwesomeGraphics()
{
    m_scene = nullptr;
    m_scenes.clear();
}

bool AwesomeGraphics::StartUp()
{

    // [DEBUG] Enable debug interface
#ifdef DX12_ENABLE_DEBUG_LAYER
    ID3D12Debug* pdx12Debug = NULL;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&pdx12Debug))))
        pdx12Debug->EnableDebugLayer();
#endif
    // Create device
    D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_12_0;
    if (D3D12CreateDevice(NULL, featureLevel, IID_PPV_ARGS(&m_device)) != S_OK)
        return false;

    // Capability queries for the cloud renderer (docs/PLAN.md P0.3). All optional:
    // the primary cloud path is plain compute; RayQuery-based paths check m_rtSupported.
    if (FAILED(m_device->QueryInterface(IID_PPV_ARGS(&m_device5))))
        m_device5 = nullptr;

    D3D12_FEATURE_DATA_D3D12_OPTIONS5 opts5 = {};
    if (SUCCEEDED(m_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &opts5, sizeof(opts5))))
        m_rtTier = (uint32)opts5.RaytracingTier;
    m_rtSupported = m_device5 != nullptr && m_rtTier >= (uint32)D3D12_RAYTRACING_TIER_1_1;

    D3D12_FEATURE_DATA_SHADER_MODEL sm = { D3D_SHADER_MODEL_6_8 };
    if (SUCCEEDED(m_device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &sm, sizeof(sm))))
        m_shaderModel = (uint32)sm.HighestShaderModel;

    D3D12_FEATURE_DATA_FORMAT_SUPPORT r11 = { DXGI_FORMAT_R11G11B10_FLOAT };
    if (SUCCEEDED(m_device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &r11, sizeof(r11))))
        m_typedUAVLoads = (r11.Support2 & D3D12_FORMAT_SUPPORT2_UAV_TYPED_LOAD) != 0;

    DebugPrint("D3D12 caps: RT tier %u, SM 0x%x, R11G11B10 typed UAV loads %d\n",
        m_rtTier, m_shaderModel, (int)m_typedUAVLoads);

    // [DEBUG] Setup debug interface to break on any warnings/errors
#ifdef DX12_ENABLE_DEBUG_LAYER
    if (pdx12Debug != NULL)
    {
        ID3D12InfoQueue* pInfoQueue = NULL;
        m_device->QueryInterface(IID_PPV_ARGS(&pInfoQueue));
        pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);
        pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true);
        pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, true);
        pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_INFO, true);
        pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_MESSAGE, true);
        pInfoQueue->Release();
        pdx12Debug->Release();
    }
#endif

#ifdef USE_NSIGHT_AFTERMATH

    // Enable GPU crash dumps and register callbacks.
    AFTERMATH_CHECK_ERROR(GFSDK_Aftermath_EnableGpuCrashDumps(
        GFSDK_Aftermath_Version_API,
        GFSDK_Aftermath_GpuCrashDumpWatchedApiFlags_DX,
        GFSDK_Aftermath_GpuCrashDumpFeatureFlags_Default,   // Default behavior.
        GpuCrashDumpCallback,                               // Register callback for GPU crash dumps.
        ShaderDebugInfoCallback,                            // Register callback for shader debug information.
        CrashDumpDescriptionCallback,                       // Register callback for GPU crash dump description.
        ResolveMarkerCallback,                              // Register callback for marker resolution (R495 or later NVIDIA graphics driver).
        nullptr));                           // Set the GpuCrashTracker object as user data passed back by the above callbacks.

    // Initialize Nsight Aftermath for this device.
    const uint32_t aftermathFlags =
        GFSDK_Aftermath_FeatureFlags_EnableMarkers |             // Enable event marker tracking.
        GFSDK_Aftermath_FeatureFlags_CallStackCapturing |        // Enable automatic call stack event markers.
        GFSDK_Aftermath_FeatureFlags_EnableResourceTracking |    // Enable tracking of resources.
        GFSDK_Aftermath_FeatureFlags_GenerateShaderDebugInfo |   // Generate debug information for shaders.
        GFSDK_Aftermath_FeatureFlags_EnableShaderErrorReporting; // Enable additional runtime shader error reporting.

    auto fc = GFSDK_Aftermath_DX12_Initialize(
        GFSDK_Aftermath_Version_API,
        aftermathFlags,
        m_device);

    AFTERMATH_CHECK_ERROR(fc);

#endif


    // Create Command Queue
    {
        D3D12_COMMAND_QUEUE_DESC desc = {};
        desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
        desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        if (m_device->CreateCommandQueue(&desc, IID_PPV_ARGS(&m_commandQueue)) != S_OK)
            return false;
    }

    // Create Swap Chain
    if (!CreateSwapChain())
        return false;
   
    // Create Render Target Views (Back Buffer) Descriptor Heap
    {
        D3D12_DESCRIPTOR_HEAP_DESC desc = {};
        desc.NumDescriptors = c_frameBufferCount;
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        if (m_device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_rtvDescHeap)) != S_OK)
            return false;
        m_rtvDescSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    }

    // Create Render Target Views (Back Buffer) 
    if (!UpdateRenderTargetViews())
        return false;

    // Create Depth/Stencil Buffer Descriptor Heap
    {
        // create a depth stencil descriptor heap so we can get a pointer to the depth stencil buffer
        D3D12_DESCRIPTOR_HEAP_DESC desc = {};
        desc.NumDescriptors = c_frameBufferCount;
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        if (FAILED(m_device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_dsDescHeap))))
            return false;
        m_dsvDescSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    }

    // Create Depth/Stencil Buffer Resource
    if(!UpdateDepthStencilView())
        return false;

    {
        D3D12_DESCRIPTOR_HEAP_DESC desc = {};
        desc.NumDescriptors = 1024;
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (!m_mainCbvSrvUavDescHeap->Create(desc, L"Main CBV/SRV/UAV Descriptor Heap"))
            return false;
    }

    // Create Command Allocators
    for (uint16 i = 0; i < c_frameBufferCount; ++i)
    {
        if(FAILED(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocator[i])) ))
            return false;

        // Create Command List
        if (FAILED(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocator[0], NULL, IID_PPV_ARGS(&m_commandList[i]))))
            return false;
        m_commandList[i]->Close();

        // Needed for BuildRaytracingAccelerationStructure; null is fine (RQ paths check).
        if (FAILED(m_commandList[i]->QueryInterface(IID_PPV_ARGS(&m_commandList4[i]))))
            m_commandList4[i] = nullptr;
    }


    // Create Fence
    {
        if (FAILED(m_device->CreateFence(m_lastCompleted, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)) ))
            return false;
        m_lastCompleted++;
        m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (!m_fenceEvent)
            return false;
        
    }

    UpdateViewport();

    if (!m_meshSystem->StartUp())
        return false;

    if (!m_materialSystem->StartUp())
        return false;

    if (!m_textureSystem->StartUp())
        return false;

    if (!m_computeSystem->StartUp())
        return false;

    if (!m_iblSystem->StartUp())
        return false;

    if (!m_tileLightCulling->StartUp())
        return false;

    if (!m_directShadows->StartUp())
        return false;

    if (!m_screenSpaceShadows->StartUp())
        return false;

    if (!m_deferredRenderer->StartUp())
        return false;
    
    if (!m_gtao->StartUp())
        return false;
    
    if (!m_postFX->StartUp())
        return false;

    if (!m_UISystem->StartUp())
        return false;

    if (!m_taa->StartUp())
        return false;

    //if (!m_hairSystem->StartUp())
    //    return false;
    
    if (!m_profiler->StartUp())
        return false;
    
    LoadBuiltin();

    return true;
}

void AwesomeGraphics::TearDown()
{
    // Wait for GPU to finish all frames
    FlushGPU();

    if (m_swapChain) {
        // Exit fullscreen
        BOOL fs = false;
        if (SUCCEEDED(m_swapChain->GetFullscreenState(&fs, NULL)))
            m_swapChain->SetFullscreenState(false, NULL);
    }

    // Release Everything    

    ReleaseTempResources();

    SafeRelease(m_device5);
    SafeRelease(m_device);
    SafeRelease(m_adapter);
    SafeRelease(m_swapChain);
    SafeRelease(m_commandQueue);
    SafeRelease(m_rtvDescHeap);
    SafeRelease(m_dsDescHeap);
    SafeRelease(m_mainCbvSrvUavDescHeap);

    for (uint16 i = 0; i < c_frameBufferCount; ++i)
    {
        SafeRelease(m_depthStencilBuffer[i]);
        SafeRelease(m_renderTargets[i]);
        SafeRelease(m_commandAllocator[i]);
        SafeRelease(m_commandList4[i]);
        SafeRelease(m_commandList[i]);
    }
    
    m_meshSystem->TearDown(); m_meshSystem = nullptr;
    m_materialSystem->TearDown(); m_materialSystem = nullptr;
    m_textureSystem->TearDown(); m_textureSystem = nullptr;
    m_computeSystem->TearDown(); m_computeSystem = nullptr;
    m_deferredRenderer->TearDown(); m_deferredRenderer = nullptr;
    m_UISystem->TearDown(); m_UISystem = nullptr;
    m_postFX->TearDown(); m_postFX = nullptr;
    m_taa->TearDown(); m_taa = nullptr;
    m_iblSystem->TearDown(); m_iblSystem = nullptr;
    m_screenSpaceShadows->TearDown(); m_screenSpaceShadows = nullptr;
    //m_hairSystem->TearDown(); m_hairSystem = nullptr;
    m_directShadows->TearDown(); m_directShadows = nullptr;
    m_tileLightCulling->TearDown(); m_tileLightCulling = nullptr;
    m_gtao->TearDown(); m_gtao = nullptr;
    m_profiler->TearDown(); m_profiler = nullptr;
    m_resourcePool->TearDown(); m_resourcePool = nullptr;
    m_quad->TearDown(); m_quad = nullptr;

    SafeRelease(m_fence);

    CloseHandle(m_fenceEvent);
    m_fenceEvent = NULL;

#ifdef DX12_ENABLE_DEBUG_LAYER
    IDXGIDebug1* pDebug = NULL;
    if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&pDebug))))
    {
        assert(SUCCEEDED(pDebug->ReportLiveObjects(DXGI_DEBUG_DX, DXGI_DEBUG_RLO_ALL)));
        pDebug->Release();
    }
#endif
}

bool AwesomeGraphics::CreateSwapChain()
{
    DWORD dxgiFactoryFlags = 0;
#ifdef DX12_ENABLE_DEBUG_LAYER
    dxgiFactoryFlags = DXGI_CREATE_FACTORY_DEBUG;
#endif
    IDXGIFactory4* dxgiFactory;
    if (FAILED(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&dxgiFactory)) ))
        return false;

    // Back Buffer Descriptor
    DXGI_MODE_DESC backBufferDesc = {};
    backBufferDesc.Width = m_width;
    backBufferDesc.Height = m_height;
    backBufferDesc.Format = GetSwapChainFormat();
    backBufferDesc.Scaling = DXGI_MODE_SCALING_STRETCHED;

    // Multi-Sampling Descriptor
    DXGI_SAMPLE_DESC sampleDesc = {};
    sampleDesc.Count = 1;

    // Swap Chain Descriptor
    DXGI_SWAP_CHAIN_DESC swapChainDesc = {};
    swapChainDesc.BufferCount = c_frameBufferCount;
    swapChainDesc.BufferDesc = backBufferDesc;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.SwapEffect =  DXGI_SWAP_EFFECT_FLIP_DISCARD; //DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL
    swapChainDesc.OutputWindow = m_hwnd;
    swapChainDesc.SampleDesc = sampleDesc;
    swapChainDesc.Windowed = true;

    IDXGISwapChain* swapChain = nullptr;
    if (FAILED(dxgiFactory->CreateSwapChain(m_commandQueue, &swapChainDesc, &swapChain)))
        return false;

    if (FAILED(dxgiFactory->MakeWindowAssociation(m_hwnd, DXGI_MWA_NO_ALT_ENTER)))
        return false;

    m_swapChain = static_cast<IDXGISwapChain3*>(swapChain);
    
    bool needsResize = false;
    
    HMONITOR hMonitor = MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST);

    IDXGIAdapter1* adapter = nullptr;

    for (UINT i = 0; dxgiFactory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i)
    {
        IDXGIOutput* currentOutput = nullptr;
        for (UINT j = 0; adapter->EnumOutputs(j, &currentOutput) != DXGI_ERROR_NOT_FOUND; ++j)
        {
            DXGI_OUTPUT_DESC desc;
            currentOutput->GetDesc(&desc);

            if (desc.Monitor == hMonitor)
            {
                IDXGIOutput6* output6 = nullptr;
                if (SUCCEEDED(currentOutput->QueryInterface(IID_PPV_ARGS(&output6))))
                {
                    DXGI_OUTPUT_DESC1 desc1;
                    if (SUCCEEDED(output6->GetDesc1(&desc1)))
                    {
                        m_maxNits = desc1.MaxLuminance;
                        m_maxFullNits = desc1.MaxFullFrameLuminance;
                
                        bool deviceIsHDR = (desc1.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020);
                
                        if (m_hdr != deviceIsHDR)
                        {
                            m_hdr = deviceIsHDR;
                            m_hdrSupported = m_hdr;
                            needsResize = true;
                        }
                
                        if (m_hdr && m_maxNits <= 0.0f) m_maxNits = 1000.0f; 
                
                        m_currentNits = m_maxNits;
                        m_hdrSupported = m_hdr;
                    }
                    output6->Release();
                }
            }
            currentOutput->Release();
        }
        adapter->Release();
    } 
    
    if (needsResize)
    {
        HRESULT hr = m_swapChain->ResizeBuffers(
            c_frameBufferCount, 
            m_width, 
            m_height, 
            GetSwapChainFormat(),
            swapChainDesc.Flags
        );
        
        if (FAILED(hr))
        {
            DebugPrint("Failed to resize swapchain to match HDR/SDR format.\n");
            return false;
        }
    }
    
    m_swapChain->SetColorSpace1(m_hdr ? DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020 : DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709);

    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

    return true;
}

bool AwesomeGraphics::UpdateRenderTargetViews()
{
    // Create Render Target Views (Back Buffer) 
    {
        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_rtvDescHeap->GetCPUDescriptorHandleForHeapStart();

        for (uint16 i = 0; i < c_frameBufferCount; ++i)
        {
            if (FAILED(m_swapChain->GetBuffer(i, IID_PPV_ARGS(&m_renderTargets[i]))))
                return false;

            D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
            rtvDesc.Format = GetSwapChainFormat();
            rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

            m_device->CreateRenderTargetView(m_renderTargets[i], &rtvDesc, rtvHandle);
            wchar_t name[256];
            swprintf_s(name, L"Render Target %d", i);
            m_renderTargets[i]->SetName(name);
            rtvHandle.ptr += m_rtvDescSize;
        }

    }
    return true;
}

bool AwesomeGraphics::UpdateDepthStencilView()
{
    // Create Depth/Stencil Buffer Resource
    {
        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Alignment = 0;
        desc.Width = m_width;
        desc.Height = m_height;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_D32_FLOAT;
        desc.SampleDesc.Count = 1;
        desc.SampleDesc.Quality = 0;
        desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

        D3D12_DEPTH_STENCIL_VIEW_DESC depthStencilDesc = {};
        depthStencilDesc.Format = DXGI_FORMAT_D32_FLOAT;
        depthStencilDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        depthStencilDesc.Flags = D3D12_DSV_FLAG_NONE;
        depthStencilDesc.Texture2D.MipSlice = 0;

        D3D12_CLEAR_VALUE depthOptimizedClearValue = {};
        depthOptimizedClearValue.Format = DXGI_FORMAT_D32_FLOAT;
        depthOptimizedClearValue.DepthStencil.Depth = 0.0f;
        depthOptimizedClearValue.DepthStencil.Stencil = 0;

        m_dsDescHeap->SetName(L"Depth/Stencil Resource Heap");

        auto dsvHandle = m_dsDescHeap->GetCPUDescriptorHandleForHeapStart();

        for (uint16 i = 0; i < c_frameBufferCount; ++i)
        {
            wchar_t name[256];
            swprintf_s(name, L"Depth/Stencil Buffer %d", i);
            m_depthStencilBuffer[i] = CreateBuffer(desc, name, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_DEPTH_WRITE, &depthOptimizedClearValue);
            
            m_device->CreateDepthStencilView(m_depthStencilBuffer[i], &depthStencilDesc, dsvHandle);
            dsvHandle.ptr += m_dsvDescSize;
        }
    }
    return true;
}

void AwesomeGraphics::UpdateViewport()
{

    // Fill out the Viewport
    m_viewport.TopLeftX = 0;
    m_viewport.TopLeftY = 0;
    m_viewport.Width = m_width;
    m_viewport.Height = m_height;
    m_viewport.MinDepth = D3D12_MIN_DEPTH;
    m_viewport.MaxDepth = D3D12_MAX_DEPTH;

    // Fill out a scissor rect
    m_scissorRect.left = 0;
    m_scissorRect.top = 0;
    m_scissorRect.right = m_width;
    m_scissorRect.bottom = m_height;
}

void AwesomeGraphics::ReleaseTempResources()
{
    if (m_toRelease.size() > 0)
    {
        for (ID3D12Resource* obj : m_toRelease)
        {
            SafeRelease(obj);
        }
        m_toRelease.clear();
    }
}

bool AwesomeGraphics::BeginFrame(bool present)
{
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
    // Reset Command Allocator and List
    if (FAILED(m_commandAllocator[m_frameIndex]->Reset()))
    {
        return false;
    }

    if(FAILED(m_commandList[m_frameIndex]->Reset(m_commandAllocator[m_frameIndex], NULL)))
        return false;


    PIXScopedEvent(GetCommandList(), 0, "Clear Frame");

    

    // Transition To Render Target
    if (present)
    {
        m_resourcePool->StartFrame();
        TransitionResource(m_renderTargets[m_frameIndex], D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);

        // Clear Swap Chain

        D3D12_CPU_DESCRIPTOR_HANDLE dsHandle = m_dsDescHeap->GetCPUDescriptorHandleForHeapStart();
        dsHandle.ptr += m_frameIndex * m_dsvDescSize;
        m_commandList[m_frameIndex]->ClearDepthStencilView(dsHandle, D3D12_CLEAR_FLAG_DEPTH, 0.0f, 0, 0, nullptr);

        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_rtvDescHeap->GetCPUDescriptorHandleForHeapStart();
        rtvHandle.ptr += m_frameIndex * m_rtvDescSize;
        m_commandList[m_frameIndex]->ClearRenderTargetView(rtvHandle, c_clearColor, 0, NULL);
  
        m_commandList[m_frameIndex]->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsHandle);

        SetViewport();
    }

    return true;
}

bool AwesomeGraphics::StartStats()
{
    IDXGIFactory4* dxgiFactory;
    if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&dxgiFactory))))
        return false;

    IDXGIAdapter* adapter;
    if (dxgiFactory->EnumAdapters(0, &adapter) == DXGI_ERROR_NOT_FOUND)
        return false;

    HRESULT res = adapter->QueryInterface(&m_adapter);
    SafeRelease(adapter);

    return SUCCEEDED(res);
}

float AwesomeGraphics::GetCurrentMemory()
{
    if(!m_adapter)
        return 0.0f;

    DXGI_QUERY_VIDEO_MEMORY_INFO info;
    if (FAILED(m_adapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP::DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info)))
        return 0.0f;

    return float(info.CurrentUsage) / 1048576.0f;
}

float AwesomeGraphics::GetTotalMemory()
{
    if (!m_adapter)
        return 0.0f;

    DXGI_QUERY_VIDEO_MEMORY_INFO info;
    if (FAILED(m_adapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP::DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info)))
        return 0.0f;

    return float(info.Budget) / 1048576.0f;
}

bool AwesomeGraphics::EndFrame(bool present)
{
    // Transition To Present
    if (present)
    {
        TransitionResource(m_renderTargets[m_frameIndex], D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
    }

    m_commandList[m_frameIndex]->Close();
    
    {
        PIXScopedEvent(m_commandQueue, 0, "Render");
        ID3D12CommandList* commandLists[] = { m_commandList[m_frameIndex] };
        m_commandQueue->ExecuteCommandLists(_countof(commandLists), commandLists);
    }

    if (present)
    {
        PIXScopedEvent(m_commandQueue, 0, "Present");
        uint32 sync = m_vsyncEnabled ? m_vsyncInverval : 0;
        if (FAILED(m_swapChain->Present(sync, 0)))
            return false;

        ReleaseTempResources();
    }

    if (present)
        WaitForNextFrame();
    else
        FlushGPU();

    return true;
}

bool AwesomeGraphics::LoadBuiltin()
{
    FlushGPU();

    if (!m_quad->StartUp())
        return false;
    FlushGPU();
    return true;
}

ID3D12Resource* AwesomeGraphics::CreateBuffer(D3D12_RESOURCE_DESC& desc, LPCWSTR name, D3D12_HEAP_TYPE type, D3D12_RESOURCE_STATES state, const D3D12_CLEAR_VALUE* clear)
{
    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = type;
    heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProps.CreationNodeMask = 1;
    heapProps.VisibleNodeMask = 1;

    ID3D12Resource* resource;
    if (FAILED(m_device->CreateCommittedResource(&heapProps,
        D3D12_HEAP_FLAG_NONE,
        &desc,
        state,
        clear,
        IID_PPV_ARGS(&resource))))
    {
        resource->Release();
        resource = nullptr;
        return resource;
    }
    resource->SetName(name);
    return resource;
}

bool AwesomeGraphics::UploadBuffer(ID3D12Resource* resource, D3D12_RESOURCE_DESC& desc, uint64 size, const uint8* srcData)
{
    ID3D12Resource* vBufferUploadHeap = CreateBuffer(desc, L"Buffer Upload Resource Heap", D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);

    D3D12_SUBRESOURCE_DATA subData = {};
    subData.pData = srcData;
    subData.RowPitch = size;
    subData.SlicePitch = size;

    {
        uint64 RequiredSize = 0;
        uint64 MemToAlloc = static_cast<uint64>(sizeof(D3D12_PLACED_SUBRESOURCE_FOOTPRINT) + sizeof(uint32) + sizeof(uint64));
        void* pMem = HeapAlloc(GetProcessHeap(), 0, static_cast<SIZE_T>(MemToAlloc));

        auto pLayouts = reinterpret_cast<D3D12_PLACED_SUBRESOURCE_FOOTPRINT*>(pMem);
        uint64* pRowSizesInBytes = reinterpret_cast<uint64*>(pLayouts + 1);
        uint32* pNumRows = reinterpret_cast<uint32*>(pRowSizesInBytes + 1);

        m_device->GetCopyableFootprints(&desc, 0, 1, 0, pLayouts, pNumRows, pRowSizesInBytes, &RequiredSize);


        uint8* pData;
        vBufferUploadHeap->Map(0, nullptr, reinterpret_cast<void**>(&pData));


        D3D12_MEMCPY_DEST DestData = { pData + pLayouts[0].Offset, pLayouts[0].Footprint.RowPitch, SIZE_T(pLayouts[0].Footprint.RowPitch) * SIZE_T(pNumRows[0]) };


        for (uint32 i = 0; i < pNumRows[0]; ++i)
        {
            uint8* dst = reinterpret_cast<uint8*>(DestData.pData);
            const uint8* src = reinterpret_cast<const uint8*>(subData.pData);
            memcpy(dst + DestData.RowPitch * i,
                src + subData.RowPitch * i,
                pRowSizesInBytes[0]);
        }

        vBufferUploadHeap->Unmap(0, nullptr);
        m_commandList[m_frameIndex]->CopyBufferRegion(resource, 0, vBufferUploadHeap, pLayouts[0].Offset, pLayouts[0].Footprint.Width);


        HeapFree(GetProcessHeap(), 0, pMem);
    }
    m_toRelease.push_back(vBufferUploadHeap);
    return true;
}

bool AwesomeGraphics::UploadTexture(ID3D12Resource* resource, D3D12_RESOURCE_DESC& desc, uint64 size, const ImageData& imgData)
{

    uint64 RequiredSize = 0;
    uint32 numRows[MAX_SUB_RESOURCE];
    uint64 rowSizeInBytes[MAX_SUB_RESOURCE];
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT layouts[MAX_SUB_RESOURCE];

    m_device->GetCopyableFootprints(&desc, 0, imgData.mipCount * desc.DepthOrArraySize, 0, layouts, numRows, rowSizeInBytes, &RequiredSize);

    D3D12_RESOURCE_DESC ubDesc = {};
    ubDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    ubDesc.Alignment = 0;
    ubDesc.Width = RequiredSize;
    ubDesc.Height = 1;
    ubDesc.DepthOrArraySize = 1;
    ubDesc.MipLevels = 1;
    ubDesc.Format = DXGI_FORMAT_UNKNOWN;
    ubDesc.SampleDesc.Count = 1;
    ubDesc.SampleDesc.Quality = 0;
    ubDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ubDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    ID3D12Resource* vBufferUploadHeap = CreateBuffer(ubDesc, L"Texture Upload Resource Heap", D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);

    uint8* pData;
    vBufferUploadHeap->Map(0, nullptr, reinterpret_cast<void**>(&pData));

    for (uint32 i = 0; i < imgData.mipCount; i++) {

        D3D12_MEMCPY_DEST DestData = { 
            pData + layouts[i].Offset,
            layouts[i].Footprint.RowPitch,
            SIZE_T(layouts[i].Footprint.RowPitch) * SIZE_T(numRows[i]) 
        };
        uint32 subPitch = imgData.GetRowPitch(i);
        D3D12_SUBRESOURCE_DATA subData = {
            &imgData.images[i].front(),
            subPitch,
            subPitch + numRows[i]
        };

        for (uint32 j = 0; j < numRows[i]; ++j)
        {
            uint8* dst = reinterpret_cast<uint8*>(DestData.pData);
            const uint8* src = reinterpret_cast<const uint8*>(subData.pData);
            memcpy(dst + DestData.RowPitch * j,
                src + subData.RowPitch * j,
                rowSizeInBytes[i]);
        }
    }

    vBufferUploadHeap->Unmap(0, nullptr);
    for (uint32 i = 0; i < imgData.mipCount; i++) {
        D3D12_TEXTURE_COPY_LOCATION destination = {};
        destination.pResource = resource;
        destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        destination.SubresourceIndex = i;

        D3D12_TEXTURE_COPY_LOCATION source = {};
        source.pResource = vBufferUploadHeap;
        source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        source.PlacedFootprint = layouts[i];
        m_commandList[m_frameIndex]->CopyTextureRegion(&destination, 0,0,0, &source, nullptr);
    }
    
    m_toRelease.push_back(vBufferUploadHeap);
    return true;
}

void AwesomeGraphics::WaitForNextFrame()
{
    m_currentFrame++;
    if (FAILED(m_commandQueue->Signal(m_fence, m_currentFrame)))
        return;

// 0 will force a wait at the end of each frame for the GPU to finish.
#if 1
    m_lastCompleted = std::max(m_fence->GetCompletedValue(), m_lastCompleted);
    uint64 lag = m_currentFrame - m_lastCompleted;
    if(lag >= c_frameBufferCount)
#else
    m_lastCompleted = m_fence->GetCompletedValue();
    if (m_currentFrame > m_lastCompleted)
#endif
    {
        if (FAILED(m_fence->SetEventOnCompletion(m_currentFrame, m_fenceEvent)))
            return;
        WaitForSingleObject(m_fenceEvent, INFINITE);
    }
}

bool AwesomeGraphics::FlushGPU()
{
    m_currentFrame++;
    if (FAILED(m_commandQueue->Signal(m_fence, m_currentFrame)))
        return false;

    if (FAILED(m_fence->SetEventOnCompletion(m_currentFrame, m_fenceEvent)))
        return false;

    WaitForSingleObject(m_fenceEvent, INFINITE);

    m_lastCompleted = m_currentFrame;
    
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

    return true;
}

void AwesomeGraphics::EndFence()
{
    CloseHandle(m_fenceEvent);
}

void AwesomeGraphics::SetViewport()
{
    PIXScopedEvent(GetCommandList(), 0, "Set Viewport");
    m_commandList[m_frameIndex]->RSSetViewports(1, GetViewport());
    m_commandList[m_frameIndex]->RSSetScissorRects(1, GetScissorRect());
}

bool AwesomeGraphics::Resize(uint32 width, uint32 height)
{
    if (m_width != width || m_height != height)
    {
        
        m_width = std::max(1u, width);
        m_height = std::max(1u, height);

        return ResetSize();

    }
    return true;
}

bool AwesomeGraphics::ResetSize()
{
    if (!FlushGPU())
        return false;

    for (uint16 i = 0; i < c_frameBufferCount; ++i)
    {
        SafeRelease(m_renderTargets[i]);
        SafeRelease(m_depthStencilBuffer[i]);
    }

    

    DXGI_SWAP_CHAIN_DESC swapChainDesc = {};
    if (FAILED(m_swapChain->GetDesc(&swapChainDesc)))
        return false;

    if (FAILED(m_swapChain->ResizeBuffers(c_frameBufferCount, m_width, m_height,
        GetSwapChainFormat(), swapChainDesc.Flags)))
        return false;

    if (FAILED(m_swapChain->SetColorSpace1(m_hdr ? DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020 : DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709)))
        return false;

    // Create Render Target Views (Back Buffer) 
    if (!UpdateRenderTargetViews())
        return false;

    // Create Depth/Stencil Buffer Resource
    if (!UpdateDepthStencilView())
        return false;

    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

    UpdateViewport();

    if (m_scene) {
        m_scene->GetCamera()->UpdateSize(m_width, m_height);
        m_screenSpaceShadows->Resize();
        m_deferredRenderer->Resize();
    }

    m_postFX->Resize();
    m_taa->Resize();
    //m_hairSystem->Resize();
    m_UISystem->Resize();

    DebugPrint("Resized to: %dx%d\n", m_width, m_height);

    return true;
}

void AwesomeGraphics::Fullscreen(bool value)
{
    m_fullscreen = value;
}

void AwesomeGraphics::TransitionResource(ID3D12Resource* resource, D3D12_RESOURCE_STATES prevState, D3D12_RESOURCE_STATES nexState)
{
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = resource;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = prevState;
    barrier.Transition.StateAfter = nexState;

    m_commandList[m_frameIndex]->ResourceBarrier(1, &barrier);
}

ID3D12Device* AwesomeGraphics::Device() const { return m_device; }

ID3D12GraphicsCommandList4* AwesomeGraphics::GetCommandList4() const
{
    return m_commandList4[m_frameIndex];
}

ID3D12GraphicsCommandList* AwesomeGraphics::GetCommandList() const
{
    return m_commandList[m_frameIndex];
}

ID3D12CommandQueue* AwesomeGraphics::GetCommandQueue() const
{
    return m_commandQueue;
}

const D3D12_VIEWPORT* AwesomeGraphics::GetViewport() const
{
    return &m_viewport;
}

const D3D12_RECT* AwesomeGraphics::GetScissorRect() const
{
    return &m_scissorRect;
}

void AwesomeGraphics::PlayWorms()
{
    m_playWorms = true;
}

ID3D12Resource* AwesomeGraphics::GetFinalOut() const
{
    return m_postFX->GetOutputBuffer();
}

uint32 AwesomeGraphics::AddScene(Scene& scene)
{
    uint32 index = uint32(m_scenes.size());
    m_scenes.push_back(scene);
    return index;
}

Scene* AwesomeGraphics::GetScene(uint32 index)
{
    if (index < m_scenes.size())
        return &m_scenes[index];
    return nullptr;
}

Scene* AwesomeGraphics::GetCurrentScene() const
{
    return m_scene;
}

bool AwesomeGraphics::SetCurrentScene(Scene* scene)
{
    if (scene)
    {
        m_scene = scene;
        m_iblSystem->SetCurrentIBL(m_scene->GetIBL());
        m_scene->MarkLightsDirty();
        return true;
    }
    return false;
}

bool AwesomeGraphics::SetCurrentScene(uint32 index)
{
    if (index < m_scenes.size())
    {
        return SetCurrentScene(&m_scenes[index]);
    }
    return false;
}

uint32 AwesomeGraphics::GetSceneCount() const
{
    return (uint32)m_scenes.size();
}

ResourcePool* AwesomeGraphics::GetResourcePool()
{
    return m_resourcePool;
}

DescriptorHeap* AwesomeGraphics::GetMainDescHeap()
{
    return m_mainCbvSrvUavDescHeap;
}

QuadMesh* AwesomeGraphics::GetQuad()
{
    return m_quad;
}

MaterialSystem* AwesomeGraphics::GetMaterialSystem()
{
    return m_materialSystem;
}

MeshSystem* AwesomeGraphics::GetMeshSystem()
{
    return m_meshSystem;
}

TextureSystem* AwesomeGraphics::GetTextureSystem()
{
    return m_textureSystem;
}

ComputeSystem* AwesomeGraphics::GetComputeSystem()
{
    return m_computeSystem;
}

DeferredRenderer* AwesomeGraphics::GetDeferredRenderer()
{
    return m_deferredRenderer;
}

TemporalAntiAliasing* AwesomeGraphics::GetTAA()
{
    return m_taa;
}

IBLSystem* AwesomeGraphics::GetIBLSystem()
{
    return m_iblSystem;
}

ScreenSpaceShadows* AwesomeGraphics::GetSSShadows()
{
    return m_screenSpaceShadows;
}

//HairSystem* AwesomeGraphics::GetHairSystem()
//{
//    return m_hairSystem;
//}

PostFX* AwesomeGraphics::GetPostFX()
{
    return m_postFX;
}

UISystem* AwesomeGraphics::GetUISystem()
{
    return m_UISystem;
}

DirectionalShadows* AwesomeGraphics::GetDirectShadows()
{
    return m_directShadows;
}

TileLightCull* AwesomeGraphics::GetTiledLights()
{
    return m_tileLightCulling;
}

GTAO* AwesomeGraphics::GetGTAO()
{
    return m_gtao;
}

AwesomeProfiler* AwesomeGraphics::GetProfiler()
{
    return m_profiler;
}

void AwesomeGraphics::SetHDR(bool value)
{
    if (value != m_hdr)
    {
        m_hdr = value;
        ResetSize();
    }
}

bool AwesomeGraphics::Render(float delta)
{
    m_profiler->Readback();
    
    PIXScopedEvent(m_commandQueue, 0, "Begin Frame");
    if (!BeginFrame(true))
    {
        DebugPrint("Begin Frame Failed, Exiting...");
        return false;
    }

    m_mainCbvSrvUavDescHeap->ReleaseHandles();

    ID3D12DescriptorHeap* descriptorHeaps[] = { m_mainCbvSrvUavDescHeap->GetHeap()};
    GetCommandList()->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_rtvDescHeap->GetCPUDescriptorHandleForHeapStart();
    rtvHandle.ptr += m_frameIndex * m_rtvDescSize;
    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = m_dsDescHeap->GetCPUDescriptorHandleForHeapStart();
    dsvHandle.ptr += m_frameIndex * m_dsvDescSize;

    m_scene->GetCamera()->StartFrame();
    m_scene->AnimateLights(delta);
    m_tileLightCulling->BuildLightBVH(m_scene);
    
    uint32 scopeIdx = 0;

    {
        PIXScopedEvent(GetCommandList(), 0, "Draw Scene");
        m_screenSpaceShadows->SetupRender();
        m_deferredRenderer->PreRender(dsvHandle);
        m_materialSystem->SetupRender();
        m_meshSystem->SetupRender();

        {
            PROFILE_SCOPE(GetProfiler(), GetCommandList(), "Draw Opaque Meshes", scopeIdx++);
            PIXScopedEvent(GetCommandList(), 0, "Draw Opaque Meshes");
            m_scene->Render(m_meshSystem, delta);
        }

        {
            PROFILE_SCOPE(GetProfiler(), GetCommandList(), "Draw Sun Shadow", scopeIdx++);
            PIXScopedEvent(GetCommandList(), 0, "Draw Sun Shadow");
            DirectionalShadowSettings dsSettings = m_directShadows->GetSettings();
            m_directShadows->PreRender();
            for (uint32 c = 0; c < dsSettings.cascades; c++) // lol
            {
                PIXScopedEvent(GetCommandList(), 0, "Shadow Cascade %d", c);
                m_directShadows->SetupRender(c);
                float nC = c == 0 ? 0.5f : dsSettings.cascadeRanges[c - 1];
                float fC = dsSettings.cascadeRanges[c];

                ViewFrustum frustum = m_scene->GetCamera()->GetFrustum(nC, fC);
                XMMATRIX viewProj = m_directShadows->GetCascadeMatrix(c, frustum, m_scene->GetSunLight());
                m_scene->RenderShadow(m_meshSystem, c, viewProj);
            }
            SetViewport();
        }
        m_materialSystem->PostRender();
        m_deferredRenderer->PostRender(rtvHandle, dsvHandle);
    }
    
    {
        PROFILE_SCOPE(GetProfiler(), GetCommandList(), "Screenspace Shadows", scopeIdx++);
        m_screenSpaceShadows->Render(delta);
    }
    
    {
        PROFILE_SCOPE(GetProfiler(), GetCommandList(), "Tiled Lighting", scopeIdx++);
        m_tileLightCulling->BulldLightTiles();
    }
    
    {
        PROFILE_SCOPE(GetProfiler(), GetCommandList(), "GTAO", scopeIdx++);
        m_gtao->Render(delta);
    }
    
    {
        PROFILE_SCOPE(GetProfiler(), GetCommandList(), "Deferred Lighting", scopeIdx++);
        m_deferredRenderer->Render(delta);
    }

    m_scene->GetCamera()->EndFrame();
    
    ID3D12Resource* postFxInput = m_deferredRenderer->GetOutputBuffer(); 

    if (m_taaEnabled)
    {
        PROFILE_SCOPE(GetProfiler(), GetCommandList(), "TAA", scopeIdx++);
        m_taa->Render(delta);
        postFxInput = m_taa->GetOutput(); 
    }
    else
    {
        TransitionResource(postFxInput, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }
    {
        PROFILE_SCOPE(GetProfiler(), GetCommandList(), "Post Stack", scopeIdx++);
        m_postFX->Render(m_postFXEnabled, delta, postFxInput);
    }
    TransitionResource(m_deferredRenderer->GetOutputBuffer(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    m_deferredRenderer->FreeResources();

    m_UISystem->PreRender(dsvHandle);

    {
        PROFILE_SCOPE(GetProfiler(), GetCommandList(), "UI", scopeIdx++);
        m_UISystem->Render();
    
        if (m_hasImgui)
        {
            PROFILE_SCOPE(GetProfiler(), GetCommandList(), "ImGui", scopeIdx++);
            PIXScopedEvent(GetCommandList(), 0, "Draw ImGui");
        
            ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), GetCommandList());
            const float blend_factor[4] = { 1.f, 1.f, 1.f, 1.f };
            GetCommandList()->OMSetBlendFactor(blend_factor);
        }

        m_UISystem->PostRender(rtvHandle, dsvHandle);

        SetViewport();

        m_UISystem->FinalComposite();
    }
    
    m_profiler->Resolve(GetCommandList());
    
    if (!EndFrame(true))
    {
        DebugPrint("End Frame Failed, Exiting...");
        return false;
    }

    return true;
}

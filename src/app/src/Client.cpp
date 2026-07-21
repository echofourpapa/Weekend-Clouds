#include "Awesome.h"
#include <windows.h>
#include "Scene.h"
#include "IBL.h"
#include "Util.h"
#include "windowsx.h"
#include "MeshImporter.h"
#include "ResourceLoader.h"
#include "ScreenSpaceShadows.h"
#include "DirectionalShadows.h"
#include "CloudSystem.h"
#include "CloudGenerator.h"
#include "Deferred.h"
#include "PostFX.h"
#include "AwesomeProfiler.h"
#include "DescriptorHeap.h"
#include "MeshData.h"
#include <chrono>
#include "imgui.h"
#include "imgui_internal.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx12.h"

#include "entt/entt.hpp"

#include <future>

extern "C" { __declspec(dllexport) extern const UINT D3D12SDKVersion = 615; }

extern "C" { __declspec(dllexport) extern const char* D3D12SDKPath = u8".\\D3D12\\"; }

#define W_UP 0x57
#define S_DOWN 0x53
#define D_RIGHT 0x44
#define A_LEFT 0x41

LPCTSTR WindowClassName = L"AwesomeWindow";
LPCTSTR WindowTitle = L"Awesome Thing";

Awesome::AwesomeGraphics* g_Awesome = nullptr;

uint32 last_mouse_x = -1;
uint32 last_mouse_y = -1;

bool fpsMode = false;

const uint32 c_lightingFlagCount = 11;

const char* c_lightingNames[c_lightingFlagCount] = {
    "Direct Diffuse",
    "Direct Specular",
    "Indirect Diffuse",
    "Indirect Specular",
    "Directional Shadows",
    "Screen Space Shadows",
    "GTAO",
    "HBIL",
    "Cascade Debug",
    "Sheen",
    "Dummy"
};

const char* c_tonemapperNames[2] = {
    "ACES",
    "Gran Turismo 7"
};

const Awesome::DeferredRenderer::LightingFlags c_lightingFlags[c_lightingFlagCount] = {
    Awesome::DeferredRenderer::LightingFlags::DirectDiffuse,
    Awesome::DeferredRenderer::LightingFlags::DirectSpecular,
    Awesome::DeferredRenderer::LightingFlags::IndirectDiffuse,
    Awesome::DeferredRenderer::LightingFlags::IndirectSpecular,
    Awesome::DeferredRenderer::LightingFlags::DirectionalShadows,
    Awesome::DeferredRenderer::LightingFlags::SSShadows,
    Awesome::DeferredRenderer::LightingFlags::GTAO,
    Awesome::DeferredRenderer::LightingFlags::HBIL,
    Awesome::DeferredRenderer::LightingFlags::CascadeDebug,
    Awesome::DeferredRenderer::LightingFlags::Sheen,
    Awesome::DeferredRenderer::LightingFlags::Dummy
};

template<typename T>
bool future_is_ready(std::future<T>& t) {
    return t.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}

static void HelpMarker(const char* desc)
{
    ImGui::TextDisabled("(?)");
    if (ImGui::BeginItemTooltip())
    {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
        ImGui::TextUnformatted(desc);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

bool SliderUInt(const char* label, uint32* v, uint32 v_min, uint32 v_max, const char* format = "%d", ImGuiSliderFlags flags = 0)
{
    return ImGui::SliderScalar(label, ImGuiDataType_U32, v, &v_min, &v_max, format, flags);
}

// Forward declare message handler from imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    static bool s_in_sizemove = false;
    
    auto imguiR = ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam);
    if (imguiR != 0)
        return imguiR;

    switch (msg)
    {

    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) {
            if (MessageBox(0, L"Are you sure you want to exit?",
                L"Really?", MB_YESNO | MB_ICONQUESTION) == IDYES)
                DestroyWindow(hwnd);
        }
        if (g_Awesome->GetCurrentScene())
        {
            int32 speed = GetKeyState(VK_SHIFT) < 0 ? 5 : 1;
            if (wParam == VK_UP || wParam == W_UP)
            {
                g_Awesome->GetCurrentScene()->GetCamera()->WalkForward(-speed);
            }
            else if (wParam == VK_DOWN || wParam == S_DOWN)
            {
                g_Awesome->GetCurrentScene()->GetCamera()->WalkForward(speed);
            }
            else if (wParam == VK_RIGHT || wParam == D_RIGHT)
            {
                g_Awesome->GetCurrentScene()->GetCamera()->WalkSide(speed);
            }
            else if (wParam == VK_LEFT || wParam == A_LEFT)
            {
                g_Awesome->GetCurrentScene()->GetCamera()->WalkSide(-speed);
            }
        }
        break;
    case WM_MOUSEMOVE:
        {
            uint32 x = GET_X_LPARAM(lParam);
            uint32 y = GET_Y_LPARAM(lParam);
            if (last_mouse_x == -1 || last_mouse_y == -1)
            {
                last_mouse_x = x;
                last_mouse_y = y;
            }
            int32 delta_x = x - last_mouse_x;
            int32 delta_y = y - last_mouse_y;
            if (g_Awesome->GetCurrentScene())
            {
                if (GetKeyState(VK_MENU) < 0 )
                {
                    fpsMode = false;
                    if (wParam & MK_LBUTTON)
                    {
                        g_Awesome->GetCurrentScene()->GetCamera()->Orbit(delta_x, delta_y);
                    }
                    else if (wParam & MK_RBUTTON)
                    {
                        if (abs(delta_x) > abs(delta_y))
                            g_Awesome->GetCurrentScene()->GetCamera()->Zoom(delta_x);
                        else
                            g_Awesome->GetCurrentScene()->GetCamera()->Zoom(delta_y);
                    }
                    else if (wParam & MK_MBUTTON)
                    {
                        g_Awesome->GetCurrentScene()->GetCamera()->Pan(delta_x, delta_y);
                    }
                }
                else if (fpsMode)
                {
                    g_Awesome->GetCurrentScene()->GetCamera()->Look(delta_x, delta_y);
                }
            }
            last_mouse_x = x;
            last_mouse_y = y;
        }
        break;
    case WM_LBUTTONDOWN:
        {
            if (GetKeyState(VK_CONTROL) < 0)
            {
                fpsMode = !fpsMode;
            }
        }
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    case WM_ENTERSIZEMOVE:
        s_in_sizemove = true;
        break;

    case WM_EXITSIZEMOVE:
        s_in_sizemove = false;
        if (g_Awesome)
        {
            RECT rc;
            GetClientRect(hwnd, &rc);

            if (!g_Awesome->Resize(rc.right - rc.left, rc.bottom - rc.top))
                DebugPrint("Resize failed!\n");
        }
        break;
    case WM_SIZE:
    {
        UINT width = LOWORD(lParam);
        UINT height = HIWORD(lParam);
        if (g_Awesome && !s_in_sizemove)
        {
            if (!g_Awesome->Resize(width, height))
                DebugPrint("Resize failed!\n");
        }
    }
        break;
    default:
        return DefWindowProc(hwnd,
            msg,
            wParam,
            lParam);
    }
    return 0;
}

HWND SetupWindow(uint16 width, uint16 height)
{
    HINSTANCE hInstance = GetModuleHandle(NULL);
    
    HICON hIcon = LoadIcon(hInstance, L"MAINICON");

    WNDCLASSEX wc =
    {
        sizeof(WNDCLASSEX),     //UINT        cbSize;
                                ///* Win 3.x */
        CS_CLASSDC,             //UINT        style;
        WndProc,                //WNDPROC     lpfnWndProc;
        NULL,                   //int         cbClsExtra;
        NULL,                   //int         cbWndExtra;
        hInstance,              //HINSTANCE   hInstance;
        hIcon,                  //HICON       hIcon;
        NULL,                   //HCURSOR     hCursor;
        NULL,                   //HBRUSH      hbrBackground;
        NULL,                   //LPCWSTR     lpszMenuName;
        WindowClassName,        //LPCWSTR     lpszClassName;
                                ///* Win 4.0 */
        hIcon                   //HICON       hIconSm;
    };

    if (!RegisterClassEx(&wc))
    {
        MessageBox(NULL, L"Error registering class",
            L"Error", MB_OK | MB_ICONERROR);
        return NULL;
    }
    
    RECT rc = { 0, 0, width, height };
    AdjustWindowRect(&rc, WS_CAPTION | WS_SYSMENU | WS_THICKFRAME, FALSE);
    HWND hwnd = CreateWindowEx(
        NULL,                   //_In_ DWORD dwExStyle,
        wc.lpszClassName,       //_In_opt_ LPCWSTR lpClassName,
        WindowTitle,            //_In_opt_ LPCWSTR lpWindowName,
        WS_OVERLAPPEDWINDOW,    //_In_ DWORD dwStyle,
        100,                    //_In_ int X,
        100,                    //_In_ int Y,
        rc.right - rc.left,                   //_In_ int nWidth,
        rc.bottom - rc.top,                    //_In_ int nHeight,
        NULL,                   //_In_opt_ HWND hWndParent,
        NULL,                   //_In_opt_ HMENU hMenu,
        wc.hInstance,           //_In_opt_ HINSTANCE hInstance,
        NULL                    //_In_opt_ LPVOID lpParam
    );

    if (!hwnd)
    {
        MessageBox(NULL, L"Error creating window",
            L"Error", MB_OK | MB_ICONERROR);
        return NULL;
    }

    return hwnd;
}

bool ProcessMessages()
{
    MSG msg;
    ZeroMemory(&msg, sizeof(MSG));
    if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
        if (msg.message == WM_QUIT)
            return false;
    }
    return true;
}

void DisplayXForm(Awesome::Transform* xform)
{
    ImGui::PushID(xform);
    if (ImGui::CollapsingHeader("Transform"))
    {
        bool update = false;

        float p[3] = {
        xform->position.x,
        xform->position.y,
        xform->position.z };

        if (ImGui::DragFloat3("Position", p))
        {
            xform->position.x = p[0];
            xform->position.y = p[1];
            xform->position.z = p[2];
            update = true;
        }

        float r[3] = {
        xform->rotation.x,
        xform->rotation.y,
        xform->rotation.z };

        if (ImGui::DragFloat3("Rotation", r))
        {
            xform->rotation.x = r[0];
            xform->rotation.y = r[1];
            xform->rotation.z = r[2];
            update = true;
        }

        float s[3] = {
        xform->scale.x,
        xform->scale.y,
        xform->scale.z };

        if (ImGui::DragFloat3("Scale", s))
        {
            xform->scale.x = s[0];
            xform->scale.y = s[1];
            xform->scale.z = s[2];
            update = true;
        }

        if (update)
            xform->CalcMatrix();
    }
    ImGui::PopID();
}

bool DisplayLight(Awesome::Light* light)
{
    ImGui::PushID(light);

    bool update = false;

    float p[3] = {
    light->position.x,
    light->position.y,
    light->position.z };

    if (ImGui::DragFloat3("Position", p))
    {
        light->position.x = p[0];
        light->position.y = p[1];
        light->position.z = p[2];
        update = true;
    }

    float c[3] = {
    light->color.x,
    light->color.y,
    light->color.z };

    if (ImGui::ColorEdit3("Color", c))
    {
        light->color.x = c[0];
        light->color.y = c[1];
        light->color.z = c[2];
    }

    if (ImGui::DragFloat("Intensity", &light->intensity, 1.0f, 0.f))
    {
        update = true;
    }

    ImGui::PopID();

    return update;
}

void ClientLoop(Awesome::AwesomeGraphics& Awesome)
{
    DebugPrint("Starting Game Loop.\n");
    MSG msg = {};
    auto t_start = std::chrono::high_resolution_clock::now();

    bool loadScene = false;
    bool sceneLoaded = false;
    bool playWorms = false;
    bool hdrReset = false;

    static char projectPath[MAX_PATH] = "D:\\code\\toy-renderer\\data\\vProject.toml";
    while (true)
    {
        if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if (msg.message == WM_QUIT)
                break;
        }
        else {

            if (loadScene)
            {
                Awesome.FlushGPU();
                sceneLoaded = Awesome::ResourceLoader::LoadResources(&Awesome, projectPath);
                if (sceneLoaded)
                {
                    Awesome.SetCurrentScene(0u);
                    // This is so that if a user changes the size of the window before the scene is loaded, everything get's resized after.
                    Awesome.ResetSize();
                }
                loadScene = false;
            }

            // Start the Dear ImGui frame
            ImGui_ImplDX12_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();
            {
                ImGui::Begin("Main");
                if(ImGui::CollapsingHeader("Info"))
                {
                    ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
                    {
                        float currentMem = Awesome.GetCurrentMemory();
                        float totalMem = Awesome.GetTotalMemory();
                        ImGui::Text("GPU memory usage %.3f mb/%.3f mb", currentMem, totalMem);
                    }
                    ImGui::Text("D3D12 caps: RT tier %u.%u | SM %u.%u | R11G11B10 UAV loads %s",
                        Awesome.GetRaytracingTier() / 10, Awesome.GetRaytracingTier() % 10,
                        Awesome.GetHighestShaderModel() >> 4, Awesome.GetHighestShaderModel() & 0xF,
                        Awesome.AreTypedUAVLoadsSupported() ? "yes" : "NO");
                    ImGui::Separator();
                    ImGui::Text("Profiler Name : Current MS | Average MS");
                    std::vector<Awesome::ProfileData> profileData;
                    for (const Awesome::ProfileData& p : Awesome.GetProfiler()->GetProfiles())
                    {
                        if (!p.valid) 
                            continue;

                        if (p.name.empty())
                            continue;
                        
                        profileData.push_back(p);
                    }
                    uint32 mid = static_cast<uint32>(profileData.size()) / 2;
                    ImGui::Columns(2);
                    for (uint32 i = 0; i < mid; i++)
                    {
                        ImGui::Text("%-20s : %6.3f ms |  %6.3f ms", profileData[i].name.c_str(), profileData[i].timeMs, profileData[i].avgTimeMs);
                    }
                    ImGui::NextColumn();
                    for (uint32 i = mid; i < profileData.size(); i++)
                    {
                        ImGui::Text("%-20s : %6.3f ms |  %6.3f ms", profileData[i].name.c_str(), profileData[i].timeMs, profileData[i].avgTimeMs);
                    }
                    ImGui::Columns(1);
                }

                if (ImGui::CollapsingHeader("Scene"))
                {
                    if (sceneLoaded)
                    {
                        ImGui::Text("'ctrl + left-click' to toggle FPS mode");
                        ImGui::Text("'atl + LMB/MMB/RMB' for Maya controls (will exit FPS mode)");
                        if (fpsMode)
                        {
                            ImGui::SetMouseCursor(ImGuiMouseCursor_None);
                            ImGui::Text("FPS Mode ON");
                        }
                        else
                        {
                            ImGui::Text("FPS Mode OFF");
                        }
                        ImGui::Separator();
                        Awesome::Camera* cam = Awesome.GetCurrentScene()->GetCamera();
                        ImGui::Text("Camera Position: X: %.3f Y: %.3f Z: %.3f ", cam->transform.position.x, cam->transform.position.y, cam->transform.position.z);
                        ImGui::Text("Camera Rotation: X: %.3f Y: %.3f Z: %.3f ", cam->transform.rotation.x, cam->transform.rotation.y, cam->transform.rotation.z);
                        ImGui::Text("Camera Target: X: %.3f Y: %.3f Z: %.3f ", cam->target.x, cam->target.y, cam->target.z);
                        ImGui::Separator();
                        float v[3] = {
                        Awesome.GetCurrentScene()->GetSunLight()->position.x,
                        Awesome.GetCurrentScene()->GetSunLight()->position.y,
                        Awesome.GetCurrentScene()->GetSunLight()->position.z };

                        if (ImGui::DragFloat3("Sun Positon", v))
                        {
                            Awesome.GetCurrentScene()->GetSunLight()->position.x = v[0];
                            Awesome.GetCurrentScene()->GetSunLight()->position.y = v[1];
                            Awesome.GetCurrentScene()->GetSunLight()->position.z = v[2];
                            Awesome.GetCurrentScene()->GetSunLight()->UpdateMatrix();
                        }

                        ImGui::DragFloat("Sun Intensity", &Awesome.GetCurrentScene()->GetSunLight()->intensity);
                        ImGui::Separator();
                        float skyBlur = Awesome.GetIBLSystem()->GetSkyBlur();
                        ImGui::SliderFloat("Sky Blur", &skyBlur, 0.0f, 1.0f);
                        Awesome.GetIBLSystem()->SetSkyBlur(skyBlur);
                        ImGui::Separator();
                        Awesome::Scene* currentScene = Awesome.GetCurrentScene();
                        if (ImGui::BeginCombo("Scenes", currentScene->GetName()))
                        {
                            uint32 sceneCount = Awesome.GetSceneCount();
                            for (uint32 i = 0; i < sceneCount; i++)
                            {
                                Awesome::Scene* scene = Awesome.GetScene(i);
                                bool selected = scene == currentScene;
                                if (ImGui::Selectable(scene->GetName(), selected))
                                {
                                    Awesome.SetCurrentScene(i);
                                }
                            }
                            ImGui::EndCombo();
                        }
                        ImGui::Separator();
                        uint32 currentIblIdx = Awesome.GetIBLSystem()->GetCurrentIBL();
                        const char* current_ibl = Awesome.GetIBLSystem()->GetIBLName(currentIblIdx).c_str();
                        if (ImGui::BeginCombo("IBLs", current_ibl))
                        {
                            uint32 iblCount = Awesome.GetIBLSystem()->GetIBLCount();
                            for (uint32 i = 0; i < iblCount; i++)
                            {
                                bool selected = currentIblIdx == i;
                                if (ImGui::Selectable(Awesome.GetIBLSystem()->GetIBLName(i).c_str(), selected))
                                {
                                    Awesome.GetIBLSystem()->SetCurrentIBL(i);
                                }
                            }
                            ImGui::EndCombo();
                        }
                        ImGui::Separator();
                        ImGui::Checkbox("Animate Debug Lights", &Awesome.GetCurrentScene()->m_animateLights);
                        ImGui::Separator();
                        if (ImGui::CollapsingHeader("Lights"))
                        {
                            ImGui::Indent();

                            uint32 lightCount = Awesome.GetCurrentScene()->GetLightCount(Awesome::LightType::Point);
                            
                            ImGui::BeginDisabled(lightCount > 1024);
                            bool debugLights = currentScene->GetDebugLights();
                            ImGui::Checkbox("Draw Debug Lights", &debugLights);
                            currentScene->SetDebugLights(debugLights);
                            ImGui::EndDisabled();
                            if (ImGui::Button("Add Point Light"))
                            {
                                Awesome::Light light = {};
                                light.type = Awesome::LightType::Point;
                                light.position.x = 0;
                                light.position.y = 0;
                                light.position.z = 0;
                                light.intensity = 1;
                                light.color = { 1,1,1 };
                                Awesome.GetCurrentScene()->AddLight(light);
                            }

                            for (uint32 l = 0; l < lightCount; l++)
                            {
                                Awesome::Light* light = Awesome.GetCurrentScene()->GetPointLight(l);
                                ImGui::PushID(light);

                                char lightName[256];
                                sprintf_s(lightName, "Point Light %d", l);

                                if (ImGui::CollapsingHeader(lightName))
                                {
                                    if (DisplayLight(light))
                                        Awesome.GetCurrentScene()->MarkLightsDirty();
                                }
                                ImGui::PopID();
                            }
                            ImGui::Unindent();
                        }

                        ImGui::Separator();
                        if (ImGui::CollapsingHeader("Objects"))
                        {
                            ImGui::Indent();
                            uint32 objCount = Awesome.GetCurrentScene()->GetObjectCount();
                            for (uint32 i = 0; i < objCount; i++)
                            {
                                Awesome::SceneObject* obj = Awesome.GetCurrentScene()->GetSceneObject(i);
                                if (obj)
                                {
                                    ImGui::PushID(obj);
                                    if (ImGui::CollapsingHeader(obj->name.c_str()))
                                    {
                                        ImGui::Indent();
                                        ImGui::Checkbox("Visible", &obj->visibile);
                                        DisplayXForm(&obj->transform);
                                        if (ImGui::CollapsingHeader("Meshes"))
                                        {
                                            ImGui::Indent();
                                            for (uint32 j = 0; j < obj->meshes.size(); j++)
                                            {
                                                Awesome::MeshData* md = Awesome.GetMeshSystem()->GetMeshData(obj->meshes[j]);
                                                ImGui::PushID(md);
                                                if (ImGui::CollapsingHeader(md->name.c_str()))
                                                {
                                                    ImGui::Checkbox("Visible", &md->visible);
                                                    DisplayXForm(&md->transform);
                                                }
                                                ImGui::PopID();
                                            }
                                            ImGui::Unindent();
                                        }

                                        ImGui::Unindent();
                                    }
                                    ImGui::PopID();
                                }
                            }
                            ImGui::Unindent();
                        }
                    }
                    else if (playWorms)
                    {
                        if (ImGui::Button("Stop Worms"))
                        {
                            playWorms = false;
                        }
                    }
                    else
                    {
                        ImGui::InputText("Project To Load", projectPath, MAX_PATH);
                        if (ImGui::Button("Load"))
                        {
                            loadScene = true;
                        }
                    }
                }

                if (ImGui::CollapsingHeader("Renderer"))
                {
                    ImGui::Columns(2);
                    ImGui::Text("Render Debug Views");
                    int e = (int)Awesome.GetDeferredDebugFlags();
                    ImGui::RadioButton("Lit", &e, int(Awesome::DebugFlags::Lit));
                    ImGui::RadioButton("Albedo", &e, int(Awesome::DebugFlags::Albedo));
                    ImGui::RadioButton("Normal", &e, int(Awesome::DebugFlags::Normal));
                    ImGui::RadioButton("Metal", &e, int(Awesome::DebugFlags::Metal));
                    ImGui::RadioButton("Roughness", &e, int(Awesome::DebugFlags::Roughness));
                    ImGui::RadioButton("Motion", &e, int(Awesome::DebugFlags::Motion));
                    ImGui::RadioButton("Screen Space Shadows", &e, int(Awesome::DebugFlags::SSShadows));
                    ImGui::RadioButton("Directional Shadows", &e, int(Awesome::DebugFlags::Shadow_MUL));
                    ImGui::RadioButton("World Position", &e, int(Awesome::DebugFlags::Depth));
                    ImGui::RadioButton("Light Tiles Heapmap", &e, int(Awesome::DebugFlags::LightTiles));
                    ImGui::RadioButton("Light Tiles Bounds", &e, int(Awesome::DebugFlags::TileBounds));
                    ImGui::RadioButton("GTAO", &e, int(Awesome::DebugFlags::GTAODebug));
                    ImGui::RadioButton("Bent Normals", &e, int(Awesome::DebugFlags::BentNormals));
                    Awesome.SetDeferredDebugFlags((Awesome::DebugFlags)e);
                    ImGui::Text("Lighting Channels");
                    {
                        ImGui::PushID("Lighting Flags");
                        std::string flag_string;
                        uint32 curFlags = Awesome.GetLightingDebugFlags();
                        for (uint32 f = 0; f < c_lightingFlagCount; f++)
                        {
                            Awesome::DeferredRenderer::LightingFlags flag = c_lightingFlags[f];
                            ImGui::CheckboxFlags(c_lightingNames[f], &curFlags, flag);
                        }

                        for (uint32 i = 0; i < 32; i++)
                        {
                            uint32 testI = 31 - i;
                            uint32 testFlag = 1 << testI;
                            flag_string += (curFlags & testFlag) ? "1" : "0";
                        }
                        Awesome.SetLightingDebugFlags(curFlags);
                        ImGui::Text(flag_string.c_str());
                        ImGui::PopID();
                    }
                    ImGui::NextColumn();
                    ImGui::Text("Render Options");
                    ImGui::SliderFloat("Sheen Tint", &Awesome.GetDeferredRenderer()->m_sheenTint, 0.f, 1.f);
                    bool fs = Awesome.GetFullScreen();
                    if (ImGui::Checkbox("Fullscreen", &fs))
                    {
                        uint32 width = 1920;
                        uint32 height = 1080;
                        if (!fs)
                        {
                            SetWindowLongPtr(Awesome.GetHWND(), GWL_STYLE, WS_VISIBLE | WS_OVERLAPPEDWINDOW);
                            SetWindowPos(Awesome.GetHWND(), NULL, 0, 0, width, height, SWP_FRAMECHANGED);
                        }
                        else
                        {
                            width = GetSystemMetrics(SM_CXSCREEN);
                            height = GetSystemMetrics(SM_CYSCREEN);
                            SetWindowLongPtr(Awesome.GetHWND(), GWL_STYLE, WS_VISIBLE | WS_POPUP);
                            SetWindowPos(Awesome.GetHWND(), HWND_TOP, 0, 0, width, height, SWP_FRAMECHANGED);
                        }
                        g_Awesome->Resize(width, height);
                    }

                    Awesome.Fullscreen(fs);
                    
                    bool vsync = Awesome.GetVSyncEnabled();
                    ImGui::Checkbox("VSync Enabled", &vsync);
                    Awesome.SetVSyncEnabled(vsync);
                    if (vsync)
                    {
                        int interval = int(Awesome.GetVSyncInterval());
                        ImGui::SliderInt("VSync Interval", &interval, 1, 4);
                        Awesome.SetVSyncInterval(uint16(interval));
                    }

                    bool taa = Awesome.GetTAAEnabled();
                    ImGui::Checkbox("TAA Enabled", &taa);
                    Awesome.SetTAAEnabled(taa);

                    bool postFX = Awesome.GetPostFXEnabled();
                    ImGui::Checkbox("Tonemapping Enabled", &postFX);
                    Awesome.SetPostFXEnabled(postFX);
                    
                    if (postFX)
                    {
                        Awesome::Tonemapper tonemapper = Awesome.GetPostFX()->GetTonemapper();
                        if (ImGui::BeginCombo("Tonemapper", c_tonemapperNames[tonemapper]))
                        {
                            for (uint32 i = 0; i < 2; i++)
                            {
                                bool selected = i == tonemapper;
                                if (ImGui::Selectable(c_tonemapperNames[i], selected))
                                {
                                    Awesome.GetPostFX()->SetTonemapper(static_cast<Awesome::Tonemapper>(i));
                                }
                            }
                            ImGui::EndCombo();
                        }
                    }
                    
                    float logMin = Awesome.GetPostFX()->GetLogMin();
                    float logMax = Awesome.GetPostFX()->GetLogMax();
                    ImGui::SliderFloat("Min Log Luminance", &logMin, -20.0f, logMax);
                    ImGui::SliderFloat("Max Log Luminance", &logMax, logMin, 20.0f);
                    Awesome.GetPostFX()->SetLogMin(logMin);
                    Awesome.GetPostFX()->SetLogMax(logMax);

                    float adaptRate = Awesome.GetPostFX()->GetAdaptRate();
                    ImGui::SliderFloat("Lum Adaption Rate", &adaptRate, 0.001f, 5.0f);
                    Awesome.GetPostFX()->SetAdaptRate(adaptRate);

                    ImGui::Columns(1);
                    ImGui::Separator();
                    ImGui::Text("GTAO/HBIL");

                    ImGui::SliderFloat("Intensity", &Awesome.GetGTAO()->m_intensity, 0.f, 5.f);
                    ImGui::SliderFloat("Radius", &Awesome.GetGTAO()->m_radius, 0.01f, 15.f, "%.6f");
                    ImGui::SliderFloat("Minimum Radius", &Awesome.GetGTAO()->m_minRadius, 0.f, 5.f, "%.6f");
                    ImGui::SliderFloat("Thickness", &Awesome.GetGTAO()->m_thickness, 0.00001f, 1.f, "%.6f");
                    SliderUInt("Slice Count", &Awesome.GetGTAO()->m_sliceCount, 1, 8);
                    SliderUInt("Step Count", &Awesome.GetGTAO()->m_stepCount, 1, 16);

                    ImGui::Separator();
                    ImGui::Text("Clouds");

                    ImGui::Checkbox("Clouds Enabled", &Awesome.GetClouds()->m_enabled);
                    ImGui::SliderFloat("Time of Day", &Awesome.GetClouds()->m_timeOfDay, 0.0f, 24.0f, "%.2f h");
                    ImGui::SliderFloat("Turbidity", &Awesome.GetClouds()->m_turbidity, 1.0f, 10.0f);
                    ImGui::SliderFloat("Sun Intensity", &Awesome.GetClouds()->m_sunIntensity, 0.0f, 80.0f);
                    {
                        const char* travItems[] = { "Tiled", "RQ Macro", "RQ Kernel", "Brute" };
                        int trav = (int)Awesome.GetClouds()->m_traversalMode;
                        if (ImGui::Combo("Traversal", &trav, travItems, IM_ARRAYSIZE(travItems)))
                            Awesome.GetClouds()->m_traversalMode = (uint32)trav;
                    }
                    {
                        // 0 = field cache, 2 = ray-traced reference (RQ traversal only),
                        // 3 = height/powder heuristic.
                        const char* litItems[] = { "Sun Cache", "Six-way", "Ray-traced Ref", "Heuristic" };
                        const uint32 litVals[] = { 0u, 1u, 2u, 3u };
                        uint32 lm = Awesome.GetClouds()->m_lightMode;
                        int lit = (lm == 0) ? 0 : (lm == 1 ? 1 : (lm == 2 ? 2 : 3));
                        if (ImGui::Combo("Lighting", &lit, litItems, IM_ARRAYSIZE(litItems)))
                            Awesome.GetClouds()->m_lightMode = litVals[lit];
                        if (lit == 2)
                            ImGui::TextDisabled("(needs Traversal = RQ Macro)");
                    }
                    {
                        const char* dbgItems[] = { "Off", "Heatmap", "Freq Bands", "Transmittance",
                            "Cloud Depth", "Analytic vs March", "Baked vs Ref", "Mask Rate",
                            "History Reject", "Min Tau", "Tile Count", "Cache Slice" };
                        int dbg = (int)Awesome.GetClouds()->m_debugView;
                        if (ImGui::Combo("Cloud Debug View", &dbg, dbgItems, IM_ARRAYSIZE(dbgItems)))
                            Awesome.GetClouds()->m_debugView = (uint32)dbg;
                    }
                    ImGui::SliderFloat("Wind Speed", &Awesome.GetClouds()->m_windSpeed, 0.0f, 40.0f, "%.1f m/s");
                    ImGui::SliderFloat("Wind Dir", &Awesome.GetClouds()->m_windDir, 0.0f, 360.0f, "%.0f deg");
                    ImGui::SliderFloat("Mask Aggressiveness", &Awesome.GetClouds()->m_maskAggressiveness, 1.0f, 8.0f);
                    ImGui::SliderFloat("Survival Floor", &Awesome.GetClouds()->m_survivalFloor, 0.01f, 0.5f);
                    ImGui::SliderFloat("Temporal Blend", &Awesome.GetClouds()->m_temporalAlpha, 0.02f, 1.0f);
                    ImGui::Checkbox("Accumulate (static cam)", &Awesome.GetClouds()->m_accumulate);
                    ImGui::Checkbox("In-register kernel synth", &Awesome.GetClouds()->m_inRegisterSynth);
                    ImGui::Checkbox("Baked scatter LUT", &Awesome.GetClouds()->m_bakedScatter);

                    {
                        Awesome::CloudGenerator* gen = Awesome.GetClouds()->GetGenerator();
                        bool regen = false;
                        regen |= ImGui::SliderFloat("Coverage", &gen->m_coverage, 0.0f, 1.0f);
                        regen |= ImGui::SliderFloat("Cloud Type", &gen->m_cloudType, 0.0f, 1.0f);
                        int oct = (int)gen->m_octaves;
                        if (ImGui::SliderInt("Octaves", &oct, 1, 4)) { gen->m_octaves = (uint32)oct; regen = true; }
                        int kpm = (int)gen->m_kernelsPerMacro;
                        if (ImGui::SliderInt("Kernels/Macro", &kpm, 4, 64)) { gen->m_kernelsPerMacro = (uint32)kpm; regen = true; }
                        int seed = (int)gen->m_seed;
                        if (ImGui::InputInt("Seed", &seed)) { gen->m_seed = (uint32)seed; regen = true; }
                        if (ImGui::Button("Regenerate")) regen = true;
                        ImGui::SameLine();
                        if (ImGui::Button("Load fit.cloud"))
                            gen->LoadFile("fit.cloud");   // from tools/vdb_fit/fit.py, placed in the working dir
                        if (regen) gen->RequestRegen();

                        float mb = gen->GetKernelCount() * 32.0f / (1024.0f * 1024.0f);
                        ImGui::Text("Macros: %u  Kernels: %u  (%.1f MB of 16 MB L2 budget)",
                            gen->GetMacroCount(), gen->GetKernelCount(), mb);
                    }

                    if (ImGui::Button("Reload Cloud Shaders"))
                        Awesome.GetClouds()->ReloadShaders();

                    ImGui::Separator();
                    ImGui::Text("Screen Space Shadows");

                    ImGui::SliderFloat("Surface Thickness", &Awesome.GetSSShadows()->m_surfaceThickness, 0.0001f, 0.01f);
                    ImGui::SliderFloat("Bilinear Threshhold", &Awesome.GetSSShadows()->m_bilinearThreshold, 0.001f, 0.1f);
                    ImGui::SliderFloat("Shadow Contrast", &Awesome.GetSSShadows()->m_shadowContrast, 1.0f, 6.0f);
                    
                    ImGui::Separator();
                    
                    ImGui::Text("Directional Shadows");
                    Awesome::DirectionalShadowSettings dsSettings = Awesome.GetDirectShadows()->GetSettings();
                    int32 cascades = (int32)dsSettings.cascades;

                    std::string sizeStr = std::to_string(dsSettings.size);

                    // Disable these controls for now
                    ImGui::BeginDisabled();
                    if (ImGui::BeginCombo("Shadow Size", sizeStr.c_str()))
                    {
                        for (uint32 i = 8; i < 14; i++)
                        {
                            uint32 s = 1 << i;
                            std::string sizeStrS = std::to_string(s);
                            if (ImGui::Selectable(sizeStrS.c_str(), s == dsSettings.size))
                            {
                                dsSettings.size = s;
                            }
                        }
                        ImGui::EndCombo();
                    }
                    
                    ImGui::SliderInt("Cascade count", &cascades, 1, c_maxCascadeCount);
                    ImGui::EndDisabled();

                    ImGui::SliderFloat("Shadow Bias", &dsSettings.bias, 0.0001f, 0.1f, "%.5f");
                    ImGui::SliderFloat("Shadow Soft Range", &dsSettings.softAmount, 0.0f, 5.0f);
                    ImGui::SliderFloat("Cascade Blend Range", &dsSettings.blendRange, 0.0f, 0.025f, "%.4f");

                    dsSettings.cascades = cascades;

                    Awesome.GetDirectShadows()->SetSettings(dsSettings);
                }

                if (ImGui::CollapsingHeader("Colors"))
                {
                    ImGui::BeginDisabled(!Awesome.GetHDRSupported());
                    bool hdrOn = Awesome.GetHDR();
                    ImGui::Checkbox("HDR Enabled", &hdrOn);
                    if (hdrOn != Awesome.GetHDR())
                    {
                        hdrReset = true;
                        Awesome.SetHDR(hdrOn);
                    }
                    
                    {
                        ImGui::BeginDisabled(!hdrOn);
                        ImGui::Text("Max Nits: %f", Awesome.GetMaxNits());
                        ImGui::Text("Max Fullscreen Nits: %f", Awesome.GetMaxFullNits());
                        float maxNits = Awesome.GetCurrentNits();
                        ImGui::SliderFloat("Current Brightness", &maxNits, 0.0f, 1000.0f);
                        Awesome.SetCurrentNits(maxNits);

                        float paperWhite = Awesome.GetUIPaperWhite();
                        ImGui::SliderFloat("UI Paperwhite", &paperWhite, 0.0f, 1000.0f);
                        Awesome.SetUIPaperWhite(paperWhite);

                        //ImGui_ImplDX12_SetHDR(false, g_Awesome->GetUIPaperWhite());
                        ImGui::EndDisabled();
                    }
                    ImGui::EndDisabled();

                    // Color Chart 
                    // Values from: https://xritephoto.com/documents/literature/en/ColorData-1p_EN.pdf

                    const ImU32 colors[24] = {
                        IM_COL32(115,  82,  68, 255), // dark skin
                        IM_COL32(194, 150, 130, 255), // light skin
                        IM_COL32( 98, 122, 157, 255), // blue sky
                        IM_COL32( 87, 108,  67, 255), // foliage
                        IM_COL32(133, 128, 177, 255), // blue flower
                        IM_COL32(103, 189, 170, 255), // blueish green
                        IM_COL32(214, 126,  44, 255), // orange
                        IM_COL32( 80,  91, 166, 255), // purplish blue
                        IM_COL32(193,  90,  99, 255), // moderate red
                        IM_COL32( 94,  60, 108, 255), // purple
                        IM_COL32(157, 188,  64, 255), // yellow green
                        IM_COL32(224, 163,  46, 255), // orange yellow
                        IM_COL32( 56,  61, 150, 255), // blue
                        IM_COL32( 70, 148,  73, 255), // green
                        IM_COL32(175,  54,  60, 255), // red
                        IM_COL32(231, 199,  31, 255), // yellow
                        IM_COL32(187,  86, 149, 255), // magenta
                        IM_COL32(  8, 133, 161, 255), // cyan
                        IM_COL32(243, 243, 242, 255), // white
                        IM_COL32(200, 200, 200, 255), // neutral 8
                        IM_COL32(160, 160, 160, 255), // neutral 6.5
                        IM_COL32(122, 122, 121, 255), // neutral 5
                        IM_COL32( 85,  85,  85, 255), // neutral 3.5
                        IM_COL32( 52,  52,  52, 255), // black
                    };

                    ImGui::Text("Color Chart Start");

                    ImDrawList* draw_list = ImGui::GetWindowDrawList();
                    const ImVec2 p = ImGui::GetCursorScreenPos();
                    const float spacing = 5.0f;
                    const float size = 48.0f;

                    for (int y = 0; y < 4; y++)
                    {
                        for (int x = 0; x < 6; x++)
                        {
                            int colIdx = x + y * 6;

                            ImVec2 start = ImVec2(p.x + x * (size+ spacing), p.y + y * (size + spacing));
                            ImVec2 end = ImVec2(start.x + size, start.y + size);

                            draw_list->AddRectFilled(start, end, colors[colIdx], 0.0f, ImDrawFlags_None);
                        }
                    }

                    ImGui::ItemSize(ImVec2(0, 4 * (size + spacing)), 0.0f);
                    ImGui::Text("Color Chart End");
                }
                
                ImGui::End();
            }

            ImGui::Render();

            auto t_end = std::chrono::high_resolution_clock::now();
            double elapsed_time_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();
            if (!Awesome.Render(float(elapsed_time_ms)))
                break;

        }
        t_start = std::chrono::high_resolution_clock::now();
    }
    DebugPrint("Ending Game Loop.\n");
}

void SetupImgui(HWND hwnd)
{
    DebugPrint("Starting Imgui.\n");
    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    
    // Setup Dear ImGui style
    ImGui::StyleColorsDark();

    // Setup Platform/Renderer backends
    ImGui_ImplWin32_Init(hwnd);

    Awesome::DescriptorHeap* heap = g_Awesome->GetMainDescHeap();

    Awesome::DescriptorHandle handle = heap->Allocate(Awesome::DescriptorSection::Assets);

    ImGui_ImplDX12_Init(g_Awesome->Device(),
        c_frameBufferCount,
        g_Awesome->GetUIFormat(),
        heap->GetHeap(),
        handle.cpuHandle,
        handle.gpuHandle);


    //ImGui_ImplDX12_SetHDR(false, g_Awesome->GetUIPaperWhite());

}



int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nCmdShow)
{

    uint16 width = 1920;
    uint16 height = 1080;

    // Create Window
    HWND hwnd = SetupWindow(width, height);
    if (!hwnd)
        return -1;

    // Finalize Window
    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);
   
    entt::registry registry;

    g_Awesome = new Awesome::AwesomeGraphics(hwnd, width, height); 

    // Init Awesome
    if (g_Awesome->StartUp())
    {
        g_Awesome->StartStats();
        SetupImgui(hwnd);

        {
            // Main Loop
            ClientLoop(*g_Awesome);
        }

        // Cleanup
        ImGui_ImplDX12_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
    }
    DebugPrint("Shutting Down...\n");

    // Cleanup
    registry.clear();

    g_Awesome->TearDown();
    delete g_Awesome;

    return 0;
}
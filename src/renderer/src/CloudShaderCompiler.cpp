#include "CloudShaderCompiler.h"
#include "Awesome.h"
#include "Util.h"

#ifdef DEBUG
#include <dxcapi.h>
#include <fstream>
#endif

using namespace Awesome;

CloudShaderCompiler::CloudShaderCompiler(AwesomeGraphics* Awesome)
    : m_Awesome(Awesome)
    , m_dxcModule(nullptr)
    , m_compiler(nullptr)
    , m_utils(nullptr)
    , m_includeHandler(nullptr)
{
}

CloudShaderCompiler::~CloudShaderCompiler()
{
}

#ifdef DEBUG

bool CloudShaderCompiler::StartUp()
{
    // Absence of the DLL is not a failure: hot reload is a convenience, .cso is the truth.
    m_dxcModule = LoadLibraryW(L"dxcompiler.dll");
    if (!m_dxcModule)
    {
        DebugPrint("CloudShaderCompiler: dxcompiler.dll not found, hot reload disabled\n");
        return true;
    }

    typedef HRESULT(__stdcall* DxcCreateInstanceProc)(REFCLSID, REFIID, LPVOID*);
    DxcCreateInstanceProc createInstance = (DxcCreateInstanceProc)GetProcAddress(m_dxcModule, "DxcCreateInstance");
    if (!createInstance)
        return true;

    if (FAILED(createInstance(CLSID_DxcUtils, IID_PPV_ARGS(&m_utils))))
        return true;
    if (FAILED(createInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&m_compiler))))
    {
        SafeRelease(m_utils);
        return true;
    }
    m_utils->CreateDefaultIncludeHandler(&m_includeHandler);

    // Runtime cwd is bin/<cfg> (premake debugdir); source lives in the repo tree.
    const wchar_t* candidates[] = { L"../../src/renderer/shaders/", L"shaders/" };
    for (const wchar_t* dir : candidates)
    {
        std::wstring probe = std::wstring(dir) + L"CloudCommon.hlsli";
        std::ifstream f(std::string(probe.begin(), probe.end()));
        if (f.good() || m_sourceDir.empty())
        {
            m_sourceDir = dir;
            if (f.good())
                break;
        }
    }

    DebugPrint("CloudShaderCompiler: hot reload available (source dir ok)\n");
    return true;
}

bool CloudShaderCompiler::TearDown()
{
    SafeRelease(m_includeHandler);
    SafeRelease(m_compiler);
    SafeRelease(m_utils);
    if (m_dxcModule)
    {
        FreeLibrary(m_dxcModule);
        m_dxcModule = nullptr;
    }
    return true;
}

bool CloudShaderCompiler::Compile(const wchar_t* name, const std::vector<std::wstring>& defines, std::vector<uint8>& outBytecode)
{
    if (!Available())
        return false;

    std::wstring path = m_sourceDir + name + L".hlsl";

    IDxcBlobEncoding* source = nullptr;
    if (FAILED(m_utils->LoadFile(path.c_str(), nullptr, &source)))
    {
        DebugPrint("CloudShaderCompiler: failed to load source for reload\n");
        return false;
    }

    std::vector<std::wstring> argStorage;
    argStorage.push_back(L"-T"); argStorage.push_back(L"cs_6_8");
    argStorage.push_back(L"-E"); argStorage.push_back(L"main");
    argStorage.push_back(L"-I"); argStorage.push_back(m_sourceDir);
    for (const std::wstring& d : defines)
    {
        argStorage.push_back(L"-D");
        argStorage.push_back(d);
    }
    std::vector<LPCWSTR> args;
    for (const std::wstring& a : argStorage)
        args.push_back(a.c_str());

    DxcBuffer buffer = {};
    buffer.Ptr = source->GetBufferPointer();
    buffer.Size = source->GetBufferSize();
    buffer.Encoding = DXC_CP_ACP;

    IDxcResult* result = nullptr;
    HRESULT hr = m_compiler->Compile(&buffer, args.data(), (uint32)args.size(), m_includeHandler, IID_PPV_ARGS(&result));
    source->Release();
    if (FAILED(hr) || !result)
        return false;

    IDxcBlobUtf8* errors = nullptr;
    result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr);
    if (errors && errors->GetStringLength() > 0)
        DebugPrint("CloudShaderCompiler: %s\n", errors->GetStringPointer());
    if (errors)
        errors->Release();

    HRESULT status = E_FAIL;
    result->GetStatus(&status);
    if (FAILED(status))
    {
        result->Release();
        return false;
    }

    IDxcBlob* object = nullptr;
    result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&object), nullptr);
    result->Release();
    if (!object)
        return false;

    const uint8* bytes = (const uint8*)object->GetBufferPointer();
    outBytecode.assign(bytes, bytes + object->GetBufferSize());
    object->Release();
    return !outBytecode.empty();
}

#else // !DEBUG — hot reload compiled out; .cso is the only path

bool CloudShaderCompiler::StartUp() { return true; }
bool CloudShaderCompiler::TearDown() { return true; }
bool CloudShaderCompiler::Compile(const wchar_t*, const std::vector<std::wstring>&, std::vector<uint8>&) { return false; }

#endif

#pragma once
#include <d3d12.h>
#include <string>
#include <vector>
#include "types.h"

struct IDxcCompiler3;
struct IDxcUtils;
struct IDxcIncludeHandler;

namespace Awesome
{
    class AwesomeGraphics;

    // Debug-only runtime compiler for cloud shaders (docs/PLAN.md 6.8 / step P0.4).
    // Loads dxcompiler.dll (copied post-build from the DXC NuGet package) and compiles
    // cloud *.hlsl source at cs_6_8 for hot reload. When the DLL or the source tree is
    // unavailable (Release builds, shipping layouts), Available() is false and callers
    // keep using the offline-built .cso — this class must never be load-bearing.
    class CloudShaderCompiler
    {
    public:
        CloudShaderCompiler(AwesomeGraphics* Awesome);
        ~CloudShaderCompiler();

        bool StartUp();
        bool TearDown();

        bool Available() const { return m_compiler != nullptr; }

        // Compiles shaders/<name>.hlsl (entry main, cs_6_8) with optional -D defines
        // ("NAME=VALUE" or "NAME"). Returns true and fills outBytecode on success;
        // false on any failure (caller falls back to the .cso).
        bool Compile(const wchar_t* name, const std::vector<std::wstring>& defines, std::vector<uint8>& outBytecode);

    private:
        AwesomeGraphics* m_Awesome;
        HMODULE m_dxcModule;
        IDxcCompiler3* m_compiler;
        IDxcUtils* m_utils;
        IDxcIncludeHandler* m_includeHandler;
        std::wstring m_sourceDir;
    };
}

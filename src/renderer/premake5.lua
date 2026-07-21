project "Awesome"
    kind "StaticLib"
    language "C++"
    cppdialect "C++17"
    targetdir "../../bin/%{cfg.buildcfg}"
    location "../../build/projects/renderer"
    files { "**.h", "**.cpp" , "**.hlsl", "**.hlsli", "../../thirdparty/MikkTSpace/**.c"}

    nuget {
        "WinPixEventRuntime:1.0.240308001",
        AgilityNuget(),
        "Microsoft.Direct3D.DXC:1.8.2502.8"
     }

    filter { "files:**.hlsl" }
        flags "ExcludeFromBuild"
        shadermodel "6.8"
        shaderobjectfileoutput "../../../bin/%{cfg.buildcfg}/shaders/%%(Filename).cso"
    filter "configurations:Debug"
        shaderoptions "/Fd\"../../../bin/%{cfg.buildcfg}/shaders/%(Filename).pdb\""
    
    filter { "files:**-p.hlsl" }
        removeflags "ExcludeFromBuild"
        shadertype "Pixel"
    filter { "files:**-v.hlsl" }
        removeflags "ExcludeFromBuild"
        shadertype "Vertex"
    filter { "files:**-c.hlsl" }
        removeflags "ExcludeFromBuild"
        shadertype "Compute"
    filter {}

    includedirs { 
        "includes"
    }

    externalincludedirs {
        "../../thirdparty/tomlplusplus/include",
        "../../thirdparty/imgui",
        "../../thirdparty/stb",
        "../../thirdparty/assimp/include",
        "../../thirdparty/MikkTSpace"
    }
    if _OPTIONS["aftermath"] then
        externalincludedirs { "../../thirdparty/aftermath/include" }
        defines { "USE_NSIGHT_AFTERMATH" }
    end
    
    --libdirs { "../../thirdparty/aftermath/bin/x64"} 

    --links {"assimp", "d3d12", "imgui"}

    filter "configurations:Debug"
        defines { "DEBUG", "NOMINMAX" }
        symbols "On"

    filter "configurations:Release"
        defines { "NDEBUG", "NOMINMAX" }
        optimize "On"

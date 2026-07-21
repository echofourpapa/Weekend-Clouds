newoption {
   trigger = "aftermath",
   description = "Enable NVIDIA Nsight Aftermath (requires the SDK in thirdparty/aftermath/)"
}

-- Agility SDK selection (docs/PLAN.md P6.4). Lets the runtime be bumped to a
-- newer redistributable -- e.g. an SM 6.9 / SER-capable D3D12 -- without editing
-- source. Defaults track the pinned NuGet payload (version 615).
newoption {
   trigger = "agility-nuget",
   value = "VER",
   description = "Agility SDK NuGet package version (default 1.615.1)"
}
newoption {
   trigger = "agility-sdk",
   value = "NUM",
   description = "D3D12SDKVersion export number matching the NuGet payload (default 615)"
}

-- Shared accessors used by both the app and renderer projects.
function AgilityNuget()
   return "Microsoft.Direct3D.D3D12:" .. (_OPTIONS["agility-nuget"] or "1.615.1")
end
function AgilitySdkVersion()
   return _OPTIONS["agility-sdk"] or "615"
end

workspace "Awesome-Thing"
   configurations { "Debug", "Release" }
   platforms { "x64" }
   location "build"
   startproject "Awesome-Client"
   filter { "platforms:Win64" }
      system "Windows"
      architecture "x86_64"

   group "thirdparty"
      -- TODO: Move all 3rd party libs to their own files
      externalproject "assimp"
         cwd = os.getcwd()
         os.chdir("thirdparty/assimp/")
         os.execute("cmake CMakeLists.txt")
         os.chdir(cwd)
         location "thirdparty/assimp/code"
         uuid(os.uuid("thirdparty/assimp"))
         kind "SharedLib"
         language "C++"

      project "entt"
         kind "StaticLib"
         language "C++"
         cppdialect "C++17"
         targetdir "bin/%{cfg.buildcfg}"   
         location "build/thirdparty/entt"
         files { 
            "thirdparty/entt/src/**.hpp" 
         }
      
      externalproject "zlibstatic"
         location "thirdparty/assimp/contrib/zlib"
         uuid(os.uuid("thirdparty/assimp/contrib/zlib"))
         kind "StaticLib"
         language "C++"
      
      project "imgui"
         kind "StaticLib"
         language "C++"
         cppdialect "C++17"
         targetdir "bin/%{cfg.buildcfg}"   
         location "build/thirdparty/imgui"
         files { 
            "thirdparty/imgui/**.h", 
            "thirdparty/imgui/**.cpp" 
         }
         removefiles { 
            "thirdparty/imgui/examples/**.h", 
            "thirdparty/imgui/examples/**.cpp", 
            "thirdparty/imgui/misc/**.h",         
            "thirdparty/imgui/misc/**.cpp",
            "thirdparty/imgui/backends/**.h",
            "thirdparty/imgui/backends/**.cpp", 
         }
         -- Add the DX12 backend
         files {
            "thirdparty/imgui/backends/imgui_impl_dx12.h",
            "thirdparty/imgui/backends/imgui_impl_dx12.cpp", 
            "thirdparty/imgui/backends/imgui_impl_win32.h",
            "thirdparty/imgui/backends/imgui_impl_win32.cpp", 
         }
         includedirs { 
            "thirdparty/imgui"
         }
         filter "configurations:Debug"
            defines { "DEBUG", "NOMINMAX" }
            symbols "On"

         filter "configurations:Release"
            defines { "NDEBUG", "NOMINMAX" }
            optimize "On"

   group "Awesome Things!"
      project "Awesome-Data"
         kind "None"
         location "build/projects/data"
         files {"data/**.toml"}

      include "src/renderer"
      include "src/app"
   group ""

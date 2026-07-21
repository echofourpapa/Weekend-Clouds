project "Awesome-Client"
   kind "WindowedApp"
   language "C++"
   cppdialect "C++17"
   targetdir "../../bin/%{cfg.buildcfg}"
   debugdir "../../bin/%{cfg.buildcfg}"

   location "../../build/projects/app"
   targetname "Awesome-Thing"
   nuget {
      "WinPixEventRuntime:1.0.240308001",
      AgilityNuget(),
      "Microsoft.Direct3D.DXC:1.8.2502.8"
   }

   -- Match the D3D12SDKVersion export in Client.cpp to the restored NuGet payload.
   defines { "D3D12SDK_VERSION_OVERRIDE=" .. AgilitySdkVersion() }

   files { "**.h", "**.cpp", "**.rc", "**.png"}

   includedirs { "includes" , "../renderer/includes"}

   externalincludedirs {
      "../../thirdparty/tomlplusplus/include",
      "../../thirdparty/imgui",
      "../../thirdparty/stb",
      "../../thirdparty/assimp/include",
      "../../thirdparty/entt/src"
   }
   links { "Awesome", "assimp","d3d12", "dxgi", "dxguid", "d3dcompiler", "imgui"}
   if _OPTIONS["aftermath"] then
      libdirs { "../../thirdparty/aftermath/lib/x64" }
      links { "GFSDK_Aftermath_Lib.x64" }
      defines { "USE_NSIGHT_AFTERMATH" }
   end
   td = path.getabsolute("../../bin/%{cfg.buildcfg}")
   ap = path.getabsolute("..\\..\\thirdparty\\assimp\\bin\\%{cfg.buildcfg}")
   -- DXC runtime DLLs for the Debug-only cloud shader hot-reload (see docs/PLAN.md 6.8);
   -- restored by NuGet next to the generated solution. 'if exist' keeps the build green
   -- before the first package restore.
   dxcbin = path.getabsolute("../../build/packages/Microsoft.Direct3D.DXC.1.8.2502.8/build/native/bin/x64")
   dxccopy = {
      "if exist \"" .. dxcbin .. "\\dxcompiler.dll\" copy /Y \"" .. dxcbin .. "\\dxcompiler.dll\" \"" .. td .. "\\dxcompiler.dll\"",
      "if exist \"" .. dxcbin .. "\\dxil.dll\" copy /Y \"" .. dxcbin .. "\\dxil.dll\" \"" .. td .. "\\dxil.dll\"",
   }
   
   filter "configurations:Debug"
      defines { "DEBUG", "NOMINMAX" }
      symbols "On"
      postbuildcommands {
         "copy /Y \"" .. ap .. "\\assimp-vc143-mtd.dll\" \"" .. td .. "\\assimp-vc143-mtd.dll\"",
         "copy /Y \"" .. ap .. "\\assimp-vc143-mtd.pdb\" \"" .. td .. "\\assimp-vc143-mtd.pdb\"",
         dxccopy[1],
         dxccopy[2],
      }

   filter "configurations:Release"
      defines { "NDEBUG", "NOMINMAX" }
      optimize "On"
      postbuildcommands {
         "copy /Y \"" .. ap .. "\\assimp-vc143-mt.dll\" \"" .. td .. "\\assimp-vc143-mt.dll\"",
         "copy /Y \"" .. ap .. "\\assimp-vc143-mt.pdb\" \"" .. td .. "\\assimp-vc143-mt.pdb\"",
         dxccopy[1],
         dxccopy[2],
      }
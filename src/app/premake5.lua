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
      "Microsoft.Direct3D.D3D12:1.615.1",
      "Microsoft.Direct3D.DXC:1.8.2502.8"
   }

   files { "**.h", "**.cpp", "**.rc", "**.png"}

   includedirs { "includes" , "../renderer/includes"}

   externalincludedirs {
      "../../thirdparty/tomlplusplus/include",
      "../../thirdparty/imgui",
      "../../thirdparty/stb",
      "../../thirdparty/assimp/include",
      "../../thirdparty/entt/src"
   }
   libdirs { "../../thirdparty/aftermath/lib/x64"} 
   links { "Awesome", "assimp","d3d12", "dxgi", "dxguid", "d3dcompiler", "imgui", "GFSDK_Aftermath_Lib.x64"}
   td = path.getabsolute("../../bin/%{cfg.buildcfg}")
   ap = path.getabsolute("..\\..\\thirdparty\\assimp\\bin\\%{cfg.buildcfg}")
   
   filter "configurations:Debug"
      defines { "DEBUG", "NOMINMAX" }
      symbols "On"
      postbuildcommands {          
         "copy /Y \"" .. ap .. "\\assimp-vc143-mtd.dll\" \"" .. td .. "\\assimp-vc143-mtd.dll\"",
         "copy /Y \"" .. ap .. "\\assimp-vc143-mtd.pdb\" \"" .. td .. "\\assimp-vc143-mtd.pdb\"",
      }

   filter "configurations:Release"
      defines { "NDEBUG", "NOMINMAX" }
      optimize "On"
      postbuildcommands {          
         "copy /Y \"" .. ap .. "\\assimp-vc143-mt.dll\" \"" .. td .. "\\assimp-vc143-mt.dll\"",
         "copy /Y \"" .. ap .. "\\assimp-vc143-mt.pdb\" \"" .. td .. "\\assimp-vc143-mt.pdb\"",
      }
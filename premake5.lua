newoption {
   trigger = "aftermath",
   description = "Enable NVIDIA Nsight Aftermath (requires the SDK in thirdparty/aftermath/)"
}

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

#pragma once
#include "types.h"
#include "toml++/toml.h"

namespace Awesome {

    class AwesomeGraphics;

    class ResourceLoader {
    public:
        static bool LoadResources(AwesomeGraphics* Awesome, const char* path);
        static void LoadMaterials(toml::v3::table& project, std::map<std::string, std::vector<uint32>>& shaderMap, AwesomeGraphics* Awesome, std::map<std::string, uint32>& materialMap);
        static void LoadIBLs(toml::v3::table& project, AwesomeGraphics* Awesome);
        static void LoadShaders(toml::v3::table& project, AwesomeGraphics* Awesome, std::map<std::string, std::vector<uint32>>& shaderMap);
        static bool CompileResources(AwesomeGraphics* Awesome, const char* path);
        static bool LoadCompiledResources(AwesomeGraphics* Awesome, const char* path);
    };

};
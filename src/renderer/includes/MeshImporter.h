#pragma once
#include "MeshData.h"
#include <string>

namespace Awesome
{
    struct ImportMatInfo
    {
        std::string name;
        std::vector<std::string> texturePaths;
        std::vector<uint32> textureTypes;
    };

    class Importer {
    public:
        static bool LoadMesh(const char* path, std::vector<MeshData>& outMesh, std::vector<ImportMatInfo>& outMaterials, bool bake=false);
        static bool ImportMesh(const char* path, std::vector<MeshData>& outMesh, std::vector<ImportMatInfo>& outMaterials, bool bake);
        static bool CompileMesh(const char* path, std::vector<MeshData>& outMesh, std::vector<ImportMatInfo>& outMaterials);
        static bool LoadCompiledMesh(const char* path, std::vector<MeshData>& outMesh, std::vector<ImportMatInfo>& outMaterials);

    };
};
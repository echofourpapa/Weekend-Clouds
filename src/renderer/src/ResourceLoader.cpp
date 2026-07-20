#include "ResourceLoader.h"
#include "SceneLoader.h"
#include "Awesome.h"
#include "MeshImporter.h"
#include "MeshData.h"
#include "Material.h"
#include "Texture.h"
#include "IBL.h"
#include "Hair.h"
#include "Util.h"
#include <filesystem>
#include <stdlib.h> 

using namespace Awesome;

bool ResourceLoader::LoadResources(AwesomeGraphics* Awesome, const char* path)
{
    std::map<std::string, std::vector<uint32>> shaderMap;
    std::map<std::string, uint32> materialMap;
    std::map<std::string, std::tuple<uint32, uint32>> meshMap;

    toml::table project = toml::parse_file(path);

    LoadShaders(project, Awesome, shaderMap);

    LoadIBLs(project, Awesome);

    LoadMaterials(project, shaderMap, Awesome, materialMap);
    
    // Meshes & Textures
    if (toml::array* Meshes = project["Mesh"].as_array())
    {
        uint32 parentIndex = 0;
        for (toml::node& mesh : *Meshes)
        {
            toml::table& mTable = *mesh.as_table();
            auto name = mTable["name"].value<std::string>().value();
            auto material = mTable["material"].value<std::string>().value();
            auto meshPath = mTable["path"].value<std::string>().value();
            auto bake = mTable["bake"].value_or<bool>(false);
            auto prim = mTable["primitive"].value_or<bool>(false);

            uint32 meshIndex = parentIndex;

            std::vector<MeshData> meshData;
            std::vector<ImportMatInfo> materialNames;
            if (Importer::LoadMesh(meshPath.c_str(), meshData, materialNames, bake))
            {
                for (int i = 0; i < meshData.size(); i++)
                {
                    // offset to the last mat's index
                    meshData[i].materialIndex += Awesome->GetMaterialSystem()->GetMaterialCount();
                    meshIndex = Awesome->GetMeshSystem()->CreateMesh(meshData[i], prim);
                }

                for (ImportMatInfo& matInfo : materialNames)
                {
                    std::string mapKey = material;

                    if (matInfo.name.find("Foliage") != std::string::npos)
                        mapKey = std::string("Foliage");

                    if (materialMap.find(matInfo.name) != materialMap.end())
                        mapKey = matInfo.name;

                    DebugPrint("Mapping material %s to %s\n", matInfo.name.c_str(), mapKey.c_str());

                    uint32 pso = materialMap[mapKey];
                    std::vector<uint32> textures;
                    assert(matInfo.texturePaths.size() <= 4);
                    for (std::string& texture : matInfo.texturePaths)
                    {
                        std::filesystem::path texPath(meshPath);
                        texPath.replace_filename(texture);

                        std::filesystem::path compTex(texPath);
                        auto f_name = texPath.filename();
                        compTex.remove_filename();
                        compTex /= "compressed" / f_name;
                        compTex.replace_extension("dds");

                        if (std::filesystem::exists(compTex))
                            texPath = compTex;

                        bool genMips = texPath.extension() != ".dds";
                        uint32 tex = Awesome->GetTextureSystem()->AddTexture(texPath.string().c_str(), genMips);
                        if(tex != invalidIndex32)
                            textures.push_back(tex);
                    }
                    Awesome->GetMaterialSystem()->CreateMaterial(pso, textures);
                }
            }

            if (meshIndex != invalidIndex32)
            {
                meshMap.insert_or_assign(name, std::tuple<uint32, uint32>(parentIndex, meshIndex));
                parentIndex = meshIndex+1;
            }
        }
    }

#if 0
    // Hair
    // TODO: Make this more data driven.  For now, just jam in some data.
    {
        const float um_to_m = ((1.0f / 1000.0f) / 1000.0f);

        // For the N number of points in a strand, there are always N-1 segments

        const uint32 hairStrandCount = 1000;
        const uint32 hairPointsCount = 10;
        Awesome::HairData hairData;
        hairData.points.reserve(hairStrandCount * hairPointsCount * 2);
        hairData.strands.resize(hairStrandCount);

        for (uint32 strand = 0; strand < hairStrandCount; ++strand)
        {
            uint32 pointTotal = hairPointsCount + ( (rand() % 10) - 5); // add a random length to the strand
            float x = float((rand() % 2000) - 1000) / 1000.0f; // random starting point
            float z = float((rand() % 2000) - 1000) / 1000.0f;
            uint32 strandStart = uint32(hairData.points.size());
            for (uint32 point = 0; point < pointTotal; ++point)
            {
                float y = float(point) + float((rand() % 20) - 10) / 100.0f; // random length offset
                hairData.points.push_back(XMFLOAT4(x, 10.f-y, z, 72.f * 0.5f * um_to_m));  // let's make the hair point down
                x += float((rand() % 20) - 10) / 50.0f; // random offset
                z += float((rand() % 20) - 10) / 50.0f;
            }
            hairData.strands[strand].x = strandStart;
            hairData.strands[strand].y = pointTotal;
        }

        Awesome->GetHairSystem()->CreateHair(hairData);

    }
#endif

    // Scenes
    if (toml::array* Scenes = project["Scene"].as_array())
    {
        for (toml::node& scene : *Scenes)
        {
            toml::table& sTable = *scene.as_table();
            auto name = sTable["name"].value<std::string>().value();
            auto scenePath = sTable["path"].value<std::string>().value();
                
            SceneLoader::LoadScene(Awesome, name.c_str(), scenePath.c_str(), meshMap);
        }
    }

    return true;
}

void ResourceLoader::LoadMaterials(toml::v3::table& project, std::map<std::string, std::vector<uint32>>& shaderMap, Awesome::AwesomeGraphics* Awesome, std::map<std::string, uint32>& materialMap)
{
    // Materials
    if (toml::array* Materials = project["Material"].as_array())
    {
        for (toml::node& material : *Materials)
        {
            toml::table& mTable = *material.as_table();
            auto name = mTable["name"].value<std::string>().value();
            auto vertex = mTable["vertex"].value<std::string>().value();
            auto pixel = mTable["pixel"].value<std::string>().value();
            auto twosided = mTable["twosided"].value<bool>().value();

            DebugPrint("Setting Material %s, with %s, %s\n", name.c_str(), vertex.c_str(), pixel.c_str());

            uint32 vIndex = invalidIndex32;
            if (shaderMap.find(vertex) != shaderMap.end())
                vIndex = shaderMap[vertex][ShaderPermute::Base];
            uint32 pIndex = invalidIndex32;
            if (shaderMap.find(pixel) != shaderMap.end())
                pIndex = shaderMap[pixel][ShaderPermute::Base];

            if (vIndex != invalidIndex32 && pIndex != invalidIndex32)
            {
                uint32 pso = Awesome->GetMaterialSystem()->CreatePipeline(vIndex, pIndex, Awesome->GetMeshSystem()->GetMeshLayout(), twosided);
                materialMap.insert_or_assign(name, pso);
            }

        }
    }
}

void ResourceLoader::LoadIBLs(toml::v3::table& project, Awesome::AwesomeGraphics* Awesome)
{
    // IBLs
    if (toml::array* Materials = project["IBL"].as_array())
    {
        for (toml::node& material : *Materials)
        {
            toml::table& mTable = *material.as_table();
            auto name = mTable["name"].value<std::string>().value();
            auto iblPath = mTable["path"].value<std::string>().value();
            uint32 ibl = Awesome->GetIBLSystem()->AddIBL(iblPath.c_str(), name.c_str());
        }
    }
}

void ResourceLoader::LoadShaders(toml::v3::table& project, Awesome::AwesomeGraphics* Awesome, std::map<std::string, std::vector<uint32>>& shaderMap)
{
    // Shaders
    if (toml::array* shaders = project["Shader"].as_array())
    {
        for (toml::node& shader : *shaders)
        {
            toml::table& sTable = *shader.as_table();
            auto name = sTable["name"].value<std::string>().value();
            auto type = sTable["type"].value<std::string>().value();
            auto shaderPath = sTable["path"].value<std::wstring>().value();
            toml::array* perms = sTable["permuations"].as_array();
            ShaderType sType;
            if (type == "vertex")
                sType = ShaderType::Vertex;
            else if (type == "pixel")
                sType = ShaderType::Pixel;
            else if (type == "compute")
                sType = ShaderType::Compute;

            std::vector<D3D_SHADER_MACRO*> permuations;
            uint32 permCount = uint32(perms->size());
            std::vector<std::string> pnames;

            for (uint32 i = 0; i < permCount; ++i)
            {
                std::string name = (*perms)[i].value<std::string>().value();
                pnames.push_back(std::string(name));
            }

            // Always one 1 Base permuation
            D3D_SHADER_MACRO* defines = new D3D_SHADER_MACRO[1];
            defines[0] = { NULL, NULL };
            permuations.push_back(defines);

            // Now add system perumtes, like Shadows
            if (sType == ShaderType::Vertex || sType == ShaderType::Pixel)
            {
                D3D_SHADER_MACRO* defines = new D3D_SHADER_MACRO[2];
                defines[0] = { "Shadow", "" };
                defines[1] = { NULL, NULL };
                permuations.push_back(defines);
            }

            // Not true permutations yet, but this should be fine for now
            // Treat them all as unique, not combinations
            for (uint32 i = 0; i < permCount; ++i)
            {
                std::string& cname = pnames[i];
                D3D_SHADER_MACRO* defines = new D3D_SHADER_MACRO[2];
                std::string& pname = pnames[i];
                defines[0] = { pname.c_str(), "" };
                defines[1] = { NULL, NULL };
                permuations.push_back(defines);
            }

            uint32 index = Awesome->GetMaterialSystem()->LoadShader(shaderPath.c_str(), sType, permuations);
            if (index != invalidIndex32)
            {
                std::vector<uint32> perms;
                for (uint32 i = 0; i < permCount + 2; ++i)
                {
                    perms.push_back(index + i);
                }
                shaderMap.insert_or_assign(name, perms);
            }
        }
    }
}

bool ResourceLoader::CompileResources(AwesomeGraphics* Awesome, const char* path)
{
    return false;
}

bool ResourceLoader::LoadCompiledResources(AwesomeGraphics* Awesome, const char* path)
{
    return false;
}

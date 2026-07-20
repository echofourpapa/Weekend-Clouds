#include "MeshImporter.h"
#include "types.h"
#include "Util.h"
#include <assimp/Importer.hpp>
#include <assimp/Logger.hpp>
#include <assimp/LogStream.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/DefaultLogger.hpp>
#include "mikktspace.h"
#include <fstream>
#include <filesystem>

using namespace Awesome;

// 3 verts per triangles
const uint32 c_vertsPerTriangle = 3;

static int GetNumFaces(const SMikkTSpaceContext* pContext) 
{
    MeshData* meshdata = reinterpret_cast<MeshData*>(pContext->m_pUserData);
    return (uint32)meshdata->indices.size() / c_vertsPerTriangle;
}

static int GetVertexPerFace(const SMikkTSpaceContext* pContext, const int iFace)
{
    
    return c_vertsPerTriangle;
}

static void GetPosition(const SMikkTSpaceContext* pContext, float fvPosOut[], const int iFace, const int iVert)
{
    MeshData* meshdata = reinterpret_cast<MeshData*>(pContext->m_pUserData);
    uint32 idx = iFace * c_vertsPerTriangle + iVert;
    uint32 vertIdx = meshdata->indices[idx];

    fvPosOut[0] = meshdata->vertices[vertIdx].position.x;
    fvPosOut[1] = meshdata->vertices[vertIdx].position.y;
    fvPosOut[2] = meshdata->vertices[vertIdx].position.z;
}

static void GetNormal(const SMikkTSpaceContext* pContext, float fvNormOut[], const int iFace, const int iVert)
{
    MeshData* meshdata = reinterpret_cast<MeshData*>(pContext->m_pUserData);
    uint32 idx = iFace * c_vertsPerTriangle + iVert;
    uint32 vertIdx = meshdata->indices[idx];

    fvNormOut[0] = meshdata->vertices[vertIdx].normal.x;
    fvNormOut[1] = meshdata->vertices[vertIdx].normal.y;
    fvNormOut[2] = meshdata->vertices[vertIdx].normal.z;
}

static void GetTexcoord(const SMikkTSpaceContext* pContext, float fvTexcOut[], const int iFace, const int iVert)
{
    MeshData* meshdata = reinterpret_cast<MeshData*>(pContext->m_pUserData);
    uint32 idx = iFace * c_vertsPerTriangle + iVert;
    uint32 vertIdx = meshdata->indices[idx];

    fvTexcOut[0] = meshdata->vertices[vertIdx].uv[0].x;
    fvTexcOut[1] = meshdata->vertices[vertIdx].uv[0].y;
}

static void SetTangentsBasic(const SMikkTSpaceContext* pContext, const float fvTangent[], const float fSign, const int iFace, const int iVert)
{
    MeshData* meshdata = reinterpret_cast<MeshData*>(pContext->m_pUserData);
    uint32 idx = iFace * c_vertsPerTriangle + iVert;
    uint32 vertIdx = meshdata->indices[idx];

    meshdata->vertices[vertIdx].tangent.x = fvTangent[0];
    meshdata->vertices[vertIdx].tangent.y = fvTangent[1];
    meshdata->vertices[vertIdx].tangent.z = fvTangent[2];
    meshdata->vertices[vertIdx].tangent.w = fSign;
}

uint32 ProcessMesh(uint32 offset, MeshData& outMesh, const aiMesh* mesh, const aiScene* scene)
{
    DebugPrint("\tProcessing Mesh: %s\n", mesh->mName.C_Str());
    for (uint32 v = 0; v < mesh->mNumVertices; ++v)
    {
        VertexData vertex = {};
        vertex.position.x = mesh->mVertices[v].x;
        vertex.position.y = mesh->mVertices[v].y;
        vertex.position.z = mesh->mVertices[v].z;

        vertex.normal.x = mesh->mNormals[v].x;
        vertex.normal.y = mesh->mNormals[v].y;
        vertex.normal.z = mesh->mNormals[v].z;

        for (uint32 uv = 0; uv < 2; uv++)
        {
            if (mesh->mTextureCoords[uv]) {
                vertex.uv[uv].x = (float)mesh->mTextureCoords[uv][v].x;
                vertex.uv[uv].y = (float)mesh->mTextureCoords[uv][v].y;
            }
        }

        outMesh.vertices.push_back(vertex);
    }

    for (uint32 f = 0; f < mesh->mNumFaces; f++) {
        aiFace face = mesh->mFaces[f];

        for (uint32 i = 0; i < face.mNumIndices; i++)
            outMesh.indices.push_back(face.mIndices[i] + offset);
    }
    outMesh.materialIndex = mesh->mMaterialIndex;
    outMesh.name = std::string(mesh->mName.C_Str());
    outMesh.visible = true;

    return uint32(outMesh.vertices.size());
}

void ProcessNode(uint32 offset, std::vector<Awesome::MeshData>& outMesh, const aiNode* node, const aiScene* scene, const aiMatrix4x4 parentXForm)
{
    DebugPrint("Processing Node: %s\n", node->mName.C_Str());
    uint32 lOffset = offset;
    aiMatrix4x4 localXForm = parentXForm * node->mTransformation;

    for (uint32 i = 0; i < node->mNumMeshes; ++i)
    {
        aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
        if(mesh->HasBones())
            DebugPrint("Bones: %d\n", mesh->mNumBones);

        if ((mesh->mPrimitiveTypes & aiPrimitiveType_TRIANGLE) != 0)
        {
            Awesome::MeshData& vMesh = outMesh.emplace_back();

            aiVector3t<float> scale;
            aiVector3t<float> rotation;
            aiVector3t<float> position;

            localXForm.Decompose(scale, rotation, position);

            vMesh.transform.position = { position.x, position.y, position.z };
            vMesh.transform.rotation = { XMConvertToDegrees(rotation.x), XMConvertToDegrees(rotation.y), XMConvertToDegrees(rotation.z) };
            vMesh.transform.scale = { scale.x, scale.y, scale.z };
            
            vMesh.transform.CalcMatrix();

            lOffset += ProcessMesh(0, vMesh, mesh, scene);
        }
    }

    for (uint32 n = 0; n < node->mNumChildren; ++n)
    {
        ProcessNode(lOffset, outMesh, node->mChildren[n], scene, localXForm);
    }
}
std::tuple<aiTextureType, aiTextureType> c_textureOrder[4] =
{
    {aiTextureType_BASE_COLOR, aiTextureType_DIFFUSE },
    {aiTextureType_NORMAL_CAMERA, aiTextureType_NORMALS },
    {aiTextureType_METALNESS, aiTextureType_SPECULAR },
    {aiTextureType_DIFFUSE_ROUGHNESS, aiTextureType_SHININESS },
};

bool Importer::LoadMesh(const char* path, std::vector<MeshData>& outMesh, std::vector<ImportMatInfo>& outMaterials, bool bake)
{
    auto compiledPath = L"assets" / std::filesystem::path(path).filename();
    compiledPath.replace_extension(L"acm");
    if (std::filesystem::exists(compiledPath))
    {
        return LoadCompiledMesh(path, outMesh, outMaterials);
    }
    else
    {
        if (ImportMesh(path, outMesh, outMaterials, bake))
        {
            return CompileMesh(path, outMesh, outMaterials);
        }
    }
    return false;
}

bool Importer::ImportMesh(const char* path, std::vector<Awesome::MeshData>& outMesh, std::vector<ImportMatInfo>& outMaterials, bool bake)
{
    DebugPrint("Importing Mesh %s\n", path);
    Assimp::DefaultLogger::create("", Assimp::Logger::NORMAL);
    Assimp::Importer importer;

    uint32 flags = aiProcess_Triangulate
        | aiProcess_GenSmoothNormals
        //| aiProcess_OptimizeGraph 
        //| aiProcess_OptimizeMeshes
        | aiProcess_SortByPType
        | aiProcess_JoinIdenticalVertices
        | aiProcess_ImproveCacheLocality
        | aiProcess_PopulateArmatureData
        | aiProcess_ConvertToLeftHanded;

#if _DEBUG
    flags |= aiProcess_ValidateDataStructure;
#endif

    if (bake)
        flags |= aiProcess_PreTransformVertices;

    const aiScene* scene = importer.ReadFile(path, flags);

    if (scene == nullptr)
        return false;

    DebugPrint("Number of lights: %d\n", scene->mNumLights);
    if (scene->hasSkeletons())
        DebugPrint("Has Skeletons\n");
    if (scene->HasAnimations())
        DebugPrint("Has Animations\n");

    ProcessNode(0, outMesh, scene->mRootNode, scene, aiMatrix4x4());

    DebugPrint("Generating Tangents\n");

    SMikkTSpaceInterface funcs = {};
    funcs.m_getNumFaces = GetNumFaces;
    funcs.m_getNumVerticesOfFace = GetVertexPerFace;
    funcs.m_getPosition = GetPosition;
    funcs.m_getNormal = GetNormal;
    funcs.m_getTexCoord = GetTexcoord;
    funcs.m_setTSpaceBasic = SetTangentsBasic;

    for (uint32 i = 0; i < outMesh.size(); i++)
    {
        DebugPrint("\t%s\n", outMesh[i].name.c_str());
        SMikkTSpaceContext context = {};
        context.m_pInterface = &funcs;
        context.m_pUserData = &outMesh[i];
        genTangSpaceDefault(&context);
    }

    for (uint32 i = 0; i < scene->mNumMaterials; i++)
    {
        aiMaterial* mat = scene->mMaterials[i];
        ImportMatInfo& matInfo = outMaterials.emplace_back();
        matInfo.name = std::string(mat->GetName().C_Str());

        uint32 start_t = aiTextureType_BASE_COLOR;

        aiShadingMode shading_mode = aiShadingMode_Flat;
        if (mat->Get(AI_MATKEY_SHADING_MODEL, shading_mode) == aiReturn_SUCCESS) {
            if (shading_mode == aiShadingMode_Phong)
                start_t = aiTextureType_DIFFUSE;
        }
        for (uint32 t = 0; t < 4; t++)
        {
            auto tTypes = c_textureOrder[t];
            auto tType = std::get<0>(tTypes);
            int count = mat->GetTextureCount(tType);
            if (count == 0)
            {
                tType = std::get<1>(tTypes);
                count = mat->GetTextureCount(tType);
            }

            for (int j = 0; j < count; j++)
            {
                aiString path;
                auto ret = mat->GetTexture(tType, j, &path);
                if (path.length > 0) {
                    matInfo.texturePaths.push_back(std::string(path.C_Str()));
                    matInfo.textureTypes.push_back(t);
                }
                else
                {
                    DebugPrint("Missing path for: %d\n", tType);
                    if (tType == aiTextureType_NORMAL_CAMERA)
                    {
                        std::string newPath = "textures\\" + matInfo.name + "_Normal.png";
                        DebugPrint("It should be a normal map, guessing the file path of: %s\n", newPath.c_str());
                        matInfo.texturePaths.push_back(std::string(newPath.c_str()));
                        matInfo.textureTypes.push_back(t);
                    }
                }
            }
        }
    }
    return true;
}

bool Importer::CompileMesh(const char* path, std::vector<MeshData>& outMesh, std::vector<ImportMatInfo>& outMaterials)
{
    auto filePath = L"assets" / std::filesystem::path(path).filename();
    filePath.replace_extension(L"acm");
    DebugPrint("Saving compiled mesh to %S\n", filePath.c_str());
    if (!std::filesystem::exists(L"assets"))
    {
        std::filesystem::create_directory(L"assets");
    }

    // open the file:
    std::ofstream file(filePath, std::ios::out | std::ios::binary);
    if (file.is_open())
    {
        // Mesh data
        size_t mdlen = outMesh.size();
        file.write((char*)&mdlen, sizeof(mdlen));
        for (int i = 0; i < outMesh.size(); i++)
        {
            MeshData& md = outMesh[i];

            // Mesh name
            size_t nameLen = md.name.size();
            file.write((char*)&nameLen, sizeof(nameLen));
            file.write((char*)&md.name[0], nameLen);

            // Material Index
            file.write((char*)&md.materialIndex, sizeof(md.materialIndex));

            // Vertices
            size_t vertLen = md.vertices.size();
            file.write((char*)&vertLen, sizeof(vertLen));
            file.write((char*)&md.vertices[0], sizeof(VertexData) * vertLen);

            // Indices
            size_t indexLen = md.indices.size();
            file.write((char*)&indexLen, sizeof(indexLen));
            file.write((char*)&md.indices[0], sizeof(uint32) * indexLen);

            // Transform
            file.write((char*)&md.transform.position, sizeof(XMFLOAT3));
            file.write((char*)&md.transform.rotation, sizeof(XMFLOAT3));
            file.write((char*)&md.transform.scale, sizeof(XMFLOAT3));
        }

        // Material Info

        size_t mnlen = outMaterials.size();
        file.write((char*)&mnlen, sizeof(mnlen));
        for (int i = 0; i < outMaterials.size(); i++)
        {
            ImportMatInfo& imi = outMaterials[i];

            // Material name
            size_t nameLen = imi.name.size();
            file.write((char*)&nameLen, sizeof(nameLen));
            file.write((char*)&imi.name[0], nameLen);

            // Texture Paths
            size_t texPathLen = imi.texturePaths.size();
            file.write((char*)&texPathLen, sizeof(texPathLen));
            for (int t = 0; t < imi.texturePaths.size(); t++)
            {
                size_t tpl = imi.texturePaths[t].size();
                file.write((char*)&tpl, sizeof(tpl));
                std::string& tp = imi.texturePaths[t];
                file.write((char*)&tp[0], tpl);
            }

            // Types
            size_t texTypeLen = imi.textureTypes.size();
            file.write((char*)&texTypeLen, sizeof(texTypeLen));
            if(texTypeLen > 0)
                file.write((char*)&imi.textureTypes[0], sizeof(uint32) * texTypeLen);

        }
        return true;
    }
    return false;
}

bool Importer::LoadCompiledMesh(const char* path, std::vector<MeshData>& outMesh, std::vector<ImportMatInfo>& outMaterials)
{
    auto filePath = L"assets" / std::filesystem::path(path).filename();
    filePath.replace_extension(L"acm");
    DebugPrint("Loading compiled mesh from %S\n", filePath.c_str());

    // open the file:
    std::ifstream file(filePath, std::ios::in | std::ios::binary);
    if (file.is_open())
    {
        // Mesh data
        size_t mdlen = 0;
        file.read((char*)&mdlen, sizeof(mdlen));
        outMesh.resize(mdlen);
        for (int i = 0; i < outMesh.size(); i++)
        {
            MeshData& md = outMesh[i];

            md.visible = true;

            // Mesh name
            size_t nameLen = 0;
            file.read((char*)&nameLen, sizeof(nameLen));
            md.name.resize(nameLen);
            file.read((char*)&md.name[0], nameLen);

            // Material Index
            file.read((char*)&md.materialIndex, sizeof(md.materialIndex));

            // Vertices
            size_t vertLen = 0;
            file.read((char*)&vertLen, sizeof(vertLen));
            md.vertices.resize(vertLen);
            file.read((char*)&md.vertices[0], sizeof(VertexData) * vertLen);

            // Indices
            size_t indexLen = md.indices.size();
            file.read((char*)&indexLen, sizeof(indexLen));
            md.indices.resize(indexLen);
            file.read((char*)&md.indices[0], sizeof(uint32) * indexLen);

            // Transform
            file.read((char*)&md.transform.position, sizeof(XMFLOAT3));
            file.read((char*)&md.transform.rotation, sizeof(XMFLOAT3));
            file.read((char*)&md.transform.scale, sizeof(XMFLOAT3));

            md.transform.CalcMatrix();
        }

        // Material Info

        size_t mnlen = 0;
        file.read((char*)&mnlen, sizeof(mnlen));
        outMaterials.resize(mnlen);
        for (int i = 0; i < outMaterials.size(); i++)
        {
            ImportMatInfo& imi = outMaterials[i];

            // Material name
            size_t nameLen = 0;
            file.read((char*)&nameLen, sizeof(nameLen));
            imi.name.resize(nameLen);
            file.read((char*)&imi.name[0], nameLen);

            // Texture Paths
            size_t texPathLen = 0;
            file.read((char*)&texPathLen, sizeof(texPathLen));
            imi.texturePaths.resize(texPathLen);
            for (int t = 0; t < texPathLen; t++)
            {
                size_t tpl = 0;
                file.read((char*)&tpl, sizeof(tpl));
                std::string& tp = imi.texturePaths[t];
                tp.resize(tpl);                
                file.read((char*)&tp[0], tpl);
            }

            // Types
            size_t texTypeLen = 0;
            file.read((char*)&texTypeLen, sizeof(texTypeLen));
            if (texTypeLen > 0)
            {
                imi.textureTypes.resize(texTypeLen);
                file.read((char*)&imi.textureTypes[0], sizeof(uint32) * texTypeLen);
            }

        }
        return true;
    }
    return false;
}

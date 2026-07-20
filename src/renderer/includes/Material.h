#pragma once
#include "types.h"
#include <d3d12.h>
#include <vector>
#include <DirectXMath.h>
#include "Transform.h"
#include "Constants.h"

namespace Awesome
{
    class AwesomeGraphics;
    struct Resource;

    enum ShaderType
    {
        Vertex,
        Pixel,
        Compute,
        Amplification,
        MeshShader
    };

    enum ShaderPermute
    {
        Base,
        Shadow,
        Count
    };

    static const char* ShaderTypeToTarget(ShaderType type)
    {
        switch (type)
        {
        case ShaderType::Vertex:
            return "vs_6_6";
        case ShaderType::Pixel:
            return "ps_6_6";
        case ShaderType::Compute:
            return "cs_6_6";
        case ShaderType::Amplification:
            return "cs_6_6";
        case ShaderType::MeshShader:
            return "cs_6_6";
        default:
            return "unknown";
        }
    }

    struct ShaderData
    {
        D3D12_SHADER_BYTECODE shader;
        ShaderType type;
    };

    struct MaterialData
    {
        uint32 m_psoIndex;
        uint32 m_texturesCount;
        uint32 m_textures[6];
    };

    class MaterialSystem
    {
    public:
        MaterialSystem(AwesomeGraphics* Awesome);
        ~MaterialSystem();

        bool StartUp();
        bool TearDown();

        void SetupRender();
        void SetupMaterial(const uint32 instance, const uint32 index, const XMMATRIX xform, const XMMATRIX viewProj, const XMMATRIX prevViewProj, const  XMMATRIX jitter);
        void SetupShadow(const uint32 instance, const uint32 index, const uint32 cascade, const XMMATRIX& viewProj);

        void PostRender();

        uint32 CreateMaterial(uint32 pso, const std::vector<uint32>& textures);

        uint32 CreatePipeline(uint32 vertex, uint32 pixel, const D3D12_INPUT_LAYOUT_DESC& meshLayout, bool twosided=false);

        uint32 CreateUIPipeline(uint32 vertex, uint32 pixel, const D3D12_INPUT_LAYOUT_DESC& meshLayout, ID3D12RootSignature* rootSignature, bool hdr=false);

        void SetPSO(uint32 pso);

        uint32 LoadShader(const wchar_t* path, ShaderType type, const std::vector< D3D_SHADER_MACRO*>& permuations);
        uint32 CompileShader(const wchar_t* path, ShaderType type, const std::vector<D3D_SHADER_MACRO*>& permuations);
        uint32 LoadCompiledShader(const wchar_t* path, ShaderType type, const std::vector<D3D_SHADER_MACRO*>& permuations);

        const ShaderData& GetShader(uint32 index);

        uint32 GetMaterialCount();

        void SetupResources();
        void FreeResources();

    private:
        AwesomeGraphics* m_Awesome;
        ID3D12RootSignature* m_rootSignature;
        std::vector<MaterialData> m_materialData;
        std::vector<ID3D12PipelineState*> m_forwardPSOs;
        std::vector<ID3D12PipelineState*> m_deferredPSOs;
        std::vector<ShaderData> m_shaderData;

        // Constant Buffers
        Resource* m_vertConstantBufferUploadHeap;
        Resource* m_shadowConstantBufferUploadHeap;
        Resource* m_pixelConstantBufferUploadHeap;

        uint8* m_vertexConstantsGPUAddress;
        uint8* m_shadowConstantsGPUAddress;
        uint8* m_pixelConstantsGPUAddress;

        uint32 m_lastPSO;

        uint32 m_vcOffset;
        uint32 m_scOffset;
        uint32 m_pcOffset;
    };
};
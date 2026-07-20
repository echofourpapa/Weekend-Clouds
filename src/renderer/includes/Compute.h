#pragma once
#include "types.h"
#include <d3d12.h>
#include <vector>
#include <string>

namespace Awesome
{
    const uint32 c_compute_thread_x = 8;
    const uint32 c_compute_thread_y = 8;
    const uint32 c_compute_thread_z = 1;

    const D3D12_STATIC_SAMPLER_DESC c_computeSamplers[6] = {
        {
            D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT,           // D3D12_FILTER Filter;
            D3D12_TEXTURE_ADDRESS_MODE_WRAP,           // D3D12_TEXTURE_ADDRESS_MODE AddressU;
            D3D12_TEXTURE_ADDRESS_MODE_WRAP,           // D3D12_TEXTURE_ADDRESS_MODE AddressV;
            D3D12_TEXTURE_ADDRESS_MODE_WRAP,           // D3D12_TEXTURE_ADDRESS_MODE AddressW;
            0,                                         // FLOAT MipLODBias;
            0,                                         // UINT MaxAnisotropy;
            D3D12_COMPARISON_FUNC_NOT_EQUAL,               // D3D12_COMPARISON_FUNC ComparisonFunc;
            D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK,    // D3D12_STATIC_BORDER_COLOR BorderColor;
            0.0f,                                      // FLOAT MinLOD;
            D3D12_FLOAT32_MAX,                         // FLOAT MaxLOD;
            0,                                         // UINT ShaderRegister;
            0,                                         // UINT RegisterSpace;
            D3D12_SHADER_VISIBILITY_ALL                // D3D12_SHADER_VISIBILITY ShaderVisibility;
        },
        {
            D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT,
            D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
            D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
            D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
            0,
            0,
            D3D12_COMPARISON_FUNC_NEVER,
            D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK,
            0.0f,
            D3D12_FLOAT32_MAX,
            1,
            0,
            D3D12_SHADER_VISIBILITY_ALL
        },
        {
            D3D12_FILTER_MIN_MAG_MIP_POINT,
            D3D12_TEXTURE_ADDRESS_MODE_WRAP,
            D3D12_TEXTURE_ADDRESS_MODE_WRAP,
            D3D12_TEXTURE_ADDRESS_MODE_WRAP,
            0,
            0,
            D3D12_COMPARISON_FUNC_NEVER,
            D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK,
            0.0f,
            D3D12_FLOAT32_MAX,
            2,
            0,
            D3D12_SHADER_VISIBILITY_ALL
        },
        {
            D3D12_FILTER_MIN_MAG_MIP_POINT,
            D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
            D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
            D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
            0,
            0,
            D3D12_COMPARISON_FUNC_NEVER,
            D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK,
            0.0f,
            D3D12_FLOAT32_MAX,
            3,
            0,
            D3D12_SHADER_VISIBILITY_ALL
        },
        {
            D3D12_FILTER_MIN_MAG_MIP_POINT,
            D3D12_TEXTURE_ADDRESS_MODE_BORDER,
            D3D12_TEXTURE_ADDRESS_MODE_BORDER,
            D3D12_TEXTURE_ADDRESS_MODE_BORDER,
            0,
            0,
            D3D12_COMPARISON_FUNC_NEVER,
            D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK,
            0.0f,
            D3D12_FLOAT32_MAX,
            4,
            0,
            D3D12_SHADER_VISIBILITY_ALL
        },
        {
            D3D12_FILTER_MIN_MAG_MIP_LINEAR,           // D3D12_FILTER Filter;
            D3D12_TEXTURE_ADDRESS_MODE_WRAP,           // D3D12_TEXTURE_ADDRESS_MODE AddressU;
            D3D12_TEXTURE_ADDRESS_MODE_WRAP,           // D3D12_TEXTURE_ADDRESS_MODE AddressV;
            D3D12_TEXTURE_ADDRESS_MODE_WRAP,           // D3D12_TEXTURE_ADDRESS_MODE AddressW;
            0,                                         // FLOAT MipLODBias;
            0,                                         // UINT MaxAnisotropy;
            D3D12_COMPARISON_FUNC_NOT_EQUAL,               // D3D12_COMPARISON_FUNC ComparisonFunc;
            D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK,    // D3D12_STATIC_BORDER_COLOR BorderColor;
            0.0f,                                      // FLOAT MinLOD;
            D3D12_FLOAT32_MAX,                         // FLOAT MaxLOD;
            5,                                         // UINT ShaderRegister;
            0,                                         // UINT RegisterSpace;
            D3D12_SHADER_VISIBILITY_ALL                // D3D12_SHADER_VISIBILITY ShaderVisibility;
        }
    };

	class AwesomeGraphics;

	class ComputeSystem
	{
	public: 
		ComputeSystem(AwesomeGraphics* Awesome);
		~ComputeSystem();
		bool StartUp();
		bool TearDown();
		uint32 CompileShader(const wchar_t* path, const std::vector<D3D_SHADER_MACRO*>& permuations);
		uint32 CreatePipeline(uint32 shaderIndex, ID3D12RootSignature* rootSignature);

		void SetPSO(uint32 index);

	private:
		AwesomeGraphics* m_Awesome;
		std::vector<ID3D12PipelineState*> m_pipelineStateObjects;
	};
};
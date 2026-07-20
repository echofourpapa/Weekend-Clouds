#include "Compute.h"
#include "Awesome.h"
#include "Material.h"
#include "Util.h"
#include "pix3.h"

using namespace Awesome;

ComputeSystem::ComputeSystem(AwesomeGraphics* Awesome)
	: m_Awesome(Awesome)
	, m_pipelineStateObjects(0)

{
    m_pipelineStateObjects.reserve(20);
}

ComputeSystem::~ComputeSystem()
{
}

bool ComputeSystem::StartUp()
{
	return true;
}

bool ComputeSystem::TearDown()
{
    for (uint32 i = 0; i < m_pipelineStateObjects.size(); ++i)
    {
        SafeRelease(m_pipelineStateObjects[i]);
    }

	return true;
}

uint32 ComputeSystem::CompileShader(const wchar_t* path, const std::vector<D3D_SHADER_MACRO*>& permuations)
{
	return m_Awesome->GetMaterialSystem()->LoadShader(path, ShaderType::Compute, permuations);
}

uint32 Awesome::ComputeSystem::CreatePipeline(uint32 shaderIndex, ID3D12RootSignature* rootSignature)
{
    // Create the Compute pipeline state object
    ID3D12PipelineState* pipelineStateObject;
    {
        D3D12_COMPUTE_PIPELINE_STATE_DESC desc;
        desc.pRootSignature = rootSignature;
        desc.CS = m_Awesome->GetMaterialSystem()->GetShader(shaderIndex).shader;
        desc.NodeMask = 1;
        desc.CachedPSO = { nullptr, 0 };
        desc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
        if (FAILED(m_Awesome->Device()->CreateComputePipelineState(&desc, IID_PPV_ARGS(&pipelineStateObject))))
            return -1;
    }

    uint32 index = uint32(m_pipelineStateObjects.size());

    m_pipelineStateObjects.push_back(pipelineStateObject);

    return index;
}

void Awesome::ComputeSystem::SetPSO(uint32 index)
{
    m_Awesome->GetCommandList()->SetPipelineState(m_pipelineStateObjects[index]);
}
#include "ScreenSpaceShadows.h"
#include "Util.h"
#include "Compute.h"
#include "Material.h"
#include "Deferred.h"
#include "Scene.h"
#include "ResourcePool.h"

/*
Trying to do this:
https://www.bendstudio.com/blog/inside-bend-screen-space-shadows/
*/


using namespace Awesome;
using namespace DirectX;

ScreenSpaceShadows::ScreenSpaceShadows(AwesomeGraphics* Awesome)
	: m_Awesome(Awesome)
	, m_rootSignature(nullptr)
	, m_screenSpaceShadowsConstantBuffer(nullptr)
	, m_screenSpaceShadowsOutput(nullptr)
	, m_screenSpaceShadowsComputePSO(invalidIndex32)
{
}

ScreenSpaceShadows::~ScreenSpaceShadows()
{
}

bool ScreenSpaceShadows::StartUp()
{
    // Create Root Signature
    {
        D3D12_DESCRIPTOR_RANGE  descriptorTableRanges[2];

        descriptorTableRanges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        descriptorTableRanges[0].NumDescriptors = 1;
        descriptorTableRanges[0].BaseShaderRegister = 0;
        descriptorTableRanges[0].RegisterSpace = 0;
        descriptorTableRanges[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        descriptorTableRanges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        descriptorTableRanges[1].NumDescriptors = 1;
        descriptorTableRanges[1].BaseShaderRegister = 0;
        descriptorTableRanges[1].RegisterSpace = 0;
        descriptorTableRanges[1].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        // create a descriptor table
        D3D12_ROOT_DESCRIPTOR_TABLE descriptorTable;
        descriptorTable.NumDescriptorRanges = _countof(descriptorTableRanges);
        descriptorTable.pDescriptorRanges = descriptorTableRanges;

        D3D12_ROOT_PARAMETER  rootParameters[2];

        rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rootParameters[0].DescriptorTable = descriptorTable;
        rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        rootParameters[1].Descriptor = { 0,0 };
        rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        D3D12_ROOT_SIGNATURE_DESC desc = {};
        desc.NumParameters = _countof(rootParameters);
        desc.pParameters = rootParameters;
        desc.NumStaticSamplers = _countof(c_computeSamplers);
        desc.pStaticSamplers = c_computeSamplers;
        desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

        ID3DBlob* signature;
        ID3DBlob* errorBuff;
        if (FAILED(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &errorBuff)))
        {
            DebugPrint((char*)errorBuff->GetBufferPointer());
            return false;
        }

        if (FAILED(m_Awesome->Device()->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature))))
            return false;
    }

    std::vector<D3D_SHADER_MACRO*> permuations;
    D3D_SHADER_MACRO* defines = new D3D_SHADER_MACRO[1];
    defines[0] = { NULL, NULL };
    permuations.push_back(defines);

    uint32 shader = m_Awesome->GetComputeSystem()->CompileShader(L"ScreenSpaceShadows-c", permuations);
    m_screenSpaceShadowsComputePSO = m_Awesome->GetComputeSystem()->CreatePipeline(shader, m_rootSignature);

	return true;
}

bool ScreenSpaceShadows::TearDown()
{
	SafeRelease(m_rootSignature);
    FreeResources();
	return true;
}

void ScreenSpaceShadows::Resize()
{

}

void ScreenSpaceShadows::SetupRender()
{
    InitResources();
}

void ScreenSpaceShadows::Render(float delta)
{
    PIXScopedEvent(m_Awesome->GetCommandList(), 0, "Screen Space Shadows");
    
    {
        PIXScopedEvent(m_Awesome->GetCommandList(), 0, "Screen Space Shadows Render");
        m_Awesome->TransitionResource(m_Awesome->GetDepthStencilBuffer(), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

        m_Awesome->GetCommandList()->SetComputeRootSignature(m_rootSignature);

        m_Awesome->GetComputeSystem()->SetPSO(m_screenSpaceShadowsComputePSO);

        ScreenSpaceShadowsConstants constants = {};
        constants.SurfaceThickness = m_surfaceThickness;
        constants.BilinearThreshold = m_bilinearThreshold;
        constants.ShadowContrast = m_shadowContrast;

        XMMATRIX projMat = m_Awesome->GetCurrentScene()->GetCamera()->GetProjectionMatrix();
        XMMATRIX viewMat = m_Awesome->GetCurrentScene()->GetCamera()->GetViewSpaceMatrix();
        XMMATRIX viewProj = viewMat * projMat;

        XMFLOAT4 lightPos = {
            m_Awesome->GetCurrentScene()->GetSunLight()->position.x,
            m_Awesome->GetCurrentScene()->GetSunLight()->position.y,
            m_Awesome->GetCurrentScene()->GetSunLight()->position.z,
            0.f
        };
        
        // Pre-transforming the light
        XMVECTOR lightDir = XMLoadFloat4(&lightPos);
        // Want the direction of the light, not the direction to the light?
        lightDir = XMVector3Normalize(lightDir);
        lightDir = XMVector4Transform(lightDir, viewProj);

        XMFLOAT4 lightData{};
        XMStoreFloat4(&lightData, lightDir);

        //Some hacked in constants:
        int32 inWaveSize = 64;
        float f_width = float(m_Awesome->GetWidth());
        float f_height = float(m_Awesome->GetHeight());

        float xy_light_w = lightData.w;
        float FP_limit = 0.000002f * (float)inWaveSize;

        if (xy_light_w >= 0 && xy_light_w < FP_limit) xy_light_w = FP_limit;
        else if (xy_light_w < 0 && xy_light_w > -FP_limit) xy_light_w = -FP_limit;

        constants.lightPos.x = ((lightData.x / xy_light_w) * +0.5f + 0.5f) * f_width;
        constants.lightPos.y = ((lightData.y / xy_light_w) * -0.5f + 0.5f) * f_height;
        constants.lightPos.z = lightData.w == 0 ? 0.f : (lightData.z / lightData.w);
        constants.lightPos.w = lightData.w > 0 ? 1.f : -1.f;

        int light_xy[2] = { (int)(constants.lightPos.x + 0.5f), (int)(constants.lightPos.y + 0.5f) };
        // Make the bounds inclusive, relative to the light
        const int biased_bounds[4] =
        {
            0 - light_xy[0],
            -(m_Awesome->GetHeight() - light_xy[1]),
            m_Awesome->GetWidth() - light_xy[0],
            -(0 - light_xy[1]),
        };

        struct DispatchData
        {
            int WaveCount[3];					// Compute Shader Dispatch(X,Y,Z) wave counts X/Y/Z
            int WaveOffset_Shader[2];			// This value is passed in to shader. It will be different for each dispatch
        };

        DispatchData Dispatch[8];			// List of dispatches (max count is 8)
        int32 DispatchCount = 0;

        for (uint32 i = 0; i < 8; i++)
        {
            Dispatch[i] = {};
        }

        // Process 4 quadrants around the light center,
        // They each form a rectangle with one corner on the light XY coordinate
        // If the rectangle isn't square, it will need breaking in two on the larger axis
        // 0 = bottom left, 1 = bottom right, 2 = top left, 2 = top right
        for (uint32 q = 0; q < 4; q++)
        {
            // Quads 0 and 3 needs to be +1 vertically, 1 and 2 need to be +1 horizontally
            bool vertical = q == 0 || q == 3;

            // Bounds relative to the quadrant
            const int32 bounds[4] =
            {
                std::max(0, ((q & 1) ? biased_bounds[0] : -biased_bounds[2])) / inWaveSize,
                std::max(0, ((q & 2) ? biased_bounds[1] : -biased_bounds[3])) / inWaveSize,
                std::max(0, (((q & 1) ? biased_bounds[2] : -biased_bounds[0]) + inWaveSize * (vertical ? 1 : 2) - 1)) / inWaveSize,
                std::max(0, (((q & 2) ? biased_bounds[3] : -biased_bounds[1]) + inWaveSize * (vertical ? 2 : 1) - 1)) / inWaveSize,
            };
            if ((bounds[2] - bounds[0]) > 0 && (bounds[3] - bounds[1]) > 0)
            {
                int bias_x = (q == 2 || q == 3) ? 1 : 0;
                int bias_y = (q == 1 || q == 3) ? 1 : 0;

                DispatchData& disp = Dispatch[DispatchCount++];

                disp.WaveCount[0] = inWaveSize;
                disp.WaveCount[1] = bounds[2] - bounds[0];
                disp.WaveCount[2] = bounds[3] - bounds[1];
                disp.WaveOffset_Shader[0] = ((q & 1) ? bounds[0] : -bounds[2]) + bias_x;
                disp.WaveOffset_Shader[1] = ((q & 2) ? -bounds[3] : bounds[1]) + bias_y;

                // We want the far corner of this quadrant relative to the light,
                // as we need to know where the diagonal light ray intersects with the edge of the bounds
                int axis_delta = +biased_bounds[0] - biased_bounds[1];
                if (q == 1) axis_delta = +biased_bounds[2] + biased_bounds[1];
                if (q == 2) axis_delta = -biased_bounds[0] - biased_bounds[3];
                if (q == 3) axis_delta = -biased_bounds[2] + biased_bounds[3];

                axis_delta = (axis_delta + inWaveSize - 1) / inWaveSize;
                if (axis_delta > 0)
                {
                    DispatchData& disp2 = Dispatch[DispatchCount++];

                    // Take copy of current volume
                    disp2 = disp;

                    if (q == 0)
                    {
                        // Split on Y, split becomes -1 larger on x
                        disp2.WaveCount[2] = std::min(disp.WaveCount[2], axis_delta);
                        disp.WaveCount[2] -= disp2.WaveCount[2];
                        disp2.WaveOffset_Shader[1] = disp.WaveOffset_Shader[1] + disp.WaveCount[2];
                        disp2.WaveOffset_Shader[0]--;
                        disp2.WaveCount[1]++;
                    }
                    if (q == 1)
                    {
                        // Split on X, split becomes +1 larger on y
                        disp2.WaveCount[1] = std::min(disp.WaveCount[1], axis_delta);
                        disp.WaveCount[1] -= disp2.WaveCount[1];
                        disp2.WaveOffset_Shader[0] = disp.WaveOffset_Shader[0] + disp.WaveCount[1];
                        disp2.WaveCount[2]++;
                    }
                    if (q == 2)
                    {
                        // Split on X, split becomes -1 larger on y
                        disp2.WaveCount[1] = std::min(disp.WaveCount[1], axis_delta);
                        disp.WaveCount[1] -= disp2.WaveCount[1];
                        disp.WaveOffset_Shader[0] += disp2.WaveCount[1];
                        disp2.WaveCount[2]++;
                        disp2.WaveOffset_Shader[1]--;
                    }
                    if (q == 3)
                    {
                        // Split on Y, split becomes +1 larger on x
                        disp2.WaveCount[2] = std::min(disp.WaveCount[2], axis_delta);
                        disp.WaveCount[2] -= disp2.WaveCount[2];
                        disp.WaveOffset_Shader[1] += disp2.WaveCount[2];
                        disp2.WaveCount[1]++;
                    }

                    // Remove if too small
                    if (disp2.WaveCount[1] <= 0 || disp2.WaveCount[2] <= 0)
                    {
                        disp2 = Dispatch[--DispatchCount];
                    }
                    if (disp.WaveCount[1] <= 0 || disp.WaveCount[2] <= 0)
                    {
                        disp = Dispatch[--DispatchCount];
                    }
                }
            }
        }

        // Scale the shader values by the wave count, the shader expects this
        for (int i = 0; i < DispatchCount; i++)
        {
            Dispatch[i].WaveOffset_Shader[0] *= inWaveSize;
            Dispatch[i].WaveOffset_Shader[1] *= inWaveSize;
        }

        constants.screenSize = {
            1.0f / f_width,
            1.0f / f_height,
            f_width,
            f_height };

        void* mapped = nullptr;
        m_screenSpaceShadowsConstantBuffer->resource->Map(0, nullptr, &mapped);
        for (int32 i = 0; i < DispatchCount; i++)
        {
            constants.waveOffset[0] = Dispatch[i].WaveOffset_Shader[0];
            constants.waveOffset[1] = Dispatch[i].WaveOffset_Shader[1];

            uint32 offset = ScreenSpaceShadowsConstantsAlignedSize * i;
            memcpy((uint8*)mapped + offset, &constants, sizeof(ScreenSpaceShadowsConstants));
            
        }
        m_screenSpaceShadowsConstantBuffer->resource->Unmap(0, nullptr);
        m_Awesome->GetCommandList()->SetComputeRootDescriptorTable(0, m_handles[0].gpuHandle);

        for(int32 i = 0; i < DispatchCount; i++)
        {
            uint32 offset = ScreenSpaceShadowsConstantsAlignedSize * i;
            m_Awesome->GetCommandList()->SetComputeRootConstantBufferView(1, m_screenSpaceShadowsConstantBuffer->resource->GetGPUVirtualAddress() + offset);

            uint32 dispatchX = Dispatch[i].WaveCount[0];
            uint32 dispatchY = Dispatch[i].WaveCount[1];
            uint32 dispatchZ = Dispatch[i].WaveCount[2];

            m_Awesome->GetCommandList()->Dispatch(dispatchX, dispatchY, dispatchZ);
        }
        m_Awesome->TransitionResource(m_Awesome->GetDepthStencilBuffer(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);
    }
    FreeResources();
}

ID3D12Resource* ScreenSpaceShadows::GetOutputBuffer()
{
    return m_screenSpaceShadowsOutput->resource;
}

void ScreenSpaceShadows::InitResources()
{
    m_Awesome->GetMainDescHeap()->AllocateBlock(m_handles, 3);

    // Shader Resource Views
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC dsDesc;
        ZeroMemory(&dsDesc, sizeof(dsDesc));
        dsDesc.Texture2D.MipLevels = 1;
        dsDesc.Texture2D.MostDetailedMip = 0;
        dsDesc.Format = DXGI_FORMAT_R32_FLOAT;
        dsDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        dsDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

        m_Awesome->Device()->CreateShaderResourceView(m_Awesome->GetDepthStencilBuffer(), &dsDesc, m_handles[0].cpuHandle);
    }

    // Deferred Output Buffer
    {
        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Alignment = 0;
        desc.Width = m_Awesome->GetWidth();
        desc.Height = m_Awesome->GetHeight();
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.SampleDesc.Count = 1;
        desc.SampleDesc.Quality = 0;
        desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        desc.Format = DXGI_FORMAT_R32_FLOAT;

        ResourceDesc rDesc = {};
        rDesc.desc = desc;
        rDesc.state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        m_screenSpaceShadowsOutput = m_Awesome->GetResourcePool()->Allocate(rDesc, L"Screen Space Shadows Output Buffer", D3D12_HEAP_TYPE_DEFAULT);


        m_Awesome->Device()->CreateUnorderedAccessView(m_screenSpaceShadowsOutput->resource, nullptr, nullptr, m_handles[1].cpuHandle);
    }

    // Constant Buffer & View
    {
        D3D12_RESOURCE_DESC resDesc = {};
        resDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        resDesc.Alignment = 0;
        resDesc.Width = ScreenSpaceShadowsConstantsAlignedSize * 8;
        resDesc.Height = 1;
        resDesc.DepthOrArraySize = 1;
        resDesc.MipLevels = 1;
        resDesc.Format = DXGI_FORMAT_UNKNOWN;
        resDesc.SampleDesc.Count = 1;
        resDesc.SampleDesc.Quality = 0;
        resDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        resDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

        ResourceDesc rDesc = {};
        rDesc.desc = resDesc;
        rDesc.state = D3D12_RESOURCE_STATE_GENERIC_READ;
        m_screenSpaceShadowsConstantBuffer = m_Awesome->GetResourcePool()->Allocate(rDesc, L"Screen Space Shadows Constant Buffer", D3D12_HEAP_TYPE_UPLOAD);

        D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
        cbvDesc.BufferLocation = m_screenSpaceShadowsConstantBuffer->resource->GetGPUVirtualAddress();
        cbvDesc.SizeInBytes = ScreenSpaceShadowsConstantsAlignedSize;


        m_Awesome->Device()->CreateConstantBufferView(&cbvDesc, m_handles[2].cpuHandle);
    }
}

void ScreenSpaceShadows::FreeResources()
{
    m_Awesome->GetResourcePool()->Free(m_screenSpaceShadowsConstantBuffer);
    m_Awesome->GetResourcePool()->Free(m_screenSpaceShadowsOutput);
    for (auto& handle : m_handles)
        m_Awesome->GetMainDescHeap()->Free(handle);
    m_handles.clear();
}

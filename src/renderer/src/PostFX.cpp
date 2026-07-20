#include "PostFX.h"
#include "Util.h"
#include "Compute.h"
#include "Material.h"
#include "Deferred.h"
#include "Scene.h"
#include "Resource.h"
#include "ResourcePool.h"
#include "Exposure.h"
#include "Tonemap.h"

using namespace Awesome;

PostFX::PostFX(AwesomeGraphics* Awesome)
    : m_Awesome(Awesome)
    , m_exposure(new Exposure(Awesome))
    , m_tonemap(new Tonemap(Awesome))
{
}

PostFX::~PostFX()
{
}

bool PostFX::StartUp()
{
    if (!m_exposure->StartUp())
        return false;

    if (!m_tonemap->StartUp())
        return false;

    return true;
}

bool PostFX::TearDown()
{
    m_exposure->TearDown(); m_exposure = nullptr;
    m_tonemap->TearDown(); m_tonemap = nullptr;
    return true;
}

void PostFX::Resize()
{
    m_exposure->Resize();
    m_tonemap->Resize();
}

void PostFX::Render(bool tonemap, float deltaTime, ID3D12Resource* inputColor)
{
    PIXScopedEvent(m_Awesome->GetCommandList(), 0, "Post Process");

    // Auto-Exposure reads input
    m_exposure->Render(deltaTime, inputColor);

    // Tonemap reads input
    m_tonemap->Render(tonemap, inputColor);
}

ID3D12Resource* PostFX::GetOutputBuffer()
{
    return m_tonemap->GetOutputBuffer();
}
// ... [Getters/Setters unchanged]
ID3D12Resource* PostFX::GetExposureBuffer() { return m_exposure->GetOutputBuffer(); }
ID3D12Resource* PostFX::GetHistogramBuffer() { return m_exposure->GetHistogramBuffer(); }
void PostFX::SetLogMin(float min) { m_exposure->SetLogMin(min); }
float PostFX::GetLogMin() { return m_exposure->GetLogMin(); }
void PostFX::SetLogMax(float max) { m_exposure->SetLogMax(max); }
float PostFX::GetLogMax() { return m_exposure->GetLogMax(); }
void PostFX::SetAdaptRate(float value) { m_exposure->SetAdaptRate(value); }
float PostFX::GetAdaptRate() { return m_exposure->GetAdaptRate(); }

Tonemapper PostFX::GetTonemapper() { return m_tonemap->GetTonemapper(); }
void PostFX::SetTonemapper(Tonemapper t) { m_tonemap->SetTonemapper(t); }


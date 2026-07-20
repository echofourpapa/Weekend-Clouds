#include "Scene.h"
#include "Awesome.h"
#include "TAA.h"
#include "pix3.h"
#include <algorithm>
#include <random>

using namespace Awesome;

Scene::Scene(AwesomeGraphics* Awesome) 
    : m_Awesome(Awesome)
    , m_camera{}
    , m_sun{ LightType::Directional, {1.f, 8.f, 1.4f}, {1.f, 1.f, 1.f}, 0.f, 0.f }
    , m_ibl(0)
    , m_placements{}
    , m_dirtyLights(true)
    , m_debugLights(false)
    , m_animateLights(false)
{
    m_placements.reserve(20);
}

Scene::~Scene()
{
    m_Awesome = nullptr;
}

SceneObject& Scene::AddObject(Transform transform)
{
    SceneObject obj = {};
    obj.transform = transform;
    obj.visibile = true;
    return m_placements.emplace_back(obj);
}

void Scene::AddLight(Light light)
{
    switch (light.type)
    {
    case Awesome::Point:
        {
            light.baseIndex = (uint32)m_pointLights.size();
            m_pointLights.push_back(light);
        }
        break;
    case Awesome::Spot:
        {
            light.baseIndex = (uint32)m_spotLights.size();
            m_spotLights.push_back(light);
        }
        break;
    case Awesome::Directional:
    default:
        break;
    }
    m_dirtyLights = true;
}

bool Scene::Render(MeshSystem* meshRender, float delta)
{
    XMMATRIX viewProj = m_camera.GetViewProjectionSpaceMatrix();
    XMMATRIX jitter = XMMatrixIdentity();

    if (m_Awesome->GetTAAEnabled())
    {
        XMFLOAT4X4 j = m_Awesome->GetTAA()->GetFrameJitter();
        jitter = XMLoadFloat4x4(&j);
    }

    for (uint32 i = 0; i < m_placements.size(); ++i)
    {
        SceneObject& obj = m_placements[i];
        if (obj.visibile)
        {
            for(uint32 m = 0; m < obj.meshes.size(); m++)
                meshRender->Render(i, obj.meshes[m], obj.transform, viewProj, m_camera.prevViewProjMatrix, jitter);
        }
    }

    if (m_debugLights)
    {
        for (uint32 i = 0; i < m_pointLights.size(); ++i)
        {
            Light& light = m_pointLights[i];
            Transform xform = {};
            xform.position = light.position;
            float radius = light.LightRadius();
            xform.scale = { radius ,radius ,radius };
            xform.CalcMatrix();
            meshRender->RenderPrim(i, 0, xform, viewProj, m_camera.prevViewProjMatrix, jitter);
        }
    }

    return true;
}

bool Scene::RenderShadow(MeshSystem* meshRender, const uint32 cascade, const XMMATRIX viewProj)
{
    for (uint32 i = 0; i < m_placements.size(); ++i)
    {
        SceneObject& obj = m_placements[i];
        if (obj.visibile)
        {
            XMMATRIX w = obj.transform.GetTransformationMatrix();
            for (uint32 m = 0; m < obj.meshes.size(); m++)
                meshRender->RenderShadows(i, obj.meshes[m], cascade, w, viewProj);
        }
    }
    return true;
}

void Scene::SetIBL(uint32 index)
{
    m_ibl = index;
}

uint32 Scene::GetLightCount(LightType type)
{
    switch (type)
    {
        case LightType::Point:
            return (uint32)m_pointLights.size();
        case LightType::Spot:
            return (uint32)m_spotLights.size();
        case LightType::Directional:
        default:
            return invalidIndex32;
    }

}

Light* Scene::GetPointLight(uint32 index)
{
    if (index < m_pointLights.size())
        return &m_pointLights[index];
    return nullptr;
}

Light* Scene::GetSpotLight(uint32 index)
{
    if (index < m_spotLights.size())
        return &m_spotLights[index];
    return nullptr;
}

void Scene::AnimateLights(float delta)
{
    if (!m_animateLights)
    {
        return;
    }
    
    const float minX = -90.0f;
    const float maxX = 90.0f;
    const float minY = 1.0f;
    const float maxY = 161.0f;
    const float minZ = -60.0f;
    const float maxZ = 60.0f;
    const float boundaryThreshold = 8.0f;
    const float invBoundaryThreshold = 1.0f / boundaryThreshold;
    const float directionVariance = 0.05f;

    static std::default_random_engine generator;
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    std::uniform_real_distribution<float> varDist(-directionVariance, directionVariance);

    const uint32 lightCount = (uint32)m_pointLights.size();
    
    for (uint32 i = 0; i < lightCount; ++i)
    {
        Light& light = m_pointLights[i];
        
        XMVECTOR currentDir = XMLoadFloat3(&light.direction);
        
        if (XMVectorGetX(XMVector3LengthSq(currentDir)) < 0.000001f)
        {
            currentDir = XMVector3Normalize(XMVectorSet(dist(generator), dist(generator), dist(generator), 0.0f));
            XMStoreFloat3(&light.direction, currentDir);
        }
        
        currentDir = XMVector3Normalize(currentDir + XMVectorSet(varDist(generator), varDist(generator), varDist(generator), 0.0f));
        
        const float px = light.position.x;
        const float py = light.position.y;
        const float pz = light.position.z;
        
        float pushX = 0.0f;
        float pushY = 0.0f;
        float pushZ = 0.0f;
        
        float distToMinX = px - minX;
        if (distToMinX < boundaryThreshold)
        {
            float t = (1.0f - distToMinX * invBoundaryThreshold) * 2.0f;
            pushX += t * t;
        }
        
        float distToMaxX = maxX - px;
        if (distToMaxX < boundaryThreshold)
        {
            float t = (1.0f - distToMaxX * invBoundaryThreshold) * 2.0f;
            pushX -= t * t;
        }
        
        float distToMinY = py - minY;
        if (distToMinY < boundaryThreshold)
        {
            float t = (1.0f - distToMinY * invBoundaryThreshold) * 2.0f;
            pushY += t * t;
        }
        
        float distToMaxY = maxY - py;
        if (distToMaxY < boundaryThreshold)
        {
            float t = (1.0f - distToMaxY * invBoundaryThreshold) * 2.0f;
            pushY -= t * t;
        }
        
        float distToMinZ = pz - minZ;
        if (distToMinZ < boundaryThreshold)
        {
            float t = (1.0f - distToMinZ * invBoundaryThreshold) * 2.0f;
            pushZ += t * t;
        }
        
        float distToMaxZ = maxZ - pz;
        if (distToMaxZ < boundaryThreshold)
        {
            float t = (1.0f - distToMaxZ * invBoundaryThreshold) * 2.0f;
            pushZ -= t * t;
        }
        
        if (pushX != 0.0f || pushY != 0.0f || pushZ != 0.0f)
        {
            currentDir = XMVector3Normalize(currentDir + XMVectorSet(pushX, pushY, pushZ, 0.0f));
        }
        
        XMStoreFloat3(&light.direction, currentDir);
        
        XMVECTOR velocity = currentDir * (light.speed * delta);
        XMVECTOR position = XMLoadFloat3(&light.position) + velocity;
        
        XMFLOAT3 newPos;
        XMStoreFloat3(&newPos, position);
        
        newPos.x = std::clamp(newPos.x, minX, maxX);
        newPos.y = std::clamp(newPos.y, minY, maxY);
        newPos.z = std::clamp(newPos.z, minZ, maxZ);
        
        light.position = newPos;
    }
    
    m_dirtyLights = true;
}

XMMATRIX Camera::GetProjectionMatrix()
{
    return projMatrix;
}

XMMATRIX Camera::GetViewSpaceMatrix()
{
    return viewMatrix;
}

XMMATRIX Camera::GetViewProjectionSpaceMatrix()
{
    return viewProjMatrix;
}

ViewFrustum Camera::GetFrustum(float _nearClip, float _farClip)
{
    // https://stackoverflow.com/questions/13665932/calculating-the-viewing-frustum-in-a-3d-space
    XMVECTOR forward = XMLoadFloat3(&transform.GetForward());
    XMVECTOR up = XMLoadFloat3(&transform.GetUp());
    XMVECTOR right = XMLoadFloat3(&transform.GetRight());
    XMVECTOR position = XMLoadFloat3(&transform.position);

    XMVECTOR nearCenter = position + forward * _nearClip;
    XMVECTOR farCenter = position + forward * _farClip;

    float nearHeight = 2.f * (float)tan(verticalFOV / 2.f) * _nearClip;
    float farHeight = 2.f * (float)tan(verticalFOV / 2.f) * _farClip;
    float nearWidth = nearHeight * aspectRatio;
    float farWidth = farHeight * aspectRatio;

    XMVECTOR nearTopLeft = nearCenter + up * (nearHeight * 0.5f) - right * (nearWidth * 0.5f);
    XMVECTOR nearTopRight = nearCenter + up * (nearHeight * 0.5f) + right * (nearWidth * 0.5f);
    XMVECTOR nearBottomLeft = nearCenter - up * (nearHeight * 0.5f) - right * (nearWidth * 0.5f);
    XMVECTOR nearBottomRight = nearCenter - up * (nearHeight * 0.5f) + right * (nearWidth * 0.5f);

    XMVECTOR farTopLeft = farCenter + up * (farHeight * 0.5f) - right * (farWidth * 0.5f);
    XMVECTOR farTopRight = farCenter + up * (farHeight * 0.5f) + right * (farWidth * 0.5f);
    XMVECTOR farBottomLeft = farCenter - up * (farHeight * 0.5f) - right * (farWidth * 0.5f);
    XMVECTOR farBottomRight = farCenter - up * (farHeight * 0.5f) + right * (farWidth * 0.5f);

    ViewFrustum frustum = {};
    XMStoreFloat3(&frustum.nearPoints[0], nearBottomLeft);
    XMStoreFloat3(&frustum.nearPoints[1], nearTopLeft);
    XMStoreFloat3(&frustum.nearPoints[2], nearTopRight);
    XMStoreFloat3(&frustum.nearPoints[3], nearBottomRight);

    XMStoreFloat3(&frustum.farPoints[0], farBottomLeft);
    XMStoreFloat3(&frustum.farPoints[1], farTopLeft);
    XMStoreFloat3(&frustum.farPoints[2], farTopRight);
    XMStoreFloat3(&frustum.farPoints[3], farBottomRight);

    return frustum;
}

static XMMATRIX XM_CALLCONV MatrixPerspectiveFovReverseInfiniteLH(
    float FovAngleY, 
    float AspectRatio, 
    float NearZ)
{
    float h = 1.0f / tanf(FovAngleY * 0.5f);
    float w = h / AspectRatio;

    // Row-Major Layout
    DirectX::XMMATRIX M;
    M.r[0] = DirectX::XMVectorSet(w,    0.0f, 0.0f,  0.0f);
    M.r[1] = DirectX::XMVectorSet(0.0f, h,    0.0f,  0.0f);
    M.r[2] = DirectX::XMVectorSet(0.0f, 0.0f, 0.0f,  1.0f); 
    M.r[3] = DirectX::XMVectorSet(0.0f, 0.0f, NearZ, 0.0f);
    return M;
}

void Camera::StartFrame()
{
    switch (type)
    {
        case Awesome::Orthographic:
            projMatrix = XMMatrixOrthographicLH(width, height, farClip, nearClip);
        case Awesome::Perspevtive:
            projMatrix = MatrixPerspectiveFovReverseInfiniteLH(verticalFOV, aspectRatio, nearClip);
    }
    viewMatrix = transform.LookAt(target, Transform::Up, distance);
    viewProjMatrix = XMMatrixMultiply(viewMatrix, projMatrix);
}

void Camera::EndFrame()
{
    prevViewProjMatrix = viewProjMatrix;
}

void Camera::Orbit(int32 theta, int32 phi)
{
    transform.rotation.y += (float)theta * 0.25f;
    transform.rotation.x += (float)phi * 0.25f;
}

void Camera::Zoom(int32 zoom)
{
    distance += (zoom * 0.001f * distance);
}

void Camera::Pan(int32 x, int32 y)
{
    XMVECTOR quat = XMQuaternionRotationRollPitchYaw(XMConvertToRadians(transform.rotation.x), XMConvertToRadians(transform.rotation.y), XMConvertToRadians(transform.rotation.z));

    XMVECTOR up = XMLoadFloat3(&Transform::Up);
    XMVECTOR right = XMLoadFloat3(&Transform::Right);

    XMVECTOR targ = XMLoadFloat3(&target);

    right = XMVector3Rotate(right, quat) * (float)x * -0.05f;
    up = XMVector3Rotate(up, quat) * (float)y * 0.05f;

    targ = targ + right + up;

    XMStoreFloat3(&target, targ);

}

void Camera::Look(int32 theta, int32 phi)
{
    transform.rotation.y += (float)theta * 0.5f;
    transform.rotation.z += (float)phi * 0.5f;

    XMVECTOR quat = XMQuaternionRotationRollPitchYaw(XMConvertToRadians(transform.rotation.x), XMConvertToRadians(transform.rotation.y), XMConvertToRadians(transform.rotation.z));
    XMVECTOR pos = XMLoadFloat3(&transform.position);
    XMVECTOR targ = XMLoadFloat3(&Transform::Forward);
    targ = XMVector3Rotate(targ, quat) * -distance;
    targ += pos;
    XMStoreFloat3(&target, targ);
}

void Camera::WalkForward(int32 distance)
{
    XMVECTOR dir = XMLoadFloat3(&Transform::Forward);
    XMVECTOR quat = XMQuaternionRotationRollPitchYaw(XMConvertToRadians(transform.rotation.x), XMConvertToRadians(transform.rotation.y), XMConvertToRadians(transform.rotation.z));
    dir = XMVector3Rotate(dir, quat);
    Walk(dir, distance);
}

void Camera::WalkSide(int32 distance)
{
    XMVECTOR dir = XMLoadFloat3(&Transform::Right);
    XMVECTOR quat = XMQuaternionRotationRollPitchYaw(XMConvertToRadians(transform.rotation.x), XMConvertToRadians(transform.rotation.y), XMConvertToRadians(transform.rotation.z));
    dir = XMVector3Rotate(dir, quat);
    Walk(dir, distance);
}

void Camera::Walk(XMVECTOR direction, int32 distance)
{
    XMVECTOR offset = direction * (float)distance * 0.5f;

    XMVECTOR targ = XMLoadFloat3(&target);

    targ += offset;

    XMStoreFloat3(&target, targ);
}

float Camera::CalcVerticalFOV()
{
    verticalFOV = 2.f * float(atan(tan(horizontalFOV / 2.0f) / aspectRatio));
    return verticalFOV;
}

void Camera::UpdateSize(uint16 iWidth, uint16 iHeight)
{
    width = iWidth;
    height = iHeight;
    aspectRatio = (float)width / (float)height;
    verticalFOV = CalcVerticalFOV();
}

XMMATRIX Light::GetViewSpaceMatrix() const
{
    return viewMatrix;
}

XMMATRIX Light::GetProjectionMatrix() const
{
    return projMatrix;
}

XMMATRIX Light::GetViewProjSpaceMatrix() const
{
    return viewProjMatrix;
}

void Light::UpdateMatrix()
{
    XMVECTOR eye = XMLoadFloat3(&position);
    XMVECTOR up = XMLoadFloat3(&Transform::Up);
    XMVECTOR zero = XMLoadFloat3(&Transform::Zero);

    viewMatrix = XMMatrixLookAtLH(eye, zero, up);

    projMatrix = XMMatrixOrthographicLH((float)250, (float)250, -250.0f, 250);

    viewProjMatrix =  GetViewSpaceMatrix() * GetProjectionMatrix();
}

float Light::LightRadius() const
{
    return sqrtf(intensity/ c_minLightIntesity);
}

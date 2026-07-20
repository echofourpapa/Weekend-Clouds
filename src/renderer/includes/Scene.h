#pragma once
#include <vector>
#include <DirectXMath.h>
#include "types.h"
#include "Transform.h"
#include "MeshData.h"
#include <string>

namespace Awesome
{

    class AwesomeGraphics;

    struct SceneObject
    {
        Transform transform;
        std::vector<uint32> meshes;
        std::string name;
        bool visibile;
    };

    enum CameraType
    {
        Orthographic,
        Perspevtive
    };

    struct ViewFrustum
    {
        // Starts bottom left, goes clockwise
        // 1 --> 2
        // ^     |
        // |     v
        // 0 <-- 3
        XMFLOAT3 nearPoints[4];
        XMFLOAT3 farPoints[4];
    };

    struct Camera
    {
        Transform transform;
        CameraType type;
        float horizontalFOV;
        float verticalFOV;
        float nearClip;
        float farClip;
        float aspectRatio;
        uint16 width;
        uint16 height;

        XMFLOAT3 target;
        float distance;
        
        XMMATRIX viewMatrix;
        XMMATRIX projMatrix;
        XMMATRIX viewProjMatrix;
        XMMATRIX prevViewProjMatrix;

        XMMATRIX GetProjectionMatrix();
        XMMATRIX GetViewSpaceMatrix();
        XMMATRIX GetViewProjectionSpaceMatrix();

        ViewFrustum GetFrustum(float _nearClip, float _farClip);

        void StartFrame();
        void EndFrame();
        void Orbit(int32 theta, int32 phi);
        void Zoom(int32 zoom);
        void Pan(int32 x, int32 y);

        void Look(int32 theta, int32 phi);
        void WalkForward(int32 distance);
        void WalkSide(int32 distance);
        void Walk(XMVECTOR direction, int32 distance);

        float CalcVerticalFOV();

        void UpdateSize(uint16 width, uint16 height);
    };

    enum LightType
    {
        Directional,
        Point,
        Spot
    };

    struct Light
    {
        LightType type;
        XMFLOAT3 position;
        XMFLOAT3 color;
        float intensity;
        float angle;
        float speed;
        XMFLOAT3 direction;
        
        uint32 baseIndex;

        XMMATRIX viewMatrix;
        XMMATRIX projMatrix;
        XMMATRIX viewProjMatrix;

        XMMATRIX GetProjectionMatrix() const;
        XMMATRIX GetViewSpaceMatrix() const;
        XMMATRIX GetViewProjSpaceMatrix() const;

        void UpdateMatrix();

        float LightRadius() const;
    };

    class Scene
    {
    public:
        Scene(AwesomeGraphics* Awesome);
        ~Scene();

        void SetName(const char* name) { m_name = std::string(name); }
        const char* GetName() { return m_name.c_str();  }
        SceneObject& AddObject(Transform transform);
        void AddLight(Light light);
        
        Camera* GetCamera() { return &m_camera; }
        Light* GetSunLight() { return &m_sun; }
        bool Render(MeshSystem* meshRender, float delta);
        bool RenderShadow(MeshSystem* meshRender, const uint32 cascade, const XMMATRIX viewProj);
        void SetIBL(uint32 index);
        uint32 GetIBL() { return m_ibl; }

        uint32 GetObjectCount() { return (uint32)m_placements.size(); }
        SceneObject* GetSceneObject(uint32 index) { if (index < m_placements.size()) return &m_placements[index]; else return nullptr; }

        uint32 GetLightCount(LightType type);
        Light* GetPointLight(uint32 index);
        Light* GetSpotLight(uint32 index);

        bool GetDebugLights() { return m_debugLights; }
        void SetDebugLights(bool value) { m_debugLights = value; }
        
        void AnimateLights(float delta);
        
        void MarkLightsDirty() { m_dirtyLights = true; }
        void MarkLightsClean() { m_dirtyLights = false; }
        bool LightsAreDirty() { return m_dirtyLights; }
        
        bool m_animateLights;

    private:

        std::string m_name;

        AwesomeGraphics* m_Awesome;
        Camera m_camera;
        Light m_sun;
        uint32 m_ibl;

        bool m_dirtyLights;
        bool m_debugLights;

        std::vector<SceneObject> m_placements;
        std::vector<Light> m_pointLights;
        std::vector<Light> m_spotLights;
    };
};
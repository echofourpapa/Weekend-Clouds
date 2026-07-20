#pragma once
#include <DirectXMath.h>

namespace Awesome
{
    using namespace DirectX;

    struct VertexConstants
    {
        XMFLOAT4X4 worldViewProj;
        XMFLOAT4X4 PrevWorldViewProj;
        XMFLOAT4X4 world;
        XMFLOAT4X4 jitterMatrix;
    };

    struct PixelConstants
    {
        XMFLOAT4 fun;
        XMFLOAT4 texInfo[6];
    };

    struct ShadowConstants
    {
        XMFLOAT4X4 shadowMatrix;
    };

    struct LightingConstants
    {
        XMFLOAT4X4 shadowMatrix[c_maxCascadeCount];
        XMFLOAT4X4 invViewProj;
        XMFLOAT4X4 invViewProjUnjittered;
        XMFLOAT4 cameraPos;
        XMFLOAT4 lightDir;
        XMFLOAT4 screenSize;
        float cascadeDepths[c_maxCascadeCount];
        XMFLOAT4 shadowSettings;
        float skyBlur;
        float iblMipCount;
        uint32 cascadeCount;
        uint32 lightFlags;
        uint32 debugFlags;
        uint32 hdrMode;
        float sheenTint;
        uint32 frameCount;
    };

    struct TAAConstants
    {
        XMFLOAT4 screenSize;
    };

    struct LightCullConstants
    {
        XMFLOAT4X4 invViewProjMtx;
        XMFLOAT4X4 viewMtx;
        XMFLOAT4 screenSize;
        XMFLOAT4 tileSize;
        XMFLOAT3 cameraPos;
        float padding;
    };

    struct PostFXConstants
    {
        XMFLOAT4 screenSize;
        float maxNits;
        int32 sRGB;
        int32 tonemap;
    };
    
    struct ExposureConstants
    {
        XMFLOAT4 screenSize;
        float minLogLum;
        float invLogLumrange;
        float deltaTime;
        float tau;
        bool hdr;
    };

    enum Tonemapper
    {
        ACES = 0,
        GT7
    };
    
    struct TonemapConstants
    {
        XMFLOAT4 screenSize;
        float minNits;
        float maxNits;
        float paperWhiteNits;
        int32 sRGB;
        int32 tonemap;
        Tonemapper tonemapper;
    };

    struct IBLConstants
    {
        XMFLOAT4 screenSize;
        uint32 sampleCount;
        float roughness;
        float mipCount;
    };

    struct ScreenSpaceShadowsConstants
    {
        XMFLOAT4 lightPos;
        XMFLOAT4 screenSize;
        int32 waveOffset[2];
        float SurfaceThickness;
        float BilinearThreshold;
        float ShadowContrast;
    };

    struct HairConstants
    {
        XMFLOAT4X4 invView;
        XMFLOAT4X4 invProj;
        XMFLOAT4X4 worldViewProj;
        XMFLOAT4 cameraPos;
        XMFLOAT4 screenSize;
        uint32 standCount;
    };

    struct GTAOConstants {
        XMFLOAT4X4 proj;
        XMFLOAT4X4 invProj;
        XMFLOAT4X4 view;
        XMFLOAT4 screenSize;
        XMFLOAT4 viewerPos;
        uint32 frameCount;
        float intensity;      // AO Darkening Power (approx 0.5 - 5.0)
        float radius;         // World Space Radius (approx 0.5 - 10.0)
        float minRadius;      // Pixel Space Clamp (usually 1.0)
        uint32 sliceCount;  // Quality: 1 to 8
        uint32 stepCount;   // Quality: 2 to 16
        float falloff;
        float padding;
    };
    
    const uint32 VertexConstantsAlignedSize = (sizeof(VertexConstants) + 255) & ~255;
    const uint32 PixelConstantsAlignedSize = (sizeof(PixelConstants) + 255) & ~255;
    const uint32 LightingConstantsAlignedSize = (sizeof(LightingConstants) + 255) & ~255;
    const uint32 ShadowConstantsAlignedSize = (sizeof(ShadowConstants) + 255) & ~255;


    const uint32 TAAConstantsAlignedSize = (sizeof(TAAConstants) + 255) & ~255;
    const uint32 LightCullConstantsAlignedSize = (sizeof(LightCullConstants) + 255) & ~255;

    const uint32 IBLConstantsAlignedSize = (sizeof(IBLConstants) + 255) & ~255;

    const uint32 ScreenSpaceShadowsConstantsAlignedSize = (sizeof(ScreenSpaceShadowsConstants) + 255) & ~255;

    const uint32 PostFXConstantsAlignedSize = (sizeof(PostFXConstants) + 255) & ~255;
    const uint32 ExposureConstantsAlignedSize = (sizeof(ExposureConstants) + 255) & ~255;
    const uint32 TonemapConstantsAlignedSize = (sizeof(TonemapConstants) + 255) & ~255;

    const uint32 HairConstantsAlignedSize = (sizeof(HairConstants) + 255) & ~255;

    const uint32 MAX_SUB_RESOURCE = 48;
};
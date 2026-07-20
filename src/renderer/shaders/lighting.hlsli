#if !defined( LIGHTING_H )
#define LIGHTING_H

#define NUM_MAX_LIGHTS 256
#define LIGHT_TILE_SIZE 16

#define MIN_LIGHT_INTENSITY 33.3334f        // 1/0.03
#define MAGIC_M             31.7542648054f  // 5.5 / sqrt(0.03)

// Smallest value for light we'll allow
#define LIGHT_EPSILON      1.0e-6

struct GpuFrustum
{
    float4 planes[6];
};

struct GPULight
{
    float4 positionIntensity;
    float3 color;
    uint missIdx;
};

struct LightTileData
{
    uint lightCount;
    uint lights[NUM_MAX_LIGHTS];
    float2 depths;
};

struct BoundingBox
{
    float3 min;
    float3 max;
};

float GetLightRadiusSquared(float intensity)
{
    return intensity * MIN_LIGHT_INTENSITY;
}

bool AABBIntersect(float3 min, float3 max, BoundingBox aabb)
{
    return !(max.x < aabb.min.x || min.x > aabb.max.x ||
             max.y < aabb.min.y || min.y > aabb.max.y ||
             max.z < aabb.min.z || min.z > aabb.max.z);
}

bool SphereIntersect(float3 center, float intensity, BoundingBox aabb)
{
    float3 closestPointInAABB = clamp(center, aabb.min.xyz, aabb.max.xyz);
    float3 v = (closestPointInAABB - center);
    float d = dot(v, v);
    
    // But if I'm taking the square root of something, and then squaring it...
    // why bother with the square root?
    //float r = sqrt(intensity * MIN_LIGHT_INTENSITY);
    //r *= r;
    float r = GetLightRadiusSquared(intensity);
    return d <= r;
}

bool FrustumIntersect(float3 min, float3 max, GpuFrustum frustum, float4x4 viewMtx)
{
    [unroll]
    for (uint i = 0; i < 6; ++i)
    {
        float4 p = frustum.planes[i];
        float3 t = select(p.xyz < 0, min, max);
        float dist = dot(p, float4(t, 1));
        if (dist < 0)
            return false;
    }
    return true;
}

bool SphereIntersect(float3 center, float intensity, GpuFrustum frustum, float4x4 viewMtx)
{
    float r = sqrt(GetLightRadiusSquared(intensity));
    
    [unroll]
    for (uint i = 0; i < 6; ++i)
    {
        float4 p = frustum.planes[i]; 
        float dist = dot(p, float4(center, 1.0f));
        if (dist < -r)
            return false;
    }
    return true;
}

// https://www.desmos.com/calculator/pn08vdthxa
float CalculatePointLightIntensity(float3 lightVector, float lightIntensity)
{
    float d = dot(lightVector, lightVector);
    float r = GetLightRadiusSquared(lightIntensity);

    float F = lightIntensity * MAGIC_M;
    
    float s = d / r;
    float n = 1 - s;
    return (d <= r) * lightIntensity * (n * n) / (F * s + 0.1);
}

#endif // !defined( LIGHTING_H )
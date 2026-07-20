#include "Compute_Header.hlsli"
#include "normal_util.hlsli"

cbuffer CB_GTAO : register(b0)
{
    row_major float4x4 g_InvView;
    row_major float4x4 g_InvProj;
    row_major float4x4 g_View;      
    float4 g_ScreenSize; 
    float4 g_ViewerPos;  
    uint   g_FrameCount;
    float  g_Intensity;
    float  g_Radius;   
    float  g_MinRadius; 
    uint   g_SliceCount;
    uint   g_StepCount;  
    float  g_Thickness;
    float  g_Padding;
};

Texture2D<float>  g_TextureDepth  : register(t0);
Texture2D<float4> g_TextureNormal : register(t1);

RWTexture2D<float4> g_Output : register(u0);

#define PI 3.14159265359
#define HALF_PI 1.57079632679
#define EPSILON 1e-5

float3 GetViewPos(float2 uv, float depth)
{
    float4 clipPos = float4(uv * 2.0 - 1.0, depth, 1.0);
    clipPos.y = -clipPos.y; 
    float4 viewPos = mul(clipPos, g_InvProj);
    return viewPos.xyz / viewPos.w;
}

float InterleavedGradientNoise(float2 pos)
{
    float3 magic = float3(0.06711056f, 0.00583715f, 52.9829189f);
    return frac(magic.z * frac(dot(pos, magic.xy)));
}

float IntegrateArc(float h, float n)
{
    float i = cos(n) + 2.0 * h * sin(n) - cos(2.0 * h - n);
    return 0.25 * i;
}

float3 IntegrateArcDirection(float h1, float h2, float n, float3 sliceDir, float3 viewDir, float3 normal)
{
    float3 planeNormal = normalize(cross(sliceDir, viewDir));
    float3 tangent = cross(normal, planeNormal);
    float3 nx = normal * cos(n) + tangent * sin(n);
    
    float a = 0.5 * (-cos(2.0 * h1 - n) + cos(n) + 2.0 * h1 * sin(n));
    float b = 0.5 * (-cos(2.0 * h2 - n) + cos(n) + 2.0 * h2 * sin(n));
    
    float bentAngle = (h1 + h2) * 0.5;
    float3 bentDir = normal * cos(bentAngle) + tangent * sin(bentAngle);
    
    return bentDir * (a + b);
}

COMPUTE_MAIN
{
    uint2 pixelPos = IN.DispatchThreadID.xy;
    if (pixelPos.x >= g_ScreenSize.x || pixelPos.y >= g_ScreenSize.y) return;

    float2 uv = (pixelPos + 0.5) * g_ScreenSize.zw;
    float depth = g_TextureDepth.Load(int3(pixelPos, 0)).r;

    if (depth <= EPSILON || depth >= 1.0f) 
    {
        g_Output[pixelPos] = float4(0, 0, 1, 1);
        return;
    }

    float3 P = GetViewPos(uv, depth);
    float3 V = normalize(-P);
    
    float4 normRaw = g_TextureNormal.Load(int3(pixelPos, 0));
    float3 N_World = DecodeNormal(normRaw.xy);
    float3 vN = normalize(mul(float4(N_World, 0), g_View).xyz);
    
    float biasAmount = g_Radius * 0.05f;
    P += vN * biasAmount;
    
    float viewRadius = g_Radius;
    float projectedRadius = viewRadius / abs(P.z);
    float stepRadius = projectedRadius * min(g_ScreenSize.x, g_ScreenSize.y);
    
    float radiusFade = saturate(stepRadius - 1.0f);
    
    float maxRadius = min(g_ScreenSize.x, g_ScreenSize.y) * 0.15;
    stepRadius = clamp(stepRadius, g_MinRadius, maxRadius);
    
    float2 noiseCoord = pixelPos + float2(g_FrameCount % 4, g_FrameCount / 4 % 4);
    float noise = InterleavedGradientNoise(noiseCoord);
    float temporalBase = (float)(g_FrameCount % 64) * 0.61803398875; 
    
    float spatialDir = frac(noise + temporalBase);
    float spatialStep = frac(noise * 0.7152 + temporalBase * 1.3247);
    
    float visibility = 0.0f;
    float weightSum = 0.0f;
    float3 bentNormal = float3(0, 0, 0);

    [loop]
    for (uint i = 0; i < g_SliceCount; ++i)
    {
        float phi = (float(i) + spatialDir) * (PI / float(g_SliceCount));
        float2 dir = float2(cos(phi), sin(phi));
        float3 sliceDir = float3(dir, 0);

        float3 planeNormalVec = cross(sliceDir, V);
        float3 planeNormal = normalize(planeNormalVec);
        float3 projN = vN - planeNormal * dot(vN, planeNormal);
        float projLen = length(projN);

        float cos_n = clamp(dot(projN, V) / projLen, -1.f, 1.f);
        float3 orthoDir = projN - dot(projN, V) * V;
        float signN = sign(dot(orthoDir, projN));
        float n = signN * acos(cos_n);
        // n -= 0.1f; // ~5.7 degrees bias
        float h1 = -1.0;
        float h2 = -1.0;
        
        float2 oDir = float2(dir.x, -dir.y);

        [loop]
        for (uint j = 0; j < g_StepCount; ++j)
        {
            float stepIndex = float(j) + spatialStep;
            float t = (stepIndex + 0.5) / float(g_StepCount);
            float stepScale = t * t;
            float2 uvOffset = oDir * stepScale * stepRadius * g_ScreenSize.zw;
            
            {
                float2 uv1 = uv + uvOffset;
                
                if (uv1.x < 0.0 || uv1.x > 1.0 || uv1.y < 0.0 || uv1.y > 1.0)
                {
                    h1 = max(h1, -1.0);
                }
                else
                {
                    float d1 = g_TextureDepth.Sample(pointClampSampler, uv1).r;
                    if (d1 > EPSILON)
                    {
                        float3 P1 = GetViewPos(uv1, d1);
                        float3 delta = P1 - P;
                        float dist = length(delta);
                        
                        if (dist > EPSILON)
                        {
                            float3 horizonDir = delta / dist;
                            float horizonCos = dot(horizonDir, V);
                            
                            float attenuation = 1.0 - smoothstep(g_Radius * 0.5, g_Radius, dist);
                            
                            float planeDist = dot(delta, vN);
                            if (planeDist < -EPSILON) 
                            {
                                float thicknessWeight = 1.0 - saturate(abs(planeDist) / g_Thickness);
                                attenuation *= thicknessWeight;
                            }
                            
                            float effectiveHorizon = lerp(-1.0, horizonCos, attenuation);
                            h1 = max(h1, effectiveHorizon);
                        }
                    }
                }
            }
            
            {
                float2 uv2 = uv - uvOffset;
                
                if (uv2.x < 0.0 || uv2.x > 1.0 || uv2.y < 0.0 || uv2.y > 1.0)
                {
                    h2 = max(h2, -1.0);
                }
                else
                {
                    float d2 = g_TextureDepth.Sample(pointClampSampler, uv2).r;
                    if (d2 > EPSILON )
                    {
                        float3 P2 = GetViewPos(uv2, d2);
                        float3 delta = P2 - P;
                        float dist = length(delta);
                        
                        if (dist > EPSILON)
                        {
                            float3 horizonDir = delta / dist;
                            float horizonCos = dot(horizonDir, V);
                            
                            float attenuation = 1.0 - smoothstep(g_Radius * 0.5, g_Radius, dist);
                            
                            float planeDist = dot(delta, vN);
                            if (planeDist < -EPSILON) 
                            {
                                float thicknessWeight = 1.0 - saturate(abs(planeDist) / g_Thickness);
                                attenuation *= thicknessWeight;
                            }
                            
                            float effectiveHorizon = lerp(-1.0, horizonCos, attenuation);
                            h2 = max(h2, effectiveHorizon);
                        }
                    }
                }
            }
        }
        
        h1 = (h1 > -1.0) ? acos(saturate(h1)) : HALF_PI;
        h2 = (h2 > -1.0) ? acos(saturate(h2)) : HALF_PI;
        
        float ih1 = n + clamp(h1 - n, -HALF_PI, HALF_PI);
        float ih2 = n + clamp(h2 - n, -HALF_PI, HALF_PI);
        
        float sliceOcclusion = IntegrateArc(ih1, n) + IntegrateArc(ih2, n);
        ih2 = n + clamp(-h2 - n, -HALF_PI, HALF_PI);
        float3 sliceBent = IntegrateArcDirection(ih1, ih2, n, sliceDir, V, vN);
        
        visibility += projLen * sliceOcclusion;
        bentNormal += projLen * sliceBent;
        weightSum += projLen;
    }
    
    if (weightSum > EPSILON)
    {
        visibility /= weightSum;
        bentNormal /= weightSum;
    }
    
    visibility = saturate(visibility);
    visibility = lerp(1.0f, visibility, radiusFade);
    visibility = pow(visibility, g_Intensity);
    
    bentNormal = normalize(bentNormal + vN * EPSILON);
    
    float3 bentNormal_World = mul(float4(bentNormal, 0), g_InvView).xyz;
    bentNormal_World = normalize(bentNormal_World);
    
    float2 encodedBent = EncodeNormal(bentNormal_World);
    
    g_Output[pixelPos] = float4(encodedBent, visibility, 1);
}
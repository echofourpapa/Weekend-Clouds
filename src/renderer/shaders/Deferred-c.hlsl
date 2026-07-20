#include "pbr.hlsli"
#include "Compute_Header.hlsli"
#include "color_util.hlsli"
#include "normal_util.hlsli"
#include "lighting.hlsli"

#define DISPLAY_SRGB 0
#define DISPLAY_HDR 1

#define PI 3.14159265359
#define HALF_PI 1.57079632679

cbuffer ConstantBuffer : register(b0)
{
    row_major float4x4 shadowMatrix[4];
    row_major float4x4 invViewProj;
    row_major float4x4 invViewProjUnjittered; 
    
    float4 camPos;
    float4 lightDir;
    float4 screenSize;
    float4 cascadeDepths;
    float4 shadowSettings;
    float skyBlur;
    float iblMipCount;
    uint cascadeCount;
    uint lightingFlags;
    uint debugFlags;
    uint hdrMode;
    float sheenTint;
    uint frameCount;
};

enum class DebugFlags
{
    Lit = 0,
    Albedo = 1,
    Normal = 2,
    Metal = 3,
    Roughness = 4,
    Motion = 5,
    SSShadows = 6,
    Shadow_MUL = 7,
    Depth = 8,
    LightTiles = 9,
    TileBounds = 10,
    GTAODebug = 11,
    HBILDebug = 12
};

enum LightingFlags
{
    DirectDiffuse = 1 << 0,
    DirectSpecular = 1 << 1,
    IndirectDiffuse = 1 << 2,
    IndirectSpecular = 1 << 3,
    DirectionalShadows = 1 << 4,
    SSShadows = 1 << 5,
    GTAO = 1 << 6,
    HBIL = 1 << 7,
    CascadeDebug = 1 << 8,
    Sheen = 1 << 9
};

Texture2D<float3> GBuffer0 : register(t0);
Texture2D<float4> GBuffer1 : register(t1);
Texture2D<float4> GBuffer2 : register(t2);
Texture2D<float> DepthTex : register(t3);

Texture2DArray<float> DirectShadowsTex : register(t4);
Texture2D<float> SSShadowsTex : register(t5);
TextureCube<float3> SpecIBL : register(t6);
TextureCube<float3> DiffIBL : register(t7);
Texture2D<float3> splitSumTex : register(t8);

StructuredBuffer<GPULight> lightBVH : register(t9);
StructuredBuffer<LightTileData> lightTiles : register(t10);
Texture2D<float4> GTAOTex : register(t11);

RWTexture2D<float4> ColorOut : register(u0);

float3 WorldPosFromDepth(float depth, float2 TexCoord, float4x4 invVP)
{
    float3 csPos = float3(TexCoord * 2.0 - 1.0, depth);
    csPos.y = -csPos.y;
    float4 hPos = mul(float4(csPos, 1.0), invVP);
    return hPos.xyz / hPos.w;
}

struct ShadowSettings
{
    float softAmount;
    float bias;
    float cascadeBlend;
    float mapSize;
};

float InterleavedGradientNoise(float2 pixelPos, uint frameCount)
{
    pixelPos += (float)frameCount * 5.588238f;
    return frac(52.9829189f * frac(0.06711056f * pixelPos.x + 0.00583715f * pixelPos.y));
}

float PCFSampleShadow(float cascade, float2 shadowUV, ShadowSettings settings, float testDepth, float2 screenPos)
{
    if (any(shadowUV < 0.0) || any(shadowUV > 1.0))
        return 1.0f;

    float noise = InterleavedGradientNoise(screenPos, frameCount) * 2.0f * PI;
    float2 noiseOffset = float2(noise, frac(noise * 1.61803398875f)) - 0.5f;

    float shadow = 0.0;
    [unroll]
    for (float x = -1.5f; x <= 1.5f; x += 1.0f)
    {
        [unroll]
        for (float y = -1.5f; y <= 1.5f; y += 1.0f)
        {
            float2 offset = (float2(x, y) + noiseOffset) * settings.mapSize * settings.softAmount;
            float shadowDepth = DirectShadowsTex.SampleLevel(pointClampSampler, float3(shadowUV + offset, cascade), 0) - settings.bias;
            shadow += (testDepth > shadowDepth);
        }
    }
    return shadow / 16.0f;
}

float3 GetShadowUV(float3 worldPos, int cascade)
{
    float4 shadowWPos = mul(float4(worldPos, 1), shadowMatrix[cascade]);
    shadowWPos /= shadowWPos.w;
    shadowWPos.y = -shadowWPos.y;
    return float3(shadowWPos.xy * 0.5 + 0.5, shadowWPos.z);
}

void GetDirectShadow(float3 worldPos, float2 screenPos, float depth, ShadowSettings settings, out float shadow, out float cascade)
{
    float4 comp = (depth < cascadeDepths);
    cascade = dot(float4(1, 1, 1, 1), comp);
    cascade = min(cascade, cascadeCount - 1);

    float3 shadowUV = GetShadowUV(worldPos, (int)cascade);

    float cascadeRatio = 1.0 - (cascade / (float)(cascadeCount - 1));
    settings.softAmount *= cascadeRatio;
    settings.bias *= (1.0f + settings.softAmount);
    
    shadow = PCFSampleShadow(cascade, shadowUV.xy, settings, shadowUV.z, screenPos);

    float blendRange = cascadeDepths[cascade] * settings.cascadeBlend;
    if (depth < blendRange && (cascade + 1) < cascadeCount)
    {
        float3 otherUV = GetShadowUV(worldPos, (int)cascade + 1);
        float otherShadow = PCFSampleShadow(cascade + 1, otherUV.xy, settings, otherUV.z, screenPos);
        float t = saturate((depth + blendRange) / (cascadeDepths[cascade] + blendRange));
        shadow = lerp(shadow, otherShadow, t);
    }
}

#define BLOCKER_SEARCH_SAMPLES 32
#define PCF_SAMPLES 32
#define LIGHT_WORLD_SIZE 15.0f  // Tune this for penumbra softness, 2.0 is direct sun

float2 VogelDiskSample(int sampleIndex, int samplesCount, float phi)
{
    float goldenAngle = 2.4f;
    float r = sqrt(sampleIndex + 0.5f) / sqrt(samplesCount);
    float theta = sampleIndex * goldenAngle + phi;
    return r * float2(cos(theta), sin(theta));
}

float FindBlockerDepth(float cascade, float2 shadowUV, ShadowSettings settings, float receiverDepth, float searchRadius, float noise)
{
    float blockerDepthSum = 0.0f;
    int numBlockers = 0;
    
    [unroll]
    for (int i = 0; i < BLOCKER_SEARCH_SAMPLES; i++)
    {
        float2 offset = VogelDiskSample(i, BLOCKER_SEARCH_SAMPLES, noise * 6.28318f) * searchRadius;
        float3 sampleUV = float3(shadowUV + offset, cascade);
        
        float shadowDepth = DirectShadowsTex.SampleLevel(pointClampSampler, sampleUV, 0);
        
        if (shadowDepth < receiverDepth - settings.bias)
        {
            blockerDepthSum += shadowDepth;
            numBlockers++;
        }
    }
    
    return (numBlockers > 0) ? (blockerDepthSum / float(numBlockers)) : -1.0f;
}

float PenumbraRadius(float receiverDepth, float blockerDepth, float lightSize)
{
    float depthDiff = receiverDepth - blockerDepth;
    return (depthDiff / (1.0f - blockerDepth)) * lightSize;
}

float PCFFilter(float cascade, float2 shadowUV, ShadowSettings settings, float receiverDepth, float filterRadius, float noise)
{
    float shadow = 0.0f;
    
    [unroll]
    for (int i = 0; i < PCF_SAMPLES; i++)
    {
        float2 offset = VogelDiskSample(i, PCF_SAMPLES, noise * 6.28318f) * filterRadius;
        float3 sampleUV = float3(shadowUV + offset, cascade);
        
        float shadowDepth = DirectShadowsTex.SampleLevel(pointClampSampler, sampleUV, 0);
        
        shadow += (receiverDepth < shadowDepth + settings.bias) ? 1.0f : 0.0f;
    }
    
    return shadow / float(PCF_SAMPLES);
}

float PCSShadow(float cascade, float2 shadowUV, ShadowSettings settings, float receiverDepth, float2 screenPos, uint frameIndex)
{
    if (any(shadowUV < 0.0) || any(shadowUV > 1.0))
        return 1.0f;
    
    float noise = InterleavedGradientNoise(screenPos, frameIndex);
    
    float searchRadius = LIGHT_WORLD_SIZE * settings.mapSize * settings.softAmount;
    float avgBlockerDepth = FindBlockerDepth(cascade, shadowUV, settings, receiverDepth, searchRadius, noise);
    
    if (avgBlockerDepth < 0.0f)
    {
        // No blockers in search, do simple shadow test
        float shadowDepth = DirectShadowsTex.SampleLevel(pointClampSampler, float3(shadowUV, cascade), 0);
        return (receiverDepth < shadowDepth + settings.bias) ? 1.0f : 0.0f;
    }
    
    float penumbraRadius = PenumbraRadius(receiverDepth, avgBlockerDepth, LIGHT_WORLD_SIZE);
    float filterRadius = penumbraRadius * settings.mapSize * settings.softAmount;
    
    filterRadius = clamp(filterRadius, settings.mapSize * 0.5f, settings.mapSize * 4.0f);
    
    return PCFFilter(cascade, shadowUV, settings, receiverDepth, filterRadius, noise);
}

void GetDirectShadowPCSS(float3 worldPos, float depth, ShadowSettings settings, out float shadow, out float cascade, float2 screenPos, uint frameIndex)
{
    float4 comp = (depth < cascadeDepths);
    cascade = dot(float4(1, 1, 1, 1), comp);
    cascade = min(cascade, cascadeCount - 1);

    float3 shadowUV = GetShadowUV(worldPos, (int)cascade);
    
    float cascadeRatio = 1.0 - (cascade / float(cascadeCount - 1));
    ShadowSettings adjustedSettings = settings;
    adjustedSettings.softAmount *= cascadeRatio;
    adjustedSettings.bias = settings.bias * pow(2.0f, cascade);
    shadow = PCSShadow(cascade, shadowUV.xy, adjustedSettings, shadowUV.z, screenPos, frameIndex);
    
    float blendRange = cascadeDepths[cascade] * settings.cascadeBlend;
    if (depth < blendRange && (cascade + 1) < cascadeCount)
    {
        float3 otherUV = GetShadowUV(worldPos, (int)cascade + 1);
        
        float nextCascadeRatio = 1.0 - ((cascade + 1) / float(cascadeCount - 1));
        adjustedSettings.softAmount = settings.softAmount * nextCascadeRatio;
        adjustedSettings.bias = settings.bias * pow(2.0f, cascade);
        float otherShadow = PCSShadow(cascade + 1, otherUV.xy, adjustedSettings, otherUV.z, screenPos, frameIndex);
        
        float t = saturate((depth + blendRange) / (cascadeDepths[cascade] + blendRange));
        shadow = lerp(shadow, otherShadow, t);
    }
}

float3 MultiBounceAO(float ao, float3 albedo)
{
    float3 a = 2.0 * albedo - 0.33;
    float3 b = -4.8 * albedo + 0.64;
    float3 c = 2.53 * albedo + 0.69;
    
    float x = ao;
    return max(x, ((x * a + b) * x + c) * x);
}

float SpecularOcclusionFromAO(float ao, float3 bentNormal, float3 reflectDir, float roughness)
{
    float horizon = sqrt(1.0 - ao);
    float horizonAngle = acos(horizon);
    float aperture = max(0.0, cos(horizonAngle + HALF_PI * roughness) - horizon);
    float baseSpecOcclusion = saturate(ao + aperture);
    
    float bentDot = saturate(dot(reflectDir, bentNormal));
    float directionInfluence = 1.0 - roughness;
    float directionalTerm = lerp(1.0, bentDot, directionInfluence);
    
    return baseSpecOcclusion * directionalTerm;
}

COMPUTE_MAIN
{
    if (IN.DispatchThreadID.x >= uint(screenSize.z) || IN.DispatchThreadID.y >= uint(screenSize.w))
    {
        return;
    }

    float2 TexelSize = screenSize.xy;
    float2 UV = TexelSize * (IN.DispatchThreadID.xy + 0.5f);
    float depth = DepthTex.SampleLevel(pointClampSampler, UV, 0);

    float3 nearWorldPos = WorldPosFromDepth(1.0f, UV, invViewProj);
    float3 viewDir = normalize(nearWorldPos - camPos.xyz);

    DebugFlags dFlags = (DebugFlags)debugFlags;
    
    float skyIntensity = lightDir.w * 0.2;
    float3 sky = 0.f;
    if (dFlags == DebugFlags::Lit)
    {
        sky = SpecIBL.SampleLevel(iblSampler, viewDir, skyBlur * iblMipCount).rgb * skyIntensity;
        if (hdrMode == DISPLAY_HDR)
            sky = Rec709ToRec2020(sky);
    }
    if (depth <= 1e-6f)
    {
        ColorOut[IN.DispatchThreadID.xy] = float4(sky, 1.0f);
        return;
    }

    float3 worldPos = WorldPosFromDepth(depth, UV, invViewProj);
    float3 albedo = GBuffer0.SampleLevel(linearClampSampler, UV, 0);
    float4 norm = GBuffer1.SampleLevel(linearClampSampler, UV, 0);
    float4 mr = GBuffer2.SampleLevel(linearClampSampler, UV, 0);
    float3 N = normalize(DecodeNormal(norm.xy));

    if (hdrMode == DISPLAY_HDR)
        albedo = Rec709ToRec2020(albedo);

    LightingFlags lFlags = (LightingFlags)lightingFlags;

    float directShadows = 1.0f;
    float ssShadows = 1.0f;
    float3 aoVisibility = 1.0f;
    float specVisibility = 1.0f;
    float3 bentNormal = N;
    float cascade = 0.0f;
    float3 indirectBounce = 0.0f;

    if (lFlags & LightingFlags::SSShadows)
    {
        ssShadows = SSShadowsTex.SampleLevel(linearClampSampler, UV, 0);
    }

    if (lFlags & LightingFlags::GTAO)
    {
        float4 gtaoData = GTAOTex.SampleLevel(linearClampSampler, UV, 0);
        bentNormal = normalize(DecodeNormal(gtaoData.rg));
        aoVisibility = gtaoData.bbb;
        specVisibility = gtaoData.b;
    }
    
    float3 finalColor = float3(0, 0, 0);

    BDRFInput brdfInput;
    brdfInput.N = N;
    brdfInput.V = -viewDir;
    brdfInput.L = normalize(lightDir.xyz);
    brdfInput.albedo = albedo;
    brdfInput.metallic = mr.x;
    brdfInput.roughness = max(mr.y * mr.y, 1e-5);
    brdfInput.specular = 0.5f;
    brdfInput.subsurface = mr.z;
    brdfInput.sheen = lFlags & LightingFlags::Sheen ? mr.w : 0.f;
    brdfInput.sheenTint = sheenTint;
    brdfInput.sheenRoughness = max(mr.y, 1e-5);;

    BRDFOutput brdfOutput = BRDF(brdfInput);

    if (lFlags & LightingFlags::DirectionalShadows)
    {
        ShadowSettings settings;
        settings.bias = shadowSettings.x;
        settings.softAmount = shadowSettings.y;
        settings.cascadeBlend = shadowSettings.z;
        settings.mapSize = shadowSettings.w;

        GetDirectShadowPCSS(worldPos, depth, settings, directShadows, cascade, IN.DispatchThreadID.xy, frameCount);
    }
    
    if (lFlags & LightingFlags::CascadeDebug)
    {
        float hue = cascade / (float)cascadeCount;
        float blendRange = cascadeDepths[cascade] * shadowSettings.z;
        if (depth < blendRange && (cascade + 1) < cascadeCount)
        {
            float t = saturate((depth + blendRange) / (cascadeDepths[cascade] + blendRange));
            hue = lerp(hue, (cascade + 1) / (float)cascadeCount, t);
        }
        albedo = hsv2rgb(float3(hue, 1, 1)) * directShadows;
    }
    
    float3 indirectDiff = 0.0f;
    float3 indirectSpec = 0.0f;
    float3 sheenScaling = 1.0f; 
    
    if ((lFlags & LightingFlags::IndirectDiffuse) || (lFlags & LightingFlags::IndirectSpecular))
    {
        float2 brdfUV = saturate(float2(brdfOutput.NoV, brdfInput.roughness));
        float3 brdfLUT = splitSumTex.SampleLevel(linearClampSampler, brdfUV, 0).xyz;
        
        // Multi-scattering
        float3 Ess = brdfOutput.f0 * brdfLUT.x + brdfOutput.f90 * brdfLUT.y;
        float3 F_avg = brdfOutput.f0 + (brdfOutput.f90 - brdfOutput.f0) / 21.0;
        float E_avg = FdezAguera_Eavg(brdfInput.roughness);
        float Ess_Scalar = saturate(brdfLUT.x + brdfLUT.y);
        float3 Ems = (1.0 - Ess_Scalar) * (F_avg * E_avg) / (1.0 - F_avg * (1.0 - E_avg));
        float3 kS = Ess + Ems;
        
        float3 R = reflect(viewDir, N);
        
        if (lFlags & LightingFlags::IndirectSpecular)
        {
            float mip = brdfInput.roughness * iblMipCount;
            float3 env = SpecIBL.SampleLevel(iblSampler, R, mip).rgb;
            if (hdrMode == DISPLAY_HDR)
                env = Rec709ToRec2020(env);
            specVisibility = SpecularOcclusionFromAO(specVisibility, bentNormal, R, brdfInput.roughness);

            indirectSpec = env * skyIntensity * kS * specVisibility;
        }
        
        float3 indirectSheen = 0.0f;

        if (brdfInput.sheen > 0.0f && lFlags & LightingFlags::Sheen)
        {
            float sheenDFG = brdfLUT.z;
            
            float3 Csheen = brdfOutput.SheenColor;
            float3 Fsheen = Csheen * brdfInput.sheen * sheenDFG;
                        
            float sheenMip = max(brdfInput.sheenRoughness, 0.15f) * iblMipCount;
            float3 sheenEnv = SpecIBL.SampleLevel(iblSampler, R, sheenMip).rgb;
            if (hdrMode == DISPLAY_HDR) 
                sheenEnv = Rec709ToRec2020(sheenEnv);
            
            indirectSheen = sheenEnv * skyIntensity * Fsheen * specVisibility;
            
            sheenScaling = 1.f - Fsheen;
            
            if (lFlags & LightingFlags::IndirectSpecular)
            {
                indirectSpec *= sheenScaling;
                indirectSpec += indirectSheen;
            }
        }
                
        if (lFlags & LightingFlags::IndirectDiffuse)
        {
            float3 sh = DiffIBL.SampleLevel(linearWrapSampler, bentNormal, 0).rgb;
            if (hdrMode == DISPLAY_HDR)
                sh = Rec709ToRec2020(sh);

            float3 lambertAlbedo = (1.0 - brdfInput.metallic) * brdfInput.albedo;
            aoVisibility = MultiBounceAO(aoVisibility.r, lambertAlbedo);
            float3 kD = 1.f - kS;
            indirectDiff = sh * skyIntensity * lambertAlbedo * aoVisibility * V_INV_PI * kD;

            if (lFlags & LightingFlags::HBIL)
            {
                indirectDiff += indirectBounce * lambertAlbedo * kD;
            }
            indirectDiff *= sheenScaling;
        }
        
    }

    float3 directDiff = 0.0f;
    float3 directSpec = 0.0f;

    float shadows = min(directShadows, ssShadows);
    float3 sunColor = lightDir.www * brdfOutput.NoL * shadows;

    if (lFlags & LightingFlags::DirectDiffuse)
        directDiff += brdfOutput.Fd * sunColor;
    if (lFlags & LightingFlags::DirectSpecular)
        directSpec += brdfOutput.Fs * sunColor;

    float3 lightDiff = 0.0f;
    float3 lightSpec = 0.0f;

    uint tileIdx = (IN.DispatchThreadID.x / 16) + (IN.DispatchThreadID.y / 16) * uint(screenSize.z / 16);
    LightTileData tileData = lightTiles[tileIdx];

    if (tileData.lightCount > 0)
    {
        for (uint l = 0; l < tileData.lightCount; l++)
        {
            uint lightIdx = tileData.lights[l];
            GPULight lightData = lightBVH[lightIdx];
            float3 lightV = lightData.positionIntensity.xyz - worldPos;
            float atten = CalculatePointLightIntensity(lightV, lightData.positionIntensity.w);

            if (atten >= LIGHT_EPSILON)
            {
                BDRFInput lightInput = brdfInput;
                lightInput.L = normalize(lightV);

                BRDFOutput lightBRDF = BRDF(lightInput);
                float3 ptLightColor = lightData.color;
                if (hdrMode == DISPLAY_HDR)
                    ptLightColor = Rec709ToRec2020(ptLightColor);

                ptLightColor *= lightBRDF.NoL * atten;

                if (lFlags & LightingFlags::DirectDiffuse)
                    lightDiff += lightBRDF.Fd * ptLightColor;
                if (lFlags & LightingFlags::DirectSpecular)
                    lightSpec += lightBRDF.Fs * ptLightColor;
            }
        }
    }

    float3 direct = directDiff + directSpec + lightDiff + lightSpec;
    float3 indirect = indirectDiff + indirectSpec;
    finalColor = direct + indirect;

    
    switch (dFlags)
    {
    case DebugFlags::Albedo:
        finalColor = albedo;
        break;
    case DebugFlags::Normal:
        finalColor = N * 0.5f + 0.5f;
        break;
    case DebugFlags::Metal:
        finalColor = mr.xxx;
        break;
    case DebugFlags::Roughness:
        finalColor = mr.yyy;
        break;
    case DebugFlags::Motion:
        finalColor = float3(norm.zw * 0.5 + 0.5f, 0);
        break;
    case DebugFlags::SSShadows:
        finalColor = ssShadows.xxx;
        break;
    case DebugFlags::Shadow_MUL:
        finalColor = directShadows.xxx;
        break;
    case DebugFlags::Depth:
        finalColor = depth.xxx;
        break;
    case DebugFlags::GTAODebug:
        finalColor = aoVisibility;
        break;
    case DebugFlags::HBILDebug:
        finalColor = bentNormal * 0.5f + 0.5f;
        break;
    case DebugFlags::LightTiles:
    {
        float lightCount = (float)tileData.lightCount;
        const float threshold = 48.0f;
        const float maxLights = (float)NUM_MAX_LIGHTS;
        if (tileData.lightCount == 0)
        {
            finalColor = float3(0.1f, 0.1f, 0.1f);
        }
        else if (lightCount <= threshold)
        {
            float hue = (1.0f - (lightCount / threshold)) * 0.6667f;
            finalColor = hsv2rgb(float3(hue, 1.0f, 1.0f));
        }
        else
        {
            float hue = 1.0f - ((lightCount - threshold) / (maxLights - threshold) * 0.1667f);
            finalColor = hsv2rgb(float3(hue, 1.0f, 1.0f));
        }
        uint2 pixelInTile = IN.DispatchThreadID.xy % 16;
        if (pixelInTile.x == 0 || pixelInTile.y == 0)
            finalColor = 0.0f;
        break;
    }
    case DebugFlags::TileBounds:
    {
        finalColor = float3(tileData.depths.y - tileData.depths.x, tileData.depths.y, tileData.depths.x);
        break;
    }
    case DebugFlags::Lit:
    default:
        break;
    }

    if (dFlags != DebugFlags::Lit && dFlags != DebugFlags::Albedo)
    {
        finalColor = Rec709ToRec2020(sRGBToLinear(finalColor));
    }

    ColorOut[IN.DispatchThreadID.xy] = float4(finalColor, 1.0f);
}
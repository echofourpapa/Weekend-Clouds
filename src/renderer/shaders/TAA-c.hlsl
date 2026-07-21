#include "Compute_Header.hlsli"

#define V_PI 3.14159265359

Texture2D<float4> SceneColor : register(t0);
Texture2D<float4> AccumTex : register(t1);
Texture2D<float4> MotionTex : register(t2);
Texture2D<float> DepthTex : register(t4);

RWTexture2D<float4> ColorOut : register(u0);

cbuffer ConstantBuffer : register(b0)
{
	float4 screenSize;
	row_major float4x4 invViewProj;    // current, unjittered (sky reprojection)
	row_major float4x4 prevViewProj;   // previous, unjittered
};

// --- Utils ---

// [FIX] NaN/Inf Protection
// This stops the "Black Screen of Death" by forcing invalid pixels to Black (0)
// and clamping fireflies to a safe maximum (65000) for FP16 safety.
float3 Sanitize(float3 c)
{
    // Check for NaN or Infinity
    if (any(isnan(c)) || any(isinf(c)))
        return float3(0, 0, 0);
        
    // Clamp negative colors (impossible in physics, but possible in buggy shaders)
    c = max(c, 0.0f);
    
    // Clamp to FP16 max to prevent math overflows in variance calc
    return min(c, 65504.0f);
}

float3 RGBToYCoCg(float3 rgb)
{
    float y = dot(rgb, float3(0.25f, 0.50f, 0.25f));
    float co = dot(rgb, float3(0.50f, 0.00f, -0.50f));
    float cg = dot(rgb, float3(-0.25f, 0.50f, -0.25f));
    return float3(y, co, cg);
}

float3 YCoCgToRGB(float3 ycocg)
{
    float t = ycocg.x - ycocg.z;
    return float3(t + ycocg.y, ycocg.x + ycocg.z, t - ycocg.y);
}

float Luminance(float3 color)
{
    return dot(color, float3(0.25f, 0.50f, 0.25f));
}

// Bicubic Filter (Catmull-Rom)
float4 SampleTextureCatmullRom(in Texture2D<float4> tex, in SamplerState linearSampler, in float2 uv, in float2 texSize)
{
    float2 samplePos = uv * texSize;
    float2 texPos1 = floor(samplePos - 0.5f) + 0.5f;
    float2 f = samplePos - texPos1;

    float2 w0 = f * (-0.5f + f * (1.0f - 0.5f * f));
    float2 w1 = 1.0f + f * f * (-2.5f + 1.5f * f);
    float2 w2 = f * (0.5f + f * (2.0f - 1.5f * f));
    float2 w3 = f * f * (-0.5f + 0.5f * f);

    float2 w12 = w1 + w2;
    float2 offset12 = w2 / (w1 + w2);

    float2 texPos0 = texPos1 - 1;
    float2 texPos3 = texPos1 + 2;
    float2 texPos12 = texPos1 + offset12;

    texPos0 /= texSize;
    texPos3 /= texSize;
    texPos12 /= texSize;

    float4 result = 0.0f;
    result += tex.SampleLevel(linearSampler, float2(texPos12.x, texPos0.y), 0.0f) * w12.x * w0.y;
    result += tex.SampleLevel(linearSampler, float2(texPos0.x, texPos12.y), 0.0f) * w0.x * w12.y;
    result += tex.SampleLevel(linearSampler, float2(texPos12.x, texPos12.y), 0.0f) * w12.x * w12.y;
    result += tex.SampleLevel(linearSampler, float2(texPos12.x, texPos3.y), 0.0f) * w12.x * w3.y;
    result += tex.SampleLevel(linearSampler, float2(texPos3.x, texPos12.y), 0.0f) * w3.x * w12.y;

    return result;
}

// Velocity Dilation: Find the neighbor closest to the camera (Reversed-Z)
float2 GetClosestDepthUV(float2 uv)
{
    float closestDepth = 0.0f; // Far plane
    float2 bestUV = uv;
    
    closestDepth = DepthTex.SampleLevel(linearClampSampler, uv, 0);

    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            float2 uvOffset = float2(x, y) * screenSize.xy;
            float d = DepthTex.SampleLevel(linearClampSampler, uv + uvOffset, 0);
            
            // Reversed-Z: Larger value is Closer
            if (d > closestDepth) 
            {
                closestDepth = d;
                bestUV = uv + uvOffset;
            }
        }
    }
    return bestUV;
}

float3 ClipHistory(float3 history, float3 boxMin, float3 boxMax)
{
    float3 p_clip = 0.5 * (boxMax + boxMin);
    float3 e_clip = 0.5 * (boxMax - boxMin);

    float3 v_clip = history - p_clip;
    float3 v_unit = v_clip.xyz / e_clip;
    float3 a_unit = abs(v_unit);
    float ma_unit = max(a_unit.x, max(a_unit.y, a_unit.z));

    if (ma_unit > 1.0)
        return p_clip + v_clip / ma_unit;
    else
        return history;
}

COMPUTE_MAIN
{
    if (IN.DispatchThreadID.x >= uint(screenSize.z) || IN.DispatchThreadID.y >= uint(screenSize.w))
        return;

    float2 UV = screenSize.xy * (IN.DispatchThreadID.xy + 0.5f);

    // 1. Velocity Dilation
    float2 velocityUV = GetClosestDepthUV(UV);
    float2 velocity = MotionTex.SampleLevel(linearClampSampler, velocityUV, 0).zw;

    float2 prevUV = UV + velocity;

    // Sky/cloud pixels (reverse-Z far == depth 0) have no geometry motion vector;
    // reproject them by camera motion. The far-plane homogeneous point has w~0 so
    // only rotation matters (translation vanishes at infinity) — no camPos needed.
    if (DepthTex.SampleLevel(linearClampSampler, UV, 0) <= 0.0)
    {
        float2 ndc = float2(UV.x * 2.0 - 1.0, 1.0 - UV.y * 2.0);
        float4 wf = mul(float4(ndc, 0.0, 1.0), invViewProj);
        float4 pc = mul(wf, prevViewProj);
        float2 pndc = pc.xy / pc.w;
        prevUV = float2(pndc.x * 0.5 + 0.5, 0.5 - pndc.y * 0.5);
    }

    // 2. Sample History (Sanitized)
    bool isOffScreen = any(prevUV < 0.0) || any(prevUV > 1.0);
    float3 historyColor = SampleTextureCatmullRom(AccumTex, linearClampSampler, prevUV, screenSize.zw).rgb;
    historyColor = Sanitize(historyColor); // [FIX] Protect against bad history

    // 3. Neighborhood Statistics (YCoCg Variance)
    float3 m1 = 0.0f; 
    float3 m2 = 0.0f; 
    
    // [FIX] Sanitize current color immediately
    float3 currentColor = Sanitize(SceneColor.SampleLevel(linearClampSampler, UV, 0).rgb);
    
    for (int x = -1; x <= 1; x++)
    {
        for (int y = -1; y <= 1; y++)
        {
            float2 uvOffset = float2(x, y) * screenSize.xy;
            float3 c = SceneColor.SampleLevel(linearClampSampler, UV + uvOffset, 0).rgb;
            c = Sanitize(c); // [FIX] Protect against bad neighbors
            
            c = RGBToYCoCg(c);
            m1 += c;
            m2 += c * c;
        }
    }

    float3 mu = m1 / 9.0f;
    float3 sigma = sqrt(abs(m2 / 9.0f - mu * mu));

    float gamma = 1.25f; 
    float3 minColor = mu - gamma * sigma;
    float3 maxColor = mu + gamma * sigma;

    // 4. Clip History
    float3 historyYCoCg = RGBToYCoCg(historyColor);
    float3 clippedHistoryYCoCg = ClipHistory(historyYCoCg, minColor, maxColor);
    float3 clippedHistory = YCoCgToRGB(clippedHistoryYCoCg);
    
    // [FIX] Sanitize result of clipping just in case
    clippedHistory = Sanitize(clippedHistory);

    // 5. Flicker Reduction
    float lum0 = Luminance(currentColor);
    float lum1 = Luminance(clippedHistory);
    float unbiasedDiff = abs(lum0 - lum1) / (max(lum0, max(lum1, 0.2)) + 1.0);
    float blendWeight = 0.1 * (1.0 - unbiasedDiff); 
    blendWeight = clamp(blendWeight, 0.02, 0.1);

    if (isOffScreen) blendWeight = 1.0f;

    float3 result = lerp(clippedHistory, currentColor, blendWeight);
    
    // [FIX] Final protection before writing
    result = Sanitize(result);
    
    ColorOut[IN.DispatchThreadID.xy] = float4(result, 1.0f);
}
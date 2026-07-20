
#include "ACES.hlsli"
#include "color_util.hlsli"

SamplerState linearWrapSampler : register(s0);
SamplerState linearClampSampler : register(s1);
SamplerState pointWrapSampler : register(s2);
SamplerState pointClampSampler : register(s3);
SamplerState pointBorderSampler : register(s4);
SamplerState iblSampler : register(s5);

Texture2D<float4> HDRColorTex : register(t0);
Texture2D<float4> UITex : register(t1);
ByteAddressBuffer Histogram : register(t2);
Texture2D<float> ExposureTex : register(t3);

cbuffer ConstantBuffer : register(b0)
{
    float4 screenSize;
    float maxNits;
    int sRGB;
    int tonemap;
};

struct PS_INPUT
{
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
};

float4 main(PS_INPUT input) : SV_Target
{
    float2 UV = input.uv;
    
    float4 hdrColor = HDRColorTex.SampleLevel(linearClampSampler, UV, 0);
    float4 uiColor = UITex.SampleLevel(linearClampSampler, UV, 0);
    
#if 0
    float exposure = ExposureTex.Load(uint3(0, 0, 0));
    
    const float width = 512.0f;
    const float height = 256.0f;
    
    const float x_offset = 0.65;
    const float y_offset = 0.75;
    
    float max_width = (width * screenSize.x + x_offset);
    float max_height = (height * screenSize.y + y_offset);
    
    if ((UV.x >= x_offset - (screenSize.x * 2) && UV.x <= max_width + (screenSize.x * 2)) && (UV.y >= y_offset - (screenSize.y * 2) && UV.y <= max_height + (screenSize.y * 2)))
    {
        uiColor = float4(0, 0, 0, 1);

    }
    
    if ((UV.x >= x_offset && UV.x <= max_width) && (UV.y >= y_offset && UV.y <= max_height))
    {
        float totalPixels = screenSize.z * screenSize.w;
        
        int bin = (UV.x - x_offset) * screenSize.z * 0.5;
        
        bin = clamp(bin, 0, 255);
        
        float count = (float) Histogram.Load(bin * 4);
        
        float amount = count / totalPixels;
        
        float cutoff = lerp(max_height, y_offset, amount);
        
        uiColor = UV.y >= cutoff ? float4(1, 1, 1, 1) : float4(0, 0, 0, 1);
    }
    
    if (UV.x >= 0.98)
    {
        float spot = 1 - exposure;
        uiColor = (UV.y < spot + screenSize.x * 5) && (UV.y > spot - screenSize.y * 5) ? 1.0f : uiColor;
    }
#endif

    if (!sRGB)
    {
        uiColor = sRGBToLinear(uiColor);
        uiColor.rgb /= uiColor.a == 0.0 ? 1.0 : uiColor.a;
        uiColor.rgb = Rec709ToRec2020(uiColor.rgb);

        const float st2084max = 10000.0;
        const float hdrScalar = maxNits / st2084max;

        uiColor.rgb = LinearToST2084(uiColor.rgb * hdrScalar);
        uiColor.rgb *= uiColor.a;
    }
    
    float4 outColor = float4(lerp(hdrColor.rgb, uiColor.rgb , uiColor.a), 1.0f);
    
    return outColor;

}
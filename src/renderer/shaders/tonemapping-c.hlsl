#include "Compute_Header.hlsli"
#include "ACES.hlsli"
#include "color_util.hlsli"
#include "GT7_Tonemap.hlsli"

Texture2D<float4> HDRColorTex : register(t0);
Texture2D<float4> ExposureTex : register(t1); // Updated to float4 for AWB+Lum
RWTexture2D<float4> Output : register(u0);

enum Tonemap: uint
{
    ACES = 0,
    GT7 = 1
};

cbuffer ConstantBuffer : register(b0)
{
    float4 screenSize;
    float minNits;
    float maxNits;
    float paperWhiteNits;
    int sRGB;
    int tonemap;
    Tonemap tonemapper;
};

COMPUTE_MAIN
{
    uint3 cord = IN.DispatchThreadID;
    if (cord.x >= uint(screenSize.z) || cord.y >= uint(screenSize.w))
    {
        return;
    }
    
    float2 TexelSize = screenSize.xy;
    float2 UV = TexelSize * (cord.xy + 0.5f);
    float4 hdrColor = HDRColorTex.SampleLevel(linearClampSampler, UV, 0);


    float4 adaptation = ExposureTex.Load(int3(0, 0, 0));
    float avgLum = adaptation.a;
    float3 whiteBalance = adaptation.rgb;
    
    if (avgLum <= 0.0001) avgLum = 1.0;

    // https://google.github.io/filament/Filament.html
    float EV = log2(avgLum * (100.0 / 12.5));
    float exposure = rcp(pow(2.0, EV) * 1.2);
    
    float4 outColor = float4(hdrColor.rgb, 1.0);
    outColor.rgb *=  whiteBalance;
    if (sRGB)
    {   
        // --- SDR Path (Rec.709) ---
        if (tonemap)
            outColor.rgb = ACESFitted(outColor.rgb);

        outColor.rgb = LinearTosRGB(outColor.rgb * exposure);
    }
    else
    {
        // --- HDR Path (Rec.2020) ---
        float3 xyY = Rec2020ToxyY(outColor.rgb);
        xyY.z *= exposure;
        outColor.rgb = xyYToRec2020(xyY);

        float hdrScale = maxNits / ST2084MAX;
        if (tonemap)
        {
            switch (tonemapper)
            {
                case ACES:
                    {
                        outColor.rgb = ACESFilmRec2020(outColor.rgb);
                    }
                    break;
                case GT7:
                    {
                        GT7ToneMapping toneMapper;
                        toneMapper.initializeAsHDR(maxNits);
                        float3 results = 0;
                        toneMapper.blendRatio_ = 0.15f;
                        float inputScale = paperWhiteNits / REFERENCE_LUMINANCE;
                        outColor.rgb = max(outColor.rgb * inputScale, 0.00001f);
                        toneMapper.applyToneMapping(outColor.rgb, results);
                        outColor.rgb = results;
                    }
                    break;
                default:
                    // passthough
                    break;
            }
        }
        outColor.rgb = LinearToST2084(outColor.rgb * hdrScale);
    }
    
    Output[IN.DispatchThreadID.xy] = outColor;
}
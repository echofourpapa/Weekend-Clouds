
#include "Compute_Header.hlsli"
#include "ACES.hlsli"
#include "color_util.hlsli"

Texture2D<float4> HDRColorTex : register(t0);
//Texture2D<float> AveLumOutput : register(t1);
RWByteAddressBuffer Histogram : register(u0);

cbuffer ConstantBuffer : register(b0)
{
    float4 screenSize;
    float minLogLum;
    float invLogLumrange;
    float deltaTime;
    float tau;
    bool hdr;
};


// https://alextardif.com/HistogramLuminance.html

#define NUM_HISTOGRAM_BINS 256

groupshared uint HistogramShared[NUM_HISTOGRAM_BINS];

uint HDRToHistogramBin(float3 hdrColor)
{
    float luminance = hdr ? GetLuminanceRec2020(hdrColor) : GetLuminance(hdrColor);
    const float epsilon = 0.0000001;
    if (luminance < epsilon)
    {
        return 0;
    }
    
    float logLuminance = saturate((log2(luminance) - minLogLum) * invLogLumrange);
    return (uint)(logLuminance * 254.0 + 1.0);
}

[numthreads(16, 16, 1)]
void main(ComputeShaderInput IN)
{
    HistogramShared[IN.GroupIndex] = 0;
    
    GroupMemoryBarrierWithGroupSync();
    
    if (IN.DispatchThreadID.x < uint(screenSize.z) || IN.DispatchThreadID.y < uint(screenSize.w))
    {
        float3 hdrColor = HDRColorTex.Load(int3(IN.DispatchThreadID.xy, 0)).rgb;
        uint binIndex = HDRToHistogramBin(hdrColor);
        InterlockedAdd(HistogramShared[binIndex], 1);
    }
    
    GroupMemoryBarrierWithGroupSync();
    uint lum = 1;
    Histogram.InterlockedAdd(IN.GroupIndex * 4, HistogramShared[IN.GroupIndex], lum);
}
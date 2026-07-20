#include "Compute_Header.hlsli"
#include "color_util.hlsli"

Texture2D<float4> HDRColorTex : register(t0);
ByteAddressBuffer Histogram : register(t1);
RWTexture2D<float2> AveLumOutput : register(u0);
RWTexture2D<float4> AWBOutput : register(u1);

cbuffer ConstantBuffer : register(b0)
{
    float4 screenSize;
    float minLogLum;
    float invLogLumrange;
    float deltaTime;
    float tau;
};

#define NUM_HISTOGRAM_BINS 256
groupshared uint HistogramShared[NUM_HISTOGRAM_BINS];
groupshared float3 AWBColorShared[NUM_HISTOGRAM_BINS];
groupshared float AWBWeightShared[NUM_HISTOGRAM_BINS];

[numthreads(16, 16, 1)]
void main(ComputeShaderInput IN)
{
    float totalPixels = screenSize.z * screenSize.w;
    float count = (float)Histogram.Load(IN.GroupIndex * 4);
    HistogramShared[IN.GroupIndex] = count * (float) IN.GroupIndex;
    
    // --- AWB Gathering ---
    float3 accumColor = 0;
    float accumWeight = 0;
    uint2 dims;
    HDRColorTex.GetDimensions(dims.x, dims.y);
    
    [unroll]
    for(uint k=0; k<32; ++k)
    {
        uint2 offset = (uint2(IN.GroupIndex * 13 + k * 7, IN.GroupIndex * 29 + k * 101)) % dims;
        float3 c = HDRColorTex.Load(int3(offset, 0)).rgb;
        float l = GetLuminanceRec2020(c);
        
        if(l > 1.f) 
        {
            accumColor += c;
            accumWeight += 1.0f;
        }
    }
    AWBColorShared[IN.GroupIndex] = accumColor;
    AWBWeightShared[IN.GroupIndex] = accumWeight;
    
    GroupMemoryBarrierWithGroupSync();

    [unroll]
    for (uint i = (NUM_HISTOGRAM_BINS >> 1); i > 0; i >>= 1)
    {
        if (IN.GroupIndex < i)
        {
            HistogramShared[IN.GroupIndex] += HistogramShared[IN.GroupIndex + i];
            AWBColorShared[IN.GroupIndex] += AWBColorShared[IN.GroupIndex + i];
            AWBWeightShared[IN.GroupIndex] += AWBWeightShared[IN.GroupIndex + i];
        }
        GroupMemoryBarrierWithGroupSync();
    }
    
    if (IN.GroupIndex == 0)
    {
        // --- Exposure Logic ---
        float logLumRange = rcp(invLogLumrange);
        float weightedSum = (float)HistogramShared[0].x;
        float weightedLogAverage = (weightedSum / max(totalPixels - count, 1.0)) - 1.0;
        float logLumtoLum = ((weightedLogAverage / 254.0) * logLumRange) + minLogLum;
        float weightedAverageLuminance = exp2(logLumtoLum);
        
        float2 prev = max(AveLumOutput[uint2(0, 0)], float2(0.0001, 0.0001));

        float speedFast = (weightedAverageLuminance > prev.x) ? 4.0 : 1.0;
        float speedSlow = (weightedAverageLuminance > prev.y) ? 1.0 : 0.2;
        
        float fast = prev.x + (weightedAverageLuminance - prev.x) * (1.0 - exp(-deltaTime * speedFast * tau));
        float slow = prev.y + (weightedAverageLuminance - prev.y) * (1.0 - exp(-deltaTime * speedSlow * tau));
        
        AveLumOutput[uint2(0, 0)] = float2(fast, slow);
        
        // --- AWB Logic ---
        float3 currentAvg = float3(1,1,1);
        if(AWBWeightShared[0] > 0)
            currentAvg = AWBColorShared[0] / AWBWeightShared[0];
            
        currentAvg = max(currentAvg, 0.001);

        // Calculate Target Gain for this frame
        float currentLuma = GetLuminanceRec2020(currentAvg);
        float3 currentGain = currentLuma / currentAvg; 
        
        float4 prevOutput = AWBOutput[uint2(0,0)];
        float3 prevGain = prevOutput.rgb;
        
        if(prevOutput.a == 0 || length(prevGain) < 0.1) prevGain = currentGain;
        
        float3 finalGain = lerp(prevGain, currentGain, 1.0 - exp(-deltaTime * tau * 0.05));
        float finalLum = max(fast, slow); 
        
        AWBOutput[uint2(0,0)] = float4(finalGain, finalLum);
    }
}
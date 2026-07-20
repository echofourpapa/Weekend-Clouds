#if !defined( COMPUTE_HEADER_H )
#define COMPUTE_HEADER_H

SamplerState linearWrapSampler  : register(s0);
SamplerState linearClampSampler : register(s1);
SamplerState pointWrapSampler   : register(s2);
SamplerState pointClampSampler  : register(s3);
SamplerState pointBorderSampler : register(s4);
SamplerState iblSampler : register(s5);

#define THREAD_X 8
#define THREAD_Y 8
#define THREAD_Z 1

struct ComputeShaderInput
{
	uint3 GroupID           : SV_GroupID;           // 3D index of the thread group in the dispatch.
	uint3 GroupThreadID     : SV_GroupThreadID;     // 3D index of local thread ID in a thread group.
	uint3 DispatchThreadID  : SV_DispatchThreadID;  // 3D index of global thread ID in the dispatch.
	uint  GroupIndex        : SV_GroupIndex;        // Flattened local index of the thread within a thread group.
};

#define COMPUTE_MAIN [numthreads(THREAD_X, THREAD_Y, THREAD_Z)] \
void main(ComputeShaderInput IN)

#endif
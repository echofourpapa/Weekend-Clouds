#if !defined( MESH_STRUCTS_H )
#define MESH_STRUCTS_H

struct VS_INPUT
{
    float3 pos : POSITION;
    float3 norm : NORMAL;
    float4 tan : TANGENT;
    float2 uv[2] : TEXCOORD;
};

struct VS_OUTPUT
{
    float4 pos : SV_POSITION;
#ifndef Shadow
	float3 wPos : POSITION0;
	float4 curPos : POSITION1;
	float4 prevPos : POSITION2;
	float3 norm : NORMAL;
	float3 tan :  TANGENT;
	float3 binorm : BINORMAL;
#endif
    float2 uv0 : TEXCOORD0;
    float2 uv1 : TEXCOORD1;
};

#ifdef Shadow
typedef void PS_DEFERRED_OUTPUT;
#else
struct PS_DEFERRED_OUTPUT
{
    float3 albedo : SV_TARGET0;
    float4 normal : SV_TARGET1;
    float4 metalrough : SV_TARGET2;
};
#endif

#endif // !defined( MESH_STRUCTS_H )
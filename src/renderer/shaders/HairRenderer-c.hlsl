
#include "Compute_Header.hlsli"
Texture2D<float4> SceneColor : register(t0);
Texture2D<float> DepthTex : register(t1);
StructuredBuffer<float4> points : register(t2);
StructuredBuffer<uint2> strands : register(t3);

RWTexture2D<float4> ColorOut : register(u0);

cbuffer ConstantBuffer : register(b0)
{
	float4x4 invView;
	float4x4 invProj;
	float4x4 worldViewProj;
	float4 camPos;
	float4 screenSize;
	uint standCount;
};

float3 HSVtoRGB(float3 HSV)
{
	float4 K = float4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
	float3 p = abs(frac(HSV.xxx + K.xyz) * 6.0 - K.www);

	return HSV.z * lerp(K.xxx, saturate(p - K.xxx), HSV.y);
}


float4 GetAABB(float2 a, float2 b)
{
	float2 tl = float2(a.x < b.x ? a.x : b.x, a.y < b.y ? a.y : b.y);
	float2 br = float2(a.x > b.x ? a.x : b.x, a.y > b.y ? a.y : b.y);
	return float4(tl.x, tl.y, br.x, br.y);
}

bool HitAABB(float4 aabb, float2 p)
{
	float2 tl = aabb.xy;
	float2 br = aabb.zw;

	return (p.x >= tl.x && p.x <= br.x) && (p.y >= tl.y && p.y <= br.y);
}

float HitStrand(float2 a, float2 b, float2 p)
{
	float2 ab = normalize(b - a);
	float2 ap = normalize(p - a);

	float d = dot(ab, ap);
	
	return d > 0.99999;
}

float Line(float2 p, float2 a, float2 b)
{
	float2 pa = p - a, ba = b - a;
	float h = saturate(dot(pa, ba) / dot(ba, ba));
	float2 d = pa - ba * h;
	return dot(d, d);
}

void plotLineLow(uint2 a, uint2 b, float3 color)
{
	int2 dxdy = b - a;
	int yi = 1;
	if (dxdy.y < 0)
	{
		yi = -1;
		dxdy.y *= -1;
	}

	int D = (2 * dxdy.y) - dxdy.x;
	uint y = a.y;
	for (uint x = a.x; x < b.x; ++x)
	{
		ColorOut[uint2(x, y)] = float4(color, 1);
		if (D > 0)
		{
			y += yi;
			D += 2 * (dxdy.y - dxdy.x);
		}
		else
		{
			D += 2 * dxdy.y;
		}
	}
}

void plotLineHigh(uint2 a, uint2 b, float3 color)
{
	int2 dxdy = b - a;
	int xi = 1;
	if (dxdy.x < 0)
	{
		xi = -1;
		dxdy.x *= -1;
	}

	int D = (2 * dxdy.x) - dxdy.y;
	uint x = a.x;
	for (uint y = a.y; y < b.y; ++y)
	{
		ColorOut[uint2(x, y)] = float4(color, 1);
		if (D > 0)
		{
			x += xi;
			D += 2 * (dxdy.x - dxdy.y);
		}
		else
		{
			D += 2 * dxdy.x;
		}
	}
}

void setPixelAA(int x, int y, int scale, float3 color)
{
	if (x < 0 || y < 0)
		return;
	if (x >= screenSize.z || y >= screenSize.w)
		return;
	float4 current = ColorOut.Load(uint2(x, y));
	float3 final = lerp(current.xyz, color, 1.f-(float(scale) / 255.f));
	ColorOut[uint2(x, y)] = float4(final, 1);
}

void plotLineAA(int x0, int y0, int x1, int y1, float3 color )
{
	int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
	int dy = abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
	int err = dx - dy, e2, x2;                       /* error value e_xy */
	int ed = dx + dy == 0 ? 1 : sqrt((float)dx * dx + (float)dy * dy);

	for (; ; ) {                                         /* pixel loop */
		setPixelAA(x0, y0, 255 * abs(err - dx + dy) / ed, color);

		e2 = err; x2 = x0;
		if (2 * e2 >= -dx) {                                    /* x step */
			if (x0 == x1) break;
			if (e2 + dy < ed) setPixelAA(x0, y0 + sy, 255 * (e2 + dy) / ed, color);
			err -= dy; x0 += sx;
		}
		if (2 * e2 <= dy) {                                     /* y step */
			if (y0 == y1) break;
			if (dx - e2 < ed) setPixelAA(x2 + sx, y0, 255 * (dx - e2) / ed, color);
			err += dx; y0 += sy;
		}
	}
}

[numthreads(8, 1, 1)]
void main(ComputeShaderInput IN)
{
	float4 currentColor = float4(0, 0, 0, 1);
	uint s = IN.DispatchThreadID.x;

	if (s >= standCount)
		return;

	uint2 strand = strands.Load(s);
	uint total = (strand.x + strand.y) - 1;

	for (uint i = strand.x; i < total; ++i)
	{
		float4 pointA = points.Load(i);
		float4 pointB = points.Load(i+1);
        float4 c_pos_A = mul(worldViewProj, float4(pointA.xyz, 1));
		c_pos_A.y *= -1;
		float3 uv_pos_A = c_pos_A.xyz / c_pos_A.w * 0.5 + 0.5;

        float4 c_pos_B = mul(worldViewProj, float4(pointB.xyz, 1));
		c_pos_B.y *= -1;
		float3 uv_pos_B = c_pos_B.xyz / c_pos_B.w * 0.5 + 0.5;

		float3 color = float3(((float(s) / float(standCount)) * 255.f) * (1.f/255.f), float(i- strand.x) / float(strand.y), 1);
		color = HSVtoRGB(color);

		int2 A = int2(clamp(round(uv_pos_A.xy * screenSize.zw), 0, screenSize.zw));
		int2 B = int2(clamp(round(uv_pos_B.xy * screenSize.zw), 0, screenSize.zw));

		plotLineAA(A.x, A.y, B.x, B.y, color);

	}
}
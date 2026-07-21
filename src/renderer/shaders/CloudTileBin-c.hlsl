// Screen-tile macro binning (docs/PLAN.md 4.7, step P2.1). One thread per
// 16x16-px tile: cull the macro list against the tile frustum, keep up to 64
// nearest, sorted front-to-back. The tiled trace then iterates only its tile's
// macros. This first version is one-thread-per-tile for clarity; P5 switches to
// a cooperative 64-thread group. Conservative (over-includes) so it can never
// drop a cloud that the brute path would draw.

#include "CloudKernels.hlsli"

StructuredBuffer<CloudMacro> g_macros : register(t1);

struct CloudTile { uint count; uint pad0, pad1, pad2; uint macroIdx[CLOUD_MAX_TILE_MACROS]; };
RWStructuredBuffer<CloudTile> g_tiles : register(u2);

// Ray through a pixel center (matches CloudRayDir but for arbitrary pixel).
float3 TileRay(float2 pixel)
{
    return CloudRayDir(pixel - 0.5, g_traceSize.zw);   // CloudRayDir adds +0.5
}

[numthreads(64, 1, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    uint tileCountX = g_counts.z;
    uint tileCountY = g_counts.w;
    uint tileIndex = dtid.x;
    if (tileIndex >= tileCountX * tileCountY) return;

    uint tx = tileIndex % tileCountX;
    uint ty = tileIndex / tileCountX;
    float2 p0 = float2(tx, ty) * CLOUD_TILE_PX;              // top-left pixel
    float2 p1 = p0 + CLOUD_TILE_PX;                          // bottom-right

    float3 o = g_camPosWS.xyz;
    // Four corner rays + a center ray (for inward-normal orientation).
    float3 rTL = TileRay(float2(p0.x, p0.y));
    float3 rTR = TileRay(float2(p1.x, p0.y));
    float3 rBR = TileRay(float2(p1.x, p1.y));
    float3 rBL = TileRay(float2(p0.x, p1.y));
    float3 rC  = normalize(rTL + rTR + rBR + rBL);

    // Inward-pointing plane normals through the camera (flip toward center ray).
    float3 n[4];
    n[0] = cross(rTL, rBL);   // left edge
    n[1] = cross(rBR, rTR);   // right edge
    n[2] = cross(rTR, rTL);   // top edge
    n[3] = cross(rBL, rBR);   // bottom edge
    [unroll] for (int e = 0; e < 4; ++e)
    {
        if (dot(n[e], rC) < 0.0) n[e] = -n[e];
        n[e] = normalize(n[e]);
    }

    uint macroCount = g_counts.x;
    uint outCount = 0;
    uint visibleTotal = 0;   // pre-clamp count; > 64 means this tile overflowed
    // Track farthest kept entry so we can replace it when full (keep nearest 64).
    float keptDist[CLOUD_MAX_TILE_MACROS];
    uint  keptIdx[CLOUD_MAX_TILE_MACROS];

    for (uint i = 0; i < macroCount; ++i)
    {
        CloudMacro m = g_macros[i];
        float3 c = float3(m.position[0], m.position[1], m.position[2]) - g_windOffset.xyz;
        float3 rel = c - o;
        float R = m.boundRadius;

        bool visible = true;
        [unroll] for (int e = 0; e < 4; ++e)
            if (dot(n[e], rel) < -R) { visible = false; }
        if (dot(rC, rel) < -R) visible = false;   // behind the tile cone

        if (!visible) continue;
        visibleTotal++;

        float dist = length(rel);
        if (outCount < CLOUD_MAX_TILE_MACROS)
        {
            keptDist[outCount] = dist;
            keptIdx[outCount] = i;
            outCount++;
        }
        else
        {
            // replace the current farthest if this one is nearer
            uint far = 0; float fd = keptDist[0];
            for (uint k = 1; k < CLOUD_MAX_TILE_MACROS; ++k)
                if (keptDist[k] > fd) { fd = keptDist[k]; far = k; }
            if (dist < fd) { keptDist[far] = dist; keptIdx[far] = i; }
        }
    }

    // Insertion sort the kept set front-to-back (<=64 entries).
    for (uint a = 1; a < outCount; ++a)
    {
        float d = keptDist[a]; uint id = keptIdx[a];
        int b = (int)a - 1;
        while (b >= 0 && keptDist[b] > d) { keptDist[b + 1] = keptDist[b]; keptIdx[b + 1] = keptIdx[b]; b--; }
        keptDist[b + 1] = d; keptIdx[b + 1] = id;
    }

    g_tiles[tileIndex].count = outCount;
    g_tiles[tileIndex].pad0 = visibleTotal;   // overflow telemetry (debug HUD/view)
    for (uint w = 0; w < outCount; ++w)
        g_tiles[tileIndex].macroIdx[w] = keptIdx[w];
}

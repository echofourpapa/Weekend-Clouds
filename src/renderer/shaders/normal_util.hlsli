#if !defined( NORMAL_UTIL_H )
#define NORMAL_UTIL_H


// https://knarkowicz.wordpress.com/2014/04/16/octahedron-normal-vector-encoding/

float3 DecodeNormal(float2 f)
{
    f = f * 2.0 - 1.0;
 
// https://twitter.com/Stubbesaurus/status/937994790553227264
    float3 n = float3(f.x, f.y, 1.0 - abs(f.x) - abs(f.y));
    float t = saturate(-n.z);
    n.xy += select(n.xy >= 0.0, -t, t);
    return normalize(n);
}

float2 OctWrap(float2 v)
{
    return (1.0 - abs(v.yx)) * select(v.xy >= 0.0,  1.0, -1.0);
}

float2 EncodeNormal(float3 n)
{
    n /= (abs(n.x) + abs(n.y) + abs(n.z));
    n.xy = n.z >= 0.0 ? n.xy : OctWrap(n.xy);
    n.xy = n.xy * 0.5 + 0.5;
    return n.xy;
}

float3 UnpackNormal(float2 pn)
{
    float3 norm = (pn.xyy * 2) - 1;
    norm.z = sqrt(1.0 - saturate(dot(norm.xy, norm.xy)));
    return normalize(norm);
}

float3 PerturbNormal(float2 normalMap, float3 vertNormal, float3 vertTan, float3 vertBinorm)
{
    float3 norm = UnpackNormal(normalMap);
    float3 perturb = norm.x * vertTan + norm.y * vertBinorm + norm.z * vertNormal;
    return normalize(perturb);
}


#endif //defined( NORMAL_UTIL_H )
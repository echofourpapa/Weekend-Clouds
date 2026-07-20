#if !defined( CLOUD_KERNELS_H )
#define CLOUD_KERNELS_H

// Analytic Gaussian/Gabor kernel primitives — the heart of the renderer.
// Every formula here is a transliteration of tools/validate_math.py, which is
// the authoritative reference (validated against numeric quadrature). If you
// change anything here, change the Python and re-run it first.
// Data layouts: docs/PLAN.md 3.1 (kernel, 32 B) and 3.2 (macro, 64 B).

#include "CloudCommon.hlsli"

// ---------------------------------------------------------------------------
// Packed data (must match CloudGenerator.h static_asserts)
// ---------------------------------------------------------------------------

struct CloudKernelPacked   // 32 B — two uint4 loads (DOD constraint C2)
{
    uint4 a;   // x: posX|posY snorm16   y: posZ|flags16   z: quat 4x snorm8   w: sigmaX|sigmaY f16
    uint4 b;   // x: sigmaZ|amp f16      y: freqX|freqY f16 z: freqZ|phase f16 w: seed16|reserved
};

struct CloudMacro          // 64 B
{
    float3 position;       // world, meters
    float  amplitude;      // peak extinction m^-1
    float3 sigma;          // world, meters
    uint   quatXY;         // quat x,y as 2x snorm16
    uint   quatZW;         // quat z,w as 2x snorm16 (w >= 0)
    uint   detailBegin;
    uint   detailCount;
    uint   seed;
    float  boundRadius;    // 3 * max(sigma)
    float  heightFrac01;
    uint   octaveMask;
    uint   pad0;
};

struct CloudKernel         // unpacked, register-resident
{
    float3 posWS;
    float3 row0, row1, row2;   // rotation rows (world -> kernel frame)
    float3 invSigma;
    float  amplitude;          // signed
    float3 freqWS;             // cycles / meter
    float  phaseTurns;
    uint   octave;
    bool   shadowVisible;
    uint   seed16;
};

// ---------------------------------------------------------------------------
// Unpack helpers
// ---------------------------------------------------------------------------

float2 UnpackSnorm16x2(uint v)
{
    int2 i = int2(v << 16, v) >> 16;               // sign-extend low/high halves
    return max(float2(i) / 32767.0, -1.0);
}

float4 UnpackSnorm8x4(uint v)
{
    int4 i = int4(v << 24, v << 16, v << 8, v) >> 24;
    return max(float4(i) / 127.0, -1.0);
}

float2 UnpackF16x2(uint v)
{
    return float2(f16tof32(v), f16tof32(v >> 16));
}

// quat (x,y,z,w) -> rotation rows mapping world vectors into the kernel frame
void QuatToRows(float4 q, out float3 row0, out float3 row1, out float3 row2)
{
    float x = q.x, y = q.y, z = q.z, w = q.w;
    row0 = float3(1.0 - 2.0 * (y * y + z * z), 2.0 * (x * y - z * w), 2.0 * (x * z + y * w));
    row1 = float3(2.0 * (x * y + z * w), 1.0 - 2.0 * (x * x + z * z), 2.0 * (y * z - x * w));
    row2 = float3(2.0 * (x * z - y * w), 2.0 * (y * z + x * w), 1.0 - 2.0 * (x * x + y * y));
}

// Unpack a detail kernel. The parent macro must be in registers/groupshared —
// positions are stored macro-local (snorm16 in units of 4*macro.sigma).
CloudKernel UnpackKernel(CloudKernelPacked p, CloudMacro parent)
{
    CloudKernel k;

    float2 pxy = UnpackSnorm16x2(p.a.x);
    float  pz  = UnpackSnorm16x2(p.a.y).x;
    uint   flags = p.a.y >> 16;

    float3 parentRow0, parentRow1, parentRow2;
    QuatToRows(float4(UnpackSnorm16x2(parent.quatXY), UnpackSnorm16x2(parent.quatZW)), parentRow0, parentRow1, parentRow2);
    float3 local = float3(pxy, pz) * (4.0 * parent.sigma);
    // Active rotation kernel->world: this column-combination is R*local =
    // rotate(parentQuat, local) (PLAN 3.1). The Phase-3 generator must therefore
    // store local = R^T * (childPos - parentPos) / (4*sigma) to round-trip.
    k.posWS = parent.position
            + float3(parentRow0.x, parentRow1.x, parentRow2.x) * local.x
            + float3(parentRow0.y, parentRow1.y, parentRow2.y) * local.y
            + float3(parentRow0.z, parentRow1.z, parentRow2.z) * local.z;

    QuatToRows(UnpackSnorm8x4(p.a.z), k.row0, k.row1, k.row2);

    float2 sxy = UnpackF16x2(p.a.w);
    float2 szA = UnpackF16x2(p.b.x);
    k.invSigma = 1.0 / max(float3(sxy, szA.x), 1e-3);
    k.amplitude = szA.y;

    float2 fxy = UnpackF16x2(p.b.y);
    float2 fzP = UnpackF16x2(p.b.z);
    k.freqWS = float3(fxy, fzP.x);
    k.phaseTurns = fzP.y;

    k.octave = flags & 0x3;
    k.shadowVisible = (flags & 0x4) != 0;
    k.seed16 = p.b.w & 0xFFFF;
    return k;
}

// A macro treated directly as a pure-Gaussian primitive (brute path / macro
// envelope integration). Detail Gabor kernels use UnpackKernel instead.
CloudKernel KernelFromMacro(CloudMacro m)
{
    CloudKernel k;
    k.posWS = m.position;
    QuatToRows(float4(UnpackSnorm16x2(m.quatXY), UnpackSnorm16x2(m.quatZW)), k.row0, k.row1, k.row2);
    k.invSigma = 1.0 / max(m.sigma, 1e-3);
    k.amplitude = m.amplitude;
    k.freqWS = float3(0, 0, 0);
    k.phaseTurns = 0.0;
    k.octave = 0;
    k.shadowVisible = true;
    k.seed16 = m.seed & 0xFFFF;
    return k;
}

// Raw macro density at a world point (reference march / erosion bounds).
float MacroDensity(CloudMacro m, float3 posWS)
{
    float3 r0, r1, r2;
    QuatToRows(float4(UnpackSnorm16x2(m.quatXY), UnpackSnorm16x2(m.quatZW)), r0, r1, r2);
    float3 rel = posWS - m.position;
    float3 p = float3(dot(r0, rel), dot(r1, rel), dot(r2, rel)) / max(m.sigma, 1e-3);
    return m.amplitude * exp(-0.5 * dot(p, p));
}

// ---------------------------------------------------------------------------
// erf / erfinv (validate_math.py: erf_as, erfinv_giles)
// ---------------------------------------------------------------------------

float ErfAS(float x)
{
    float s = x < 0.0 ? -1.0 : 1.0;
    float ax = abs(x);
    float t = 1.0 / (1.0 + 0.3275911 * ax);
    float p = t * (0.254829592 + t * (-0.284496736 + t * (1.421413741
            + t * (-1.453152027 + t * 1.061405429))));
    return s * (1.0 - p * exp(-ax * ax));
}

float ErfInvGiles(float x)
{
    float w = -log(max((1.0 - x) * (1.0 + x), 1e-30));
    float p;
    if (w < 5.0)
    {
        w = w - 2.5;
        p = 2.81022636e-08;
        p = 3.43273939e-07 + p * w;
        p = -3.5233877e-06 + p * w;
        p = -4.39150654e-06 + p * w;
        p = 0.00021858087 + p * w;
        p = -0.00125372503 + p * w;
        p = -0.00417768164 + p * w;
        p = 0.246640727 + p * w;
        p = 1.50140941 + p * w;
    }
    else
    {
        w = sqrt(w) - 3.0;
        p = -0.000200214257;
        p = 0.000100950558 + p * w;
        p = 0.00134934322 + p * w;
        p = -0.00367342844 + p * w;
        p = 0.00573950773 + p * w;
        p = -0.0076224613 + p * w;
        p = 0.00943887047 + p * w;
        p = 1.00167406 + p * w;
        p = 2.83297682 + p * w;
    }
    return p * x;
}

// ---------------------------------------------------------------------------
// Closed-form ray integrals (validate_math.py: kernel_ray_setup, tau_*_clamped)
// ---------------------------------------------------------------------------

struct KernelRayTerms
{
    float a, b, c;    // |p+tq|^2 = a t^2 + 2 b t + c   (kernel space)
    float tbar;       // ray-Gaussian mean = -b/a
    float omega;      // 2π f·d
    float psi;        // 2π f·(o-μ) + 2π phase
    bool  pureGauss;  // freq == 0: no cosine, no Gabor damping
};

KernelRayTerms KernelRaySetup(CloudKernel k, float3 rayOrigin, float3 rayDir)
{
    float3 rel = rayOrigin - k.posWS;
    float3 p = float3(dot(k.row0, rel), dot(k.row1, rel), dot(k.row2, rel)) * k.invSigma;
    float3 q = float3(dot(k.row0, rayDir), dot(k.row1, rayDir), dot(k.row2, rayDir)) * k.invSigma;

    KernelRayTerms t;
    t.a = max(dot(q, q), 1e-8);
    t.b = dot(p, q);
    t.c = dot(p, p);
    t.tbar = -t.b / t.a;
    t.pureGauss = dot(k.freqWS, k.freqWS) == 0.0;
    t.omega = 6.28318531 * dot(k.freqWS, rayDir);
    t.psi = 6.28318531 * (dot(k.freqWS, rel) + k.phaseTurns + g_windPhaseVel[k.octave] * g_camPosWS.w);
    return t;
}

// Signed optical depth over [t0, t1] with LOD low-pass folded into the single
// exp (DOD constraint C6). lodExp = -2π²·|f|²·s(tbar)² (0 for pure Gaussians).
float TauKernelClamped(float amplitude, KernelRayTerms t, float lodExp, float t0, float t1)
{
    float s = sqrt(0.5 * t.a);
    float bracket = ErfAS(s * (t1 - t.tbar)) - ErfAS(s * (t0 - t.tbar));

    float envExp = -0.5 * (t.c - t.b * t.b / t.a);
    float gaborExp = t.pureGauss ? 0.0 : -(t.omega * t.omega) / (2.0 * t.a);

    float e = exp(max(envExp + gaborExp + lodExp, -20.0));   // the ONE exp
    float osc = t.pureGauss ? 1.0 : cos(t.omega * t.tbar + t.psi);

    return amplitude * sqrt(6.28318531 / t.a) * 0.5 * e * osc * bracket;
}

// Convenience for macro envelopes / cache rays (pure Gaussian, full domain).
float TauGaussFull(float amplitude, KernelRayTerms t)
{
    float envExp = -0.5 * (t.c - t.b * t.b / t.a);
    return amplitude * sqrt(6.28318531 / t.a) * exp(max(envExp, -20.0));
}

// Truncated free-flight sample within [t0, t1] (validate_math.py: freeflight_truncated)
float FreeflightTruncated(KernelRayTerms t, float t0, float t1, float xi)
{
    float s = sqrt(0.5 * t.a);
    float e0 = ErfAS(s * (t0 - t.tbar));
    float e1 = ErfAS(s * (t1 - t.tbar));
    float u = clamp(lerp(e0, e1, xi), -0.999999, 0.999999);
    return t.tbar + sqrt(2.0 / t.a) * ErfInvGiles(u);
}

// Full-domain fast path predicate (validate_math.py: fullpath_eligible):
// sky pixel AND the kernel's ±3σ ray support entirely in front of the camera.
bool FullDomainEligible(float tGeom, KernelRayTerms t)
{
    return tGeom >= CLOUD_BIG_T && (t.tbar - 3.0 * sqrt(2.0 / t.a)) > 0.0;
}

// LOD low-pass exponent for a kernel at ray-mean distance tbar (PLAN.md 4.8):
// footprintScale = 2*tan(fovY/2)/traceHeight; returns ≤ 0.
float LodExponent(float3 freqWS, float tbar)
{
    float sFoot = g_lodParams.x * max(tbar, 0.0);
    return -19.7392088 * dot(freqWS, freqWS) * sFoot * sFoot;   // -2π²·|f|²·s²
}

#endif

#include "CloudGenerator.h"
#include "CloudSystem.h"
#include "Awesome.h"
#include "Util.h"
#include <DirectXMath.h>
#include <cmath>

using namespace Awesome;
using namespace DirectX;

const float CloudGenerator::c_cellMeters = 400.0f;

// ---- packing helpers (mirror the HLSL unpack in CloudKernels.hlsli) ----
static inline uint32 PackS16(float v)
{
    v = v < -1.0f ? -1.0f : (v > 1.0f ? 1.0f : v);
    return (uint32)((int)lroundf(v * 32767.0f) & 0xFFFF);
}
static inline uint32 PackS16x2(float a, float b) { return PackS16(a) | (PackS16(b) << 16); }
static inline uint32 PackS8(float v)
{
    v = v < -1.0f ? -1.0f : (v > 1.0f ? 1.0f : v);
    return (uint32)((int)lroundf(v * 127.0f) & 0xFF);
}
static inline uint32 PackS8x4(float x, float y, float z, float w)
{
    return PackS8(x) | (PackS8(y) << 8) | (PackS8(z) << 16) | (PackS8(w) << 24);
}
static inline uint32 PackF16x2(float a, float b)
{
    return XMConvertFloatToHalf(a) | ((uint32)XMConvertFloatToHalf(b) << 16);
}

// ---- deterministic hash / value-noise FBM (matches nothing on GPU; CPU only) ----
static inline uint32 Hash(uint32 x) { x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16; return x; }
static inline float Hash01(uint32 x) { return (Hash(x) & 0xFFFFFF) / float(0x1000000); }
static inline float Hash01(int xi, int yi, uint32 seed) { return Hash01((uint32)(xi * 73856093) ^ (uint32)(yi * 19349663) ^ seed); }

static float ValueNoise(float x, float y, uint32 seed)
{
    int xi = (int)floorf(x), yi = (int)floorf(y);
    float fx = x - xi, fy = y - yi;
    float ux = fx * fx * (3 - 2 * fx), uy = fy * fy * (3 - 2 * fy);
    float a = Hash01(xi, yi, seed), b = Hash01(xi + 1, yi, seed);
    float c = Hash01(xi, yi + 1, seed), d = Hash01(xi + 1, yi + 1, seed);
    return (a * (1 - ux) + b * ux) * (1 - uy) + (c * (1 - ux) + d * ux) * uy;
}
static float FBM(float x, float y, uint32 seed, int octaves)
{
    float sum = 0, amp = 0.5f, freq = 1.0f;
    for (int i = 0; i < octaves; ++i) { sum += amp * ValueNoise(x * freq, y * freq, seed + i * 101); freq *= 2.0f; amp *= 0.5f; }
    return sum;
}

CloudGenerator::CloudGenerator(AwesomeGraphics* Awesome, CloudSystem* clouds)
    : m_Awesome(Awesome), m_clouds(clouds) {}
CloudGenerator::~CloudGenerator() {}

static ID3D12Resource* CreateStructured(AwesomeGraphics* g, uint32 count, uint32 stride, D3D12_RESOURCE_STATES state, const wchar_t* name)
{
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = (uint64)count * stride;
    desc.Height = 1; desc.DepthOrArraySize = 1; desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    desc.Format = DXGI_FORMAT_UNKNOWN;
    return g->CreateBuffer(desc, name, D3D12_HEAP_TYPE_DEFAULT, state);
}

bool CloudGenerator::StartUp()
{
    m_macroBuf = CreateStructured(m_Awesome, c_maxMacros, sizeof(CloudMacro), D3D12_RESOURCE_STATE_COPY_DEST, L"Cloud Macro Buffer");
    m_kernelBuf = CreateStructured(m_Awesome, c_maxKernels, sizeof(CloudKernelPacked), D3D12_RESOURCE_STATE_COPY_DEST, L"Cloud Kernel Buffer");
    if (!m_macroBuf || !m_kernelBuf) return false;

    D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
    srv.Format = DXGI_FORMAT_UNKNOWN;
    srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Buffer.NumElements = c_maxMacros;
    srv.Buffer.StructureByteStride = sizeof(CloudMacro);
    m_clouds->WriteSRV(SRV_Macro, m_macroBuf, &srv);
    srv.Buffer.NumElements = c_maxKernels;
    srv.Buffer.StructureByteStride = sizeof(CloudKernelPacked);
    m_clouds->WriteSRV(SRV_Kernel, m_kernelBuf, &srv);
    return true;
}

bool CloudGenerator::TearDown()
{
    SafeRelease(m_macroBuf);
    SafeRelease(m_kernelBuf);
    return true;
}

void CloudGenerator::Regenerate()
{
    m_cpuMacros.clear();
    m_cpuKernels.clear();
    const uint32 K = m_kernelsPerMacro;
    const int G = c_gridDim;
    const float half = 0.5f * G * c_cellMeters;
    const float lambda[4] = { 1600.0f, 600.0f, 250.0f, 110.0f };
    int oct = (int)(m_octaves < 1 ? 1 : (m_octaves > 4 ? 4 : m_octaves));

    for (int cy = 0; cy < G; ++cy)
    for (int cx = 0; cx < G; ++cx)
    {
        float u = cx / (float)G, v = cy / (float)G;
        float cov = FBM(u * 8.0f, v * 8.0f, m_seed, 4);
        float thresh = 1.0f - m_coverage;
        if (cov < thresh) continue;
        if (m_cpuMacros.size() >= c_maxMacros) break;
        if ((uint32)((m_cpuMacros.size() + 1) * K) > c_maxKernels) break;

        uint32 cellSeed = Hash((uint32)cx * 92837111u ^ (uint32)cy * 689287499u ^ m_seed);
        float jx = (Hash01(cellSeed) - 0.5f) * c_cellMeters;
        float jz = (Hash01(cellSeed ^ 0x9e3779b9u) - 0.5f) * c_cellMeters;
        float type = FBM(u * 4.0f + 13.0f, v * 4.0f + 7.0f, m_seed + 555, 2);
        type = type * 0.5f + m_cloudType * 0.5f;

        CloudMacro m = {};
        m.position[0] = cx * c_cellMeters - half + jx;
        m.position[2] = cy * c_cellMeters - half + jz;
        float sx = 700.0f + 700.0f * (cov - thresh) / (m_coverage + 1e-3f);
        sx = sx < 500.0f ? 500.0f : (sx > 1600.0f ? 1600.0f : sx);
        float sz = sx * (0.8f + 0.4f * Hash01(cellSeed ^ 0x55u));
        float sy = sx * (0.35f + 0.35f * type);           // flattened, taller for cumulus
        m.position[1] = m_cloudBaseY + type * (m_cloudTopY - m_cloudBaseY - 1.2f * sy);
        m.sigma[0] = sx; m.sigma[1] = sy; m.sigma[2] = sz;
        // fold expected erosion back into the base amplitude so LOD fade doesn't
        // change distant coverage (octaves are ~zero-mean).
        m.amplitude = (0.03f + 0.03f * type) * (1.0f + 0.7f * 0.5f);

        float yaw = (Hash01(cellSeed ^ 0xabcdu) - 0.5f) * 6.2831853f;
        float qy = sinf(yaw * 0.5f), qw = cosf(yaw * 0.5f);
        m.quatXY = PackS16x2(0.0f, qy);
        m.quatZW = PackS16x2(0.0f, qw < 0.0f ? -qw : qw);
        m.detailBegin = (uint32)m_cpuMacros.size() * K;
        m.detailCount = K;
        m.seed = cellSeed;
        m.boundRadius = 3.0f * (sx > sz ? sx : sz);
        m.heightFrac01 = type;
        m.octaveMask = (1u << oct) - 1u;
        m_cpuMacros.push_back(m);

        // Gabor detail kernels for this macro (fixed slot range).
        for (uint32 k = 0; k < K; ++k)
        {
            uint32 ks = Hash(cellSeed ^ (k * 2654435761u));
            int o = k % oct;
            float lam = lambda[o] * (0.75f + 0.5f * Hash01(ks ^ 1u));

            // local offset in units of 4*sigma (parent-local frame), clamped ±1
            float gx = (Hash01(ks ^ 2u) - 0.5f), gy = (Hash01(ks ^ 3u) - 0.5f), gz = (Hash01(ks ^ 4u) - 0.5f);
            float spread = 0.55f;
            float lx = gx * spread, ly = gy * spread * 0.7f, lz = gz * spread;
            lx = lx < -1 ? -1 : (lx > 1 ? 1 : lx); ly = ly < -1 ? -1 : (ly > 1 ? 1 : ly); lz = lz < -1 ? -1 : (lz > 1 ? 1 : lz);

            float ksig = lam * (0.5f + 0.15f * (Hash01(ks ^ 5u) - 0.5f));

            // frequency direction: octave 0 horizontal-biased, higher octaves uniform
            float fdx = Hash01(ks ^ 6u) - 0.5f, fdy = Hash01(ks ^ 7u) - 0.5f, fdz = Hash01(ks ^ 8u) - 0.5f;
            if (o == 0) fdy *= 0.3f;
            float fl = sqrtf(fdx * fdx + fdy * fdy + fdz * fdz) + 1e-5f;
            float fmag = 1.0f / lam;
            fdx = fdx / fl * fmag; fdy = fdy / fl * fmag; fdz = fdz / fl * fmag;

            // amplitude: octave falloff, signed erosion for high octaves, bounded
            float sign = 1.0f;
            if (o >= 2 && Hash01(ks ^ 9u) < 0.5f) sign = -1.0f;
            float amp = m.amplitude * powf(0.55f, (float)o) * sign;
            // erosion bound ~0.7 * parent density at the child (approx: exp(-0.5*|local*4|^2))
            float rr = (lx * 4) * (lx * 4) + (ly * 4) * (ly * 4) + (lz * 4) * (lz * 4);
            float parentDens = m.amplitude * expf(-0.5f * rr);
            if (sign < 0.0f && fabsf(amp) > 0.7f * parentDens) amp = -0.7f * parentDens;

            float phase = Hash01(ks ^ 10u);
            uint32 flags = (uint32)o & 0x3;
            if (o <= 1) flags |= 0x4;                     // shadowVisible

            // quat random
            float q0 = Hash01(ks ^ 11u) - 0.5f, q1 = Hash01(ks ^ 12u) - 0.5f, q2 = Hash01(ks ^ 13u) - 0.5f, q3 = Hash01(ks ^ 14u) - 0.5f;
            float ql = sqrtf(q0 * q0 + q1 * q1 + q2 * q2 + q3 * q3) + 1e-5f;
            q0 /= ql; q1 /= ql; q2 /= ql; q3 /= ql; if (q3 < 0) { q0 = -q0; q1 = -q1; q2 = -q2; q3 = -q3; }

            CloudKernelPacked p = {};
            p.a[0] = PackS16x2(lx, ly);
            p.a[1] = PackS16(lz) | (flags << 16);
            p.a[2] = PackS8x4(q0, q1, q2, q3);
            p.a[3] = PackF16x2(ksig, ksig);
            p.b[0] = PackF16x2(ksig, amp);
            p.b[1] = PackF16x2(fdx, fdy);
            p.b[2] = PackF16x2(fdz, phase);
            p.b[3] = ks & 0xFFFF;
            m_cpuKernels.push_back(p);
        }
    }

    m_macroCount = (uint32)m_cpuMacros.size();
    m_kernelCount = (uint32)m_cpuKernels.size();
    m_dirty = false;
    m_uploaded = false;
    DebugPrint("CloudGenerator: %u macros, %u kernels (%.1f MB)\n",
        m_macroCount, m_kernelCount, m_kernelCount * 32.0f / (1024.0f * 1024.0f));
}

void CloudGenerator::EnsureUploaded()
{
    if (m_dirty)
        Regenerate();
    if (m_uploaded || m_cpuMacros.empty())
        return;

    // On regen the buffers are back in NON_PIXEL; return them to COPY_DEST first.
    if (m_inReadState)
    {
        m_Awesome->TransitionResource(m_macroBuf, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
        m_Awesome->TransitionResource(m_kernelBuf, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
    }

    // AwesomeGraphics::UploadBuffer wants a desc whose Width is the ACTUAL byte
    // count and whose Flags are NONE (an UPLOAD staging heap cannot carry
    // ALLOW_UNORDERED_ACCESS, and GetCopyableFootprints sizes the copy from
    // Width). Reusing the DEFAULT buffer's full-capacity/UAV desc read past the
    // source vector and failed the staging-heap creation. Build a fresh desc.
    D3D12_RESOURCE_DESC md = m_macroBuf->GetDesc();
    md.Width = (uint64)m_cpuMacros.size() * sizeof(CloudMacro);
    md.Flags = D3D12_RESOURCE_FLAG_NONE;
    m_Awesome->UploadBuffer(m_macroBuf, md, md.Width, (const uint8*)m_cpuMacros.data());
    m_Awesome->TransitionResource(m_macroBuf, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    if (!m_cpuKernels.empty())
    {
        D3D12_RESOURCE_DESC kd = m_kernelBuf->GetDesc();
        kd.Width = (uint64)m_cpuKernels.size() * sizeof(CloudKernelPacked);
        kd.Flags = D3D12_RESOURCE_FLAG_NONE;
        m_Awesome->UploadBuffer(m_kernelBuf, kd, kd.Width, (const uint8*)m_cpuKernels.data());
    }
    m_Awesome->TransitionResource(m_kernelBuf, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    m_uploaded = true;
    m_inReadState = true;
}

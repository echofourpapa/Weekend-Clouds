# Weekend-Clouds — Gabor-Field Cloud Renderer: Implementation Specification

This is the **canonical spec**. It is written so that an agent (or human) with no prior context can
implement any remaining step correctly. Read §0 before touching anything.

Technique source: "Gabor Fields" (Condor et al., arXiv 2602.05081) + "Don't Splat your Gaussians"
(Condor et al., TOG 2024, arXiv 2405.15425). Target: **≤1 ms GPU at 2560×1440 on RTX 4070-class**
(504 GB/s DRAM, ~29 TFLOP FP32, 36 MB L2). R&D project: exploration and demos over production polish,
but wrong math or spec violations are not acceptable.

---

## 0. Agent protocol (read first)

1. **State lives in two places only**: `git log` on branch `claude/realtime-cloud-renderer-d3d12-7cartj`
   and `docs/STATUS.md`. Before doing ANY work: read `docs/STATUS.md`, run `git log --oneline -15`,
   and reconcile. STATUS.md is a checklist of the atomic steps in §7; each step has an ID (P0.1 …).
2. **One step = one commit.** Commit subject format: `P<phase>.<step>: <summary>` (e.g.
   `P2.3: tiled trace with groupshared staging`). Update the step's checkbox + commit SHA in
   STATUS.md **in the same commit**. Push after every commit (`git push -u origin
   claude/realtime-cloud-renderer-d3d12-7cartj`; on network failure retry 4× with 2/4/8/16 s backoff).
3. **Never leave the tree red.** Every commit must pass `tools/check_shaders.sh` (if shaders changed)
   and `tools/validate_math.py` (if `CloudKernels.hlsli` math changed). C++ cannot be compiled in the
   Linux container — compensate by following §6 wiring contracts exactly and keeping diffs small.
4. **Anchors, not line numbers.** All code locations in this spec are given as grep anchors
   (`anchor: "text"`); line numbers drift. `grep -n` for the anchor in the named file.
5. **Do not refactor the host engine** beyond what a step explicitly requires. The engine has known
   warts (raw pointers, god-class); leave them. Match its style: `namespace Awesome`, `m_` members,
   `PascalCase` methods, `uint32` from `types.h`, HRESULT + `FAILED()` early-return, no exceptions,
   no smart pointers, `SafeRelease` in TearDown.
6. **When this spec and the engine disagree** (an anchor missing, a signature changed), stop, re-read
   the surrounding code, adapt minimally, and record the deviation in STATUS.md under "Deviations".
7. Windows build/run is done by the user, not you. When a step's acceptance says "USER:", finish the
   step, note "awaiting user verification" in STATUS.md, and continue with the next step unless it
   depends on the user's result.

---

## 1. Non-negotiable constraints (DOD priority order)

When two choices conflict, the LOWER number wins.

- **C1 — L2 residency.** All data touched per-candidate in the trace inner loop (kernel buffer +
  macro buffer + tile lists) must total **≤16 MB**. Hard caps: `c_maxKernels = 400*1024` (400k ×
  32 B = 12.8 MB), `c_maxMacros = 8192` (× 64 B = 0.5 MB). Enforce in C++ (`static_assert` on struct
  sizes, runtime clamp on generation params) and surface in ImGui ("X kernels = Y MB of 16 MB").
- **C2 — Hot data is 32 B/kernel, exactly.** Layout in §3.1. No field may be added to the hot record;
  cold/derived data goes to the macro record or the constant buffer.
- **C3 — Broadcast access, not divergent traversal.** The primary trace iterates tile-binned macros
  and stages detail kernels through groupshared so all 256 threads of a group read the same kernel
  data. Per-thread BVH traversal exists only in clearly-marked A/B comparison paths.
- **C4 — Zero per-frame CPU allocation.** No descriptor allocation, no view creation, no ResourcePool
  calls, no readbacks in the per-frame cloud path. Everything persistent, created in `StartUp`.
  Constants via 3 persistently-mapped upload buffers (one per frame index). The only per-frame CPU
  work: memcpy constants, set root params, record dispatches/barriers.
- **C5 — Deterministic GPU data flow.** Kernel placement is a pure function of (seed, weather map,
  params). Compaction via prefix sum, never raw UAV-append ordering. Same seed ⇒ same buffers.
- **C6 — Transcendental fusion.** The inner loop computes ONE `exp` and ONE `cos` per surviving
  candidate: sum all exponent terms (envelope + Gabor damping + LOD) before the single `exp`.
- **C7 — ≤6 compute PSOs for the cloud system.** Debug views and lighting mode are a uniform branch
  on `CloudConstants.debugView` / `.lightMode`. `#define` permutations only for `TRACE_RQ` (RayQuery
  A/B) and `TRACE_BRUTE` (Phase 1 path).

---

## 2. Architecture / frame graph

```
                 (amortized, on change)                 (per frame)
 Weather 512²  ─┐
 GenMacro      ─┼─► macro buf ─► GenScan ─► GenDetail ─► kernel buf (32B packed)
 (+ MacroGrid rasterize: 32×8×32 cell → macro lists)         │
 SkyLUTs (trans 256×64, skyview 192×108) ── on sun change    │
 LightCache 128×32×128 fp16 τ ── time-sliced 1/8 per frame ──┤ (reads MacroGrid)
                                                             ▼
 CloudTileBin (per 16×16-px tile @720p: cull+sort macros) ─► tile buf
                                                             ▼
 CloudTrace @1280×720 (tiled, groupshared; TRACE_RQ/TRACE_BRUTE variants)
   → Scattering RGBA16F (rgb inscatter, a transmittance) + CloudDepth R16F
                                                             ▼
 CloudDenoise (à-trous ×2, 720p) ─► CloudReproject (temporal, 720p, ping-pong history)
                                                             ▼
 CloudComposite @2560×1440 (bilateral upsample + sky background + blend into deferred HDR)
```

Hook: inside `AwesomeGraphics::Render` after the Deferred Lighting scope
(anchor in `src/renderer/src/Awesome.cpp`: `"Deferred Lighting"`), before
`m_scene->GetCamera()->EndFrame()`. The deferred output buffer is in `UNORDERED_ACCESS` there and is
consumed by TAA afterwards. Cloud rays are **unjittered** (do not apply the TAA jitter to ray setup).

---

## 3. Data specifications (bit-exact)

### 3.1 CloudKernelPacked — 32 B hot record (C++ `CloudGenerator.h` + HLSL `CloudKernels.hlsli`)

Two `uint4` loads. `static_assert(sizeof(CloudKernelPacked)==32)`.

| uint | bits 0–15 | bits 16–31 |
|---|---|---|
| u0 | posX: snorm16, kernel-local X | posY: snorm16, local Y |
| u1 | posZ: snorm16, local Z | flags16 (below) |
| u2 | quat: 4×snorm8 (x,y,z,w), w canonicalized ≥ 0 | — (packed across all 32 bits) |
| u3 | sigmaX: f16 (meters) | sigmaY: f16 |
| u4 | sigmaZ: f16 | amplitude: f16, SIGNED — negative = erosion, units m⁻¹ (extinction) |
| u5 | freqX: f16 | freqY: f16 |
| u6 | freqZ: f16 | phase: f16, stored in TURNS [0,1) |
| u7 | seed16: masking-hash seed | reserved = 0 |

- `flags16`: bits 0–1 octave (0–3); bit 2 shadowVisible; bits 3–15 reserved 0.
- **Position reconstruction** (the parent macro is always in registers/groupshared when a detail
  kernel is read): `posWS = macro.position + rotate(macro.quat, snorm3 * (4.0 * macro.sigma))`.
  Generation must clamp detail positions to ±4σ of the parent.
- **freq is a world-space frequency vector** (cycles/meter) — no rotation needed at eval:
  `omega = 2π·dot(freq, rayDirWS)`, `psi = 2π·(dot(freq, rayOriginWS − kernelPosWS)) + 2π·phaseTurns`.
- quat rotates only the Gaussian envelope (σ frame).
- Pure Gaussian kernels (macros' own contribution is separate; a detail kernel with freq==0 is legal
  and takes the Gaussian-only path).

### 3.2 CloudMacro — 64 B (`static_assert`ed)

| offset | field | type |
|---|---|---|
| 0 | position | float3 (world, meters) |
| 12 | amplitude | float (peak extinction m⁻¹, ≥0) |
| 16 | sigma | float3 (world, meters) |
| 28 | quatXY | uint (quat x,y as 2×snorm16) |
| 32 | quatZW | uint (quat z,w as 2×snorm16, w ≥ 0) |
| 36 | detailBegin | uint (index into kernel buffer) |
| 40 | detailCount | uint |
| 44 | seed | uint |
| 48 | boundRadius | float (= 3·max(sigma), for culling sphere) |
| 52 | heightFrac01 | float (cloud-base-relative height of center, for ambient gradient) |
| 56 | octaveMask | uint (bit k set ⇒ octave k present) |
| 60 | pad0 | uint = 0 |

### 3.3 Buffers & textures (all persistent, self-owned by cloud subsystems — never ResourcePool)

| name | type/format | size | states it cycles |
|---|---|---|---|
| kernelBuf | StructuredBuffer 32 B | 400k → 12.8 MB | UAV (gen) ↔ SRV (trace) |
| macroBuf | StructuredBuffer 64 B | 8192 → 512 KB | UAV ↔ SRV |
| macroCountBuf | 4×uint (count, scanTotal, pad, pad) | 16 B | UAV ↔ SRV; copied to readback ONLY on regen |
| macroGridBuf | StructuredBuffer 128 B/cell (uint count + 31 uint idx) | 32×8×32 cells → 1 MB | UAV ↔ SRV |
| tileBuf | StructuredBuffer 272 B/tile (uint count + up to 64 uint idx, sorted near→far, + pad) | 80×45 tiles → 980 KB | UAV (bin) ↔ SRV (trace) |
| weatherTex | Tex2D R8G8 (R coverage, G type) | 512² | UAV ↔ SRV |
| skyTransLUT | Tex2D R11G11B10 | 256×64 | UAV ↔ SRV |
| skyViewLUT | Tex2D R11G11B10 | 192×108 | UAV ↔ SRV |
| lightCacheTex | **Tex3D** R16F (τ toward sun) — six-way variant: R16G16B16A16F ×2 (τ±X,τ±Y / τ±Z,pad) | 128×32×128 | UAV (slice) ↔ SRV (trace) |
| scatterTex | Tex2D RGBA16F | 1280×720 | UAV ↔ SRV |
| cloudDepthTex | Tex2D R16F (reservoir-winner t; 0 ⇒ "no cloud", reproject as infinity) | 1280×720 | UAV ↔ SRV |
| denoiseTmp | Tex2D RGBA16F | 1280×720 | UAV ↔ SRV |
| historyTex[2] | Tex2D RGBA16F ping-pong (+ historyDepth[2] R16F) | 1280×720 | UAV ↔ SRV, alternating |
| blueNoiseTex | Tex2D R8G8B8A8 64×64 (generated once by compute at StartUp — do NOT add an asset) | 64² | SRV only |
| cloudCB[3] | upload heap, persistently mapped, 1 per frame index | align(sizeof(CloudConstants),256) | GENERIC_READ forever |

Light cache extent: fixed box, XZ centered on camera **snapped to whole voxels** (voxel = 250 m ×
250 m × 250 m ⇒ 32 km × 8 km × 32 km), Y from 0 to 8 km. Toroidal addressing; on origin shift, the
revealed slab's voxels are stale — they refresh within one slice rotation (acceptable; note in ImGui).

### 3.4 CloudConstants (cbuffer b0; C++ mirror in `CloudSystem.h`; HLSL in `CloudCommon.hlsli`)

Exact order; 16-byte aligned rows; `static_assert(sizeof(CloudConstants) % 16 == 0)`:

```c
float4x4 invViewProj;       // unjittered, current frame
float4x4 prevViewProj;      // unjittered, previous frame
float4   camPosWS;          // xyz cam pos, w = time seconds
float4   sunDirWS;          // xyz normalized TOWARD sun, w = sun angular radius (0.004625 rad)
float4   sunRadiance;       // rgb, w = exposure hint
float4   windOffset;        // xyz accumulated advection offset (m), w = windSpeed
float4   windPhaseVel;      // per-octave phase velocity (turns/sec), octaves 0..3
float4   cacheOriginWS;     // xyz world origin of light-cache voxel (0,0,0), w = 1/voxelSize
float4   traceSize;         // x=1280, y=720, z=1/x, w=1/y
float4   outputSize;        // x=2560, y=1440, z=1/x, w=1/y
uint4    counts;            // x macroCount, y kernelCount, z tileCountX(80), w tileCountY(45)
float4   lodParams;         // x footprintScale = 2*tan(fovY/2)/traceHeight, y lodSkipThreshold(0.02),
                            // z maskAggressiveness(1=off..8), w survivalFloor(0.05)
float4   scatterParams;     // x hgG0(0.85), y hgG1(-0.15), z hgBlend(0.7), w msOctaves(3)
float4   ambientParams;     // x ambientStrength, y groundAlbedo, z cloudBaseY(m), w cloudTopY(m)
uint4    mode;              // x debugView(enum §A2), y lightMode(0 sunCache,1 sixWay,2 rtReference),
                            // z traversalMode(0 tiled,1 rqMacro,2 rqKernel,3 brute), w frameIndex
float4   temporal;          // x alphaBase(0.07), y disocclusionTauDelta(0.15), z maxHistoryFrames, w histBlendMax
float4   skyParams;         // x turbidity, y groundOffsetKm, z pad, w pad
uint4    genParams;         // x seed, y kernelsPerMacroPerOctave, z octaveCount(1..4), w cacheSliceIndex
float4   erosionParams;     // x erosionBoundK(0.7), y detailPosClamp(4.0 sigma), z minSigmaM, w maxSigmaM
```

Frequency of update: whole struct rewritten every frame (memcpy into cloudCB[frameIndex]); cheap.

### 3.5 Descriptor plan

Global register map, identical for every cloud pass (unused slots are bound anyway — they are
persistent views, binding costs nothing):

- `b0` — root CBV (root param 0, GPU VA of cloudCB[frameIndex]).
- `t0` kernelBuf, `t1` macroBuf, `t2` tileBuf, `t3` weatherTex, `t4` skyTransLUT, `t5` skyViewLUT,
  `t6` scene depth (R32F view of D32), `t7` blueNoiseTex, `t8` lightCacheTex, `t9` historyRead
  (color), `t10` historyRead (depth), `t11` scatterTex (read for denoise/reproject), `t12`
  cloudDepthTex read, `t13` denoiseTmp read, `t14` macroGridBuf — one SRV table (root param 1).
- `u0` kernelBuf, `u1` macroBuf, `u2` tileBuf, `u3` weatherTex, `u4` skyTransLUT, `u5` skyViewLUT,
  `u6` lightCacheTex, `u7` scatterTex, `u8` cloudDepthTex, `u9` denoiseTmp, `u10` historyWrite color,
  `u11` historyWrite depth, `u12` deferred HDR output (R11G11B10 typed UAV), `u13` macroCountBuf,
  `u14` macroGridBuf — one UAV table (root param 2).
- root param 3: root SRV `t15` = TLAS GPU VA (RQ paths only; set 0 when unused — never accessed then).
- Static samplers: reuse `c_computeSamplers` from `includes/Compute.h`.

Persistent tables: SRV/UAV tables vary by (frameIndex∈{0,1,2} for depth+CB, pingpong∈{0,1} for
history) ⇒ allocate **6 SRV blocks + 6 UAV blocks at StartUp from `DescriptorSection::Assets`**
(persistent section; the 256-slot System transient list is nearly full — verified engine trap). If
`DescriptorHeap::AllocateBlock` lacks a section parameter, add one (default = current behavior).
Depth SRV: the engine has one depth buffer per frame index (anchor `"m_depthStencilBuffer"` in
`Awesome.h`) — that is why tables are per-frame-index. Total ≈ 6×15 + 6×15 = 180 descriptors from
Assets (1024-heap has room; Sponza uses well under 500 — verify with a debug print at StartUp).

The root signature is built once in `CloudSystem::StartUp` (serialize v1.0 like the engine does,
anchor `"D3D12SerializeRootSignature"` in `GTAO.cpp` for the pattern) and shared by all cloud PSOs.

### 3.6 PSO inventory (C7: ≤6)

1. `CloudGen` — entry per-dispatch via defines is NOT allowed (would multiply PSOs); instead 4
   separate thin `-c.hlsl` files sharing `CloudKernels.hlsli`: Weather, GenMacro, GenScan, GenDetail
   — these 4 count as ONE logical stage but are 4 PSOs? No — **revision**: generation runs rarely
   (regen only), so its PSOs are exempt from C7's spirit (C7 guards the per-frame path). Per-frame
   PSOs: `CloudTileBin`, `CloudTrace` (+`CloudTraceRQ`, `CloudTraceBrute` variants — PSOs exist but
   only one is dispatched per frame), `CloudLightCache`, `CloudDenoise`, `CloudReproject`,
   `CloudComposite`, `SkyLUT` (amortized). Total ≈ 12 PSOs, but ≤7 dispatched in a steady-state
   frame, and debug/lighting modes are uniform branches, not PSOs. This is the binding
   interpretation of C7.

---

## 4. Pass specifications

Common: all compute, threadgroup sizes below, all use the shared root signature (§3.5). Barrier
notation: `A: UAV→SRV` = `TransitionResource(A, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)` via the engine helper (anchor
`"TransitionResource"` in `Awesome.cpp`).

### 4.1 SkyLUT-c.hlsl — entry `main`, 8×8
Uniform-branch on a push value in `skyParams.z` (0 = transmittance pass, 1 = sky-view pass); two
dispatches back-to-back with a UAV barrier when the sun/turbidity changed this frame (dirty flag).
- Model: single-scattering Rayleigh+Mie, no ozone. Constants: βR=(5.802,13.558,33.1)e-6 m⁻¹,
  βM=3.996e-6·turbidity, HR=8 km, HM=1.2 km, planet R=6360 km, atmosphere top 6420 km.
- Transmittance LUT (256×64): x = cosZenith remapped `x=(µ+0.15)/1.15` (µ∈[−0.15,1]), y = altitude
  0–60 km. 40-step trapezoid march to atmosphere exit; store `exp(−τ)`.
- Sky-view LUT (192×108): lat-long around camera at ground level; x = azimuth relative to sun
  [−π,π], y = non-linear elevation `v = 0.5+0.5·sign(e)·sqrt(|e|/(π/2))`. 32-step march using the
  transmittance LUT for both view and sun segments. HG g=0.8 for Mie.
- Sun disc is NOT in the LUT — composite adds it analytically (radius `sunDirWS.w`, limb-darkened
  `pow(cosR, 0.6)`), scaled by transmittance-LUT toward sun.

### 4.2 CloudWeather-c.hlsl — entry `main`, 8×8, 512²
R = coverage [0,1]: 4-octave value-noise FBM (hash from `genParams.seed`), remapped by user coverage
slider `cov`: `R = saturate((fbm − (1−cov·1.2)) / max(cov·1.2,1e-3))`. G = cloud type [0,1] =
independent 2-octave FBM. Tiling: sample space wraps at 512 (fract). World mapping: 1 texel = 100 m
(51.2 km repeat).

### 4.3 CloudGenMacro-c.hlsl — entry `main`, 8×8 over a 128×128 placement grid
Cell world size 400 m (covers 51.2 km, matching weather repeat). Per cell: jittered sample of
weatherTex at cell center; if `coverage > hash01(cell,seed)` place ONE macro:
- `position.xz` = cell center + jitter(±150 m); `position.y` = cloudBase + type·(cloudTop−cloudBase−1.2·sigmaY`)
  with cloudBase/TopY from `ambientParams`.
- `sigma`: base 600–1400 m by coverage; **flattened**: σy = σx·lerp(0.35, 0.8, type); σx≈σz(±20%).
- `quat`: yaw-only rotation, random.
- `amplitude`: `lerp(0.02, 0.08, coverage)` m⁻¹ **minus the expected erosion mass** (see 4.5) —
  implemented as: amplitude *= (1 + erosionBoundK·0.5) so detail octaves are ~zero-mean (constraint
  from the adversarial review: LOD fade must not change distant coverage).
- Write via `InterlockedAdd(macroCountBuf[0])` into macroBuf (order does not matter for macros;
  determinism comes from cell-keyed seeds, not slot order). detailCount = kernelsPerMacroPerOctave ×
  popcount(octaveMask visible for this type); octaveMask from type (stratus: octaves 0–2, cumulus 0–3).
- Clamp: if count would exceed `c_maxMacros`, skip (InterlockedAdd result ≥ max ⇒ decrement back).

### 4.4 CloudGenScan-c.hlsl — entry `main`, ONE group of 1024 threads
Exclusive prefix sum over `macroBuf[i].detailCount` for i < macroCount (≤8192 ⇒ 8 elements/thread,
groupshared Hillis–Steele). Writes `detailBegin` into each macro and total into `macroCountBuf[1]`.
If total > `c_maxKernels`: scale every detailCount down proportionally (recompute in the same pass:
factor = maxKernels/total, detailCount = floor(detailCount·factor), re-scan — one iteration is fine).

### 4.5 CloudGenDetail-c.hlsl — entry `main`, one 64-thread group PER MACRO (dispatch macroCount groups)
Threads cooperatively fill `[detailBegin, detailBegin+detailCount)`. Per kernel k (rng = PCG,
sequence = (macro.seed, k)):
- octave o = k mod octaveCount (respect octaveMask); frequency magnitude per octave:
  |f| = (1/λo), λ = {1600, 600, 250, 110} m for o = 0..3, ±25% jitter.
- position: gaussian-distributed inside parent, clamped to ±`detailPosClamp`σ; snorm16-quantized
  BY THE SAME FORMULA the reader uses (quantize → dequantize in the generator when computing
  amplitude bounds, so bounds hold post-quantization).
- sigma: λo·(0.5±0.15) isotropic-ish (±30% per axis); quat random.
- freq direction: o==0 horizontal-biased (|fy| ≤ 0.3|f|), o≥1 uniform sphere.
- phase: hash01 turns.
- amplitude: octave falloff `A = macro.amplitude · 0.55^o · sign`, where sign: o<2 ⇒ +1;
  o≥2 ⇒ 50% negative (erosion). **Erosion bound (mandatory)**: if negative,
  `|A| ≤ erosionBoundK · macroDensityAt(posWS)` where macroDensityAt = macro.amplitude ·
  exp(−½‖(pos−μ)/σ‖²) — evaluate in-shader, cheap.
- shadowVisible flag: o ≤ 1.
- seed16 = 16-bit hash of (macro.seed, k).

### 4.6 CloudMacroGrid rasterize (folded into GenDetail? NO — separate tiny pass) `CloudGenGrid`
Entry `main`, 64 threads/group, one thread per macro: rasterize the macro's world AABB
(position ± boundRadius) into the 32×8×32 macro grid (cell = 1 km × 1 km × 1 km over the light-cache
extent); `InterlockedAdd` per cell count, write index if slot < 31. Grid consumers: light cache +
RT-reference shadow rays. Rebuilt on every regen (cheap: ≤8k macros × few tens of cells).

### 4.7 CloudTileBin-c.hlsl — entry `main`, ONE 64-thread group per 16×16-px tile (dispatch 80×45)
- Build tile frustum: 4 side planes from the 4 corner rays (invViewProj at tile corners, unjittered).
- Loop macros in chunks of 64 (thread t tests macro chunk+t): sphere (position+windOffset shift,
  boundRadius) vs 4 planes AND behind-camera reject.
- Survivors: groupshared compaction (prefix sum over ballot), then groupshared **bitonic sort by
  view distance** (≤64 entries), truncate at 64 (increment a "tile overflow" stat in
  macroCountBuf[2] for the debug HUD), write `tileBuf[tile] = {count, sortedIndices[64]}`.

### 4.8 CloudTrace-c.hlsl — entry `main`, **16×16 group = one bin tile**, dispatch 80×45. THE HOT PASS.
Per thread: ray from pixel (unjittered), `t1 = geometry hit distance` from scene depth (reverse-Z:
depth 0 ⇒ sky ⇒ t1 = +inf); `T = 1` (transmittance), reservoir {wsum, pick, tPick}.
Loop over `tileBuf[tile]` macros (ALL threads iterate the same list — uniform):
- Load macro to groupshared once (thread 0) + `GroupMemoryBarrierWithGroupSync` per macro batch.
- Early out: if `WaveActiveAllTrue(T < 0.005)` across the group (use groupshared vote), break.
- Integrate the macro's own Gaussian (its envelope IS cloud mass) with the clamped closed form (§5).
- Inner loop over its detail range in chunks of 64: cooperative stage
  `kernelBuf[detailBegin+chunk+lane]` into groupshared (64×32 B = 2 KB), sync, then each thread
  integrates all 64 staged kernels:
  - unpack (posWS via §3.1, quat→3 rows, etc.)
  - LOD: `lodExp = −2π²·dot(f,f)·s²(t̄)`, s(t̄) = lodParams.x · t̄ (t̄ from the Gaussian terms —
    compute a,b first, they're needed anyway). If `exp(lodExp)` would be < lodSkipThreshold
    (test `lodExp < ln(0.02) = −3.912`) ⇒ skip (octave-level fast path: if the OCTAVE's minimum |f|
    at this tile's min t̄ already fails, skip whole chunks — precomputed per tile in the bin pass is
    a Phase 5 optimization, note as knob).
  - Masking (only when `lodExp < ln(0.25)`): p = clamp(exp(lodExp)/maskAggressiveness,
    survivalFloor, 1); skip if `hash01(pixelIdx, seed16, frameIndex) > p`, else weight `w = 1/p`.
  - τ_k via §5 clamped form with the SINGLE fused exp (C6), signed by amplitude; `τ_ray += w·τ_k`.
  - Reservoir: `wr = max(τ_k, 0)·w; wsum += wr; if (rand01·wsum < wr) {pick = kernel; tPick = t̄}`.
- After the loop: `τ_ray = max(τ_ray, 0)` (erosion overshoot clamp), `T = exp(−τ_ray)`.
- Scatter (lightMode uniform branch):
  - sunCache/sixWay: sample lightCacheTex at `posWS(tPick)` (trilinear) → τ_sun; sixWay: §4.9 blend.
  - rtReference (TRACE_RQ variant only): inline RayQuery toward sun over macro BLAS + inner ranges.
  - `phase = lerp(HG(cosθ, g1), HG(cosθ, g0), hgBlend)`;
    `L_sun = sunRadiance · Σ_{k<msOctaves} 0.5^k · exp(−0.5^k·τ_sun) · phaseAt(g·0.5^k)`  (Wrenninge)
  - ambient: `L_amb = ambientStrength · lerp(groundTint, skyTopSample, heightFrac01(pick's parent))`
    (sixWay mode: modulate by exp(−τ+Y)).
  - `inscatter = (1 − T) · (L_sun + L_amb)` (single-scatter energy split; documented approximation).
- Free-flight refinement of tPick (§5 truncated sampling) for cloudDepth output only.
- Write scatterTex = (inscatter, T), cloudDepthTex = (T > 0.995 ? 0 : tPick).
- Debug views (uniform branch on mode.x): write the §A2 visualization instead.
- TDR guard: hard cap 4096 total kernel evaluations/thread (counter, break + flag pixel magenta in
  debug builds).

Variants: `TRACE_BRUTE` (Phase 1: no tiles, loop kernels 0..N linearly); `TRACE_RQ` (macro-BLAS
RayQuery instead of tile list; same inner loop; for A/B).

### 4.9 CloudLightCache-c.hlsl — entry `main`, 4×4×4 groups over the slice
Each frame processes slab `z ∈ [sliceIndex·16, ..+16)` of 128 (⇒ full refresh every 8 frames;
`genParams.w` rotates). Per voxel: march from voxel center toward `sunDirWS` through the macro grid
(3D-DDA over 1 km cells, ≤64 cell steps to exit): for each macro in each crossed cell (dedupe by
last-seen index — lists are small), accumulate the **full-domain** Gaussian integral (voxels are
never "inside geometry"; full-domain is correct here) of macro envelopes only (details omitted —
they are ~zero-mean by construction, C5/§4.3) ⇒ τ_sun. Store fp16.
Six-way mode (uniform branch): repeat for ±X, ±Y, ±Z into the two RGBA16F volumes (6× cost — only
when the toggle is on). Runtime blend: `w_i = max(0, dot(sunDir, axis_i))²`, normalized;
`T_sun = exp(−Σ w_i·τ_i)`.

### 4.10 CloudDenoise-c.hlsl — entry `main`, 8×8, 720p, two dispatches (à-trous step 1 then 2)
3×3 cross taps at stride {1,2}; weights: gaussian [1,2,1] × edge-stops
`exp(−|d_c−d_n|/(0.1·d_c+1e-3))·exp(−|T_c−T_n|/0.1)`. Ping between scatterTex and denoiseTmp
(final result must land back in scatterTex; step1: scatter→tmp, step2: tmp→scatter).

### 4.11 CloudReproject-c.hlsl — entry `main`, 8×8, 720p
- `posWS = camPos + rayDir·cloudDepth` (cloudDepth==0 ⇒ reproject as direction-only / infinite).
- Add `−windOffsetDelta` (this frame's advection delta) before reprojecting with prevViewProj.
- History fetch bilinear from historyRead; 3×3 neighborhood min/max of current scatter for clamp.
- Disocclusion: offscreen OR |T_hist − T_cur| > temporal.y ⇒ α = 1 (full current).
- Else α = temporal.x (+velocity boost: α = min(α·(1+pixelVel·8), temporal.w)).
- `out = lerp(clamp(history, nmin, nmax), current, α)` → historyWrite (color+depth), ALSO the input
  of composite this frame.
- Static-camera accumulation mode (Phase 4 validation): uniform branch — if mode.w
  `accumulate` flag set: `out = history·(n/(n+1)) + current/(n+1)` with no clamp (counter in
  temporal.z); reset on any camera/sun/param change (CPU sets a reset flag ⇒ α=1).

### 4.12 CloudComposite-c.hlsl — entry `main`, 8×8, 2560×1440
- Bilateral upsample of historyWrite-color (720p) to 1440p: 4 taps, weights by
  `exp(−|sceneDepthLin − cloudDepthTap|·0.02)` guarding silhouettes + gaussian.
- Sky: where scene depth == 0 (reverse-Z far): `hdr = skyView(dir) + sunDisc(dir)`; else keep hdr.
- Clouds: `hdr = hdr·T + inscatter` (premultiplied composite).
- In-place read-modify-write of the deferred HDR output via typed UAV u12 (requires the typed-UAV
  load cap, checked in Phase 0).
- Debug views that need full-res (heatmaps etc.) overwrite hdr here (uniform branch).

### 4.13 RTScene (RayQuery A/B only; may be skipped if RT tier < 1.1 — everything else must not care)
Macro-AABB BLAS: `D3D12_RAYTRACING_GEOMETRY_TYPE_PROCEDURAL_PRIMITIVE_AABBS`, AABB buffer written by
a 10-line addition to CloudGenGrid (macro AABBs, NaN-min for inactive tail slots — the spec-blessed
inactive marker), flags `PREFER_FAST_TRACE | ALLOW_UPDATE`. Single-instance TLAS, identity.
Barrier chain: gen UAV barrier → AABB buf UAV→NON_PIXEL_SHADER_RESOURCE → BuildRaytracingAS on
`GetCommandList4()` → UAV barrier → trace. AS buffers: committed, `RAYTRACING_ACCELERATION_STRUCTURE`
state forever, self-owned; scratch: committed self-owned (sized once at c_maxMacros). Rebuild only on
regen (not per frame). TLAS bound as root SRV (param 3).

---

## 5. Math reference (HLSL-ready)

All in `CloudKernels.hlsli`. Validated by `tools/validate_math.py` against numpy quadrature — do not
change one without the other.

```hlsl
// erf — Abramowitz–Stegun 7.1.26, max abs err 1.5e-7
float erf_as(float x) {
    float s = x < 0 ? -1 : 1; x = abs(x);
    float t = 1.0 / (1.0 + 0.3275911 * x);
    float p = t*(0.254829592 + t*(-0.284496736 + t*(1.421413741 + t*(-1.453152027 + t*1.061405429))));
    return s * (1.0 - p * exp(-x*x));
}
// erfinv — Giles 2010, both branches; coefficient tables are authoritative in
// tools/validate_math.py (function erfinv_giles) — COPY them from there verbatim.
```
(The implementing agent MUST copy the coefficient tables from `tools/validate_math.py`, which holds
the authoritative Giles coefficients; the snippet above is a placeholder marker, not the table.)

**Kernel-space setup** (kernel μ, rotation matrix R from quat, σ per axis, ray o+t·d, d normalized):
`p = (R·(o−μ))/σ` (component-wise), `q = (R·d)/σ`, `a = dot(q,q)`, `b = dot(p,q)`, `c = dot(p,p)`,
`t̄ = −b/a` (guard `a > 1e-8`).

**Clamped optical depth** over [t0, t1] (t0 = max(RayTMin,0); t1 = min(tGeom, BIG)).
Density along the ray is `σ0·exp(−½(a·t² + 2b·t + c))`; completing the square gives, with
`bracket = erf(sqrt(a/2)·(t1−t̄)) − erf(sqrt(a/2)·(t0−t̄))` (∈ [0,2]):
```
envExp   = −0.5·(c − b²/a)                        // ≤ 0 always; clamp sum of exponents ≥ −20
τ_gauss  = amp · sqrt(2π/a) · 0.5 · exp(envExp) · bracket            // freq == 0
gaborExp = −ω²/(2a);  ω = 2π·dot(f_ws, d);  ψ = 2π·dot(f_ws, o−μ) + 2π·phaseTurns + windPhase(octave)
τ_gabor  = amp · sqrt(2π/a) · 0.5 · exp(envExp + gaborExp) · cos(ω·t̄ + ψ) · bracket   // freq ≠ 0
           // exact when the kernel is fully inside [t0,t1]; for partial clamps the bracket scaling
           // is an approximation — validate_math.py measures ≤ ~0.3 of the kernel's ray mass in the
           // worst case (kernel cut mid-oscillation by geometry/camera). Accepted: it only occurs at
           // cloud/geometry (or camera) intersections, errs on smooth density, and the Gaussian
           // (freq==0) term — the mass carrier — is exact under clamping.
LOD (C6 fusion): factor = exp(envExp + gaborExp + lodExp) — ONE exp; multiply cos & bracket after.
```
Full-domain fast path (bracket = 2) allowed only when `tGeom == +inf` AND `t̄ − 3/sqrt(a/2)·? > t0`
— concretely: sky pixel and `t̄ − 3·sqrt(2/a) > 0` (kernel's ±3σ ray-support fully in front of the
camera). (validate_math.py checks the eligibility predicate too.)

**Truncated free-flight** within [t0,t1] for the reservoir winner:
`e0 = erf(sqrt(a/2)·(t0−t̄)); e1 = erf(sqrt(a/2)·(t1−t̄)); u = lerp(e0, e1, ξ);
 t = t̄ + sqrt(2/a)·erfinv(u)` (ξ = blue-noise scalar).

**HG phase**: `HG(c,g) = (1−g²) / (4π·pow(1 + g² − 2·g·c, 1.5))`, c = dot(rayDir, sunDir).

**Hashes**: PCG3D (Jarzynski & Olano) for all hash01 uses; seed inputs stated per call site in §4.

---

## 6. Engine integration map (exact contracts)

All anchors verified at seed commit `32d538e` (= toy-renderer `c15a3425`).

1. **`src/renderer/includes/Awesome.h`**
   - forward decls near anchor `"class GTAO;"`: add `class CloudSystem;`
   - member near anchor `"GTAO* m_gtao"`: add `CloudSystem* m_clouds;`
   - getter near anchor `"GetGTAO"` (or the getter cluster): `CloudSystem* GetClouds() { return m_clouds; }`
   - Device accessors: `ID3D12Device5* Device5()`, `ID3D12GraphicsCommandList4* GetCommandList4()`
     (backed by QI'd members; see step P0.3).
2. **`src/renderer/src/Awesome.cpp`**
   - ctor init-list anchor `"m_gtao"`: append `, m_clouds(new CloudSystem(this))`.
   - `StartUp` anchor `"m_gtao->StartUp"`: after it, `if (!m_clouds->StartUp()) return false;`
   - `TearDown` anchor `"m_gtao->TearDown"`: add clouds teardown + `delete`.
   - `Render` anchor `"Deferred Lighting"`: AFTER that scope's closing brace and BEFORE
     `m_scene->GetCamera()->EndFrame()`, insert:
     ```cpp
     {
         PROFILE_SCOPE(GetProfiler(), GetCommandList(), "Clouds", scopeIdx++);
         m_clouds->Render(delta);
     }
     ```
   - Device creation anchor `"D3D12CreateDevice"`: after success, QI Device5 (failure ⇒
     `m_rtSupported = false`, not fatal), `CheckFeatureSupport` OPTIONS5 (RaytracingTier),
     SHADER_MODEL (require ≥ 6.5), FORMAT_SUPPORT for R11G11B10 typed UAV load
     (`D3D12_FORMAT_SUPPORT2_UAV_TYPED_LOAD`; failure ⇒ composite falls back to a copy-based
     non-in-place path — implement the fallback ONLY if the user reports the cap missing; assume
     present on the 4070 target).
   - Per-frame command list creation anchor `"CreateCommandList"`: QI each to CommandList4 (store
     array; may be null when RT unsupported — RQ paths check).
3. **`CloudSystem` owns and orders sub-objects**: `SkyAtmosphere`, `CloudGenerator`, `CloudLighting`,
   `RTScene` (optional), and the trace/denoise/reproject/composite passes. Only `CloudSystem` touches
   `AwesomeGraphics`. Internal call order per frame = frame graph §2.
4. **Shaders** compile automatically: any `src/renderer/**-c.hlsl` becomes
   `bin/<cfg>/shaders/<name>.cso` at SM 6.8 (premake filter anchor `"files:**-c.hlsl"` in
   `src/renderer/premake5.lua`). Load via `ComputeSystem::CompileShader(L"<Name>-c", perms)` +
   `CreatePipeline` (pattern anchor `"CompileShader"` in `GTAO.cpp`).
5. **ImGui panel**: `src/app/src/Client.cpp`, anchor `"GTAO"` in the settings window — add a
   collapsed "Clouds" section after it reading/writing `g_Awesome->GetClouds()` public members
   (§A1). RT caps readout in the same panel.
6. **TAA sky fix** (Phase 5): `src/renderer/shaders/TAA-c.hlsl` anchor `"motion"` — where motion is
   fetched, add: if scene depth == 0, compute camera-only reprojection from current/prev view-proj
   (both already available? if prevViewProj is not in TAA's CB, add it to TAAConstants — anchor
   `"TAAConstants"` in `TAA.cpp`/`TAA.h`).
7. **Aftermath opt-in** (P0.1): `src/app/premake5.lua` anchors `"aftermath"` (libdirs) and
   `"GFSDK_Aftermath_Lib"` (links) — wrap both in `if _OPTIONS["aftermath"] then ... end`; root
   `premake5.lua`: add the `newoption` block at top. `src/renderer/premake5.lua` anchor
   `"aftermath/include"` — gate the same way. C++ already guards via `USE_NSIGHT_AFTERMATH` (leave).
8. **DXC runtime DLLs** (P0.4): app premake postbuild — copy `dxcompiler.dll` + `dxil.dll` from the
   NuGet package path `packages/Microsoft.Direct3D.DXC.1.8.2502.8/build/native/bin/x64/` (mirror
   the assimp copy lines, anchor `"assimp-vc143"`). `CloudShaderCompiler` loads them at runtime
   (`LoadLibrary` + `DxcCreateInstance`), Debug builds only (`#ifdef DEBUG`), falls back silently to
   `.cso` when unavailable.

---

## 7. Phase plan — atomic steps

Rules: each step ends committed+pushed (§0.2). "SHADER-CHECK" = `tools/check_shaders.sh` green.
"MATH-CHECK" = `tools/validate_math.py` green. Steps within a phase are ordered; do not parallelize
steps that touch the same files.

### Phase 0 — hygiene & device (partially done)
- [x] **P0.0** Seed repo from toy-renderer @ c15a3425 (done: commit `32d538e`, includes HTTPS
  .gitmodules + submodule gitlinks).
- [ ] **P0.1** This spec: `docs/PLAN.md` + `docs/STATUS.md` + `tools/check_shaders.sh` +
  `tools/validate_math.py` (with authoritative erf/erfinv/integral references + quadrature tests) +
  README build notes (submodule init, `--aftermath` option, no-Aftermath default).
  Acceptance: MATH-CHECK green in container.
- [ ] **P0.2** Aftermath opt-in + DXC DLL postbuild copy (§6.7, §6.8). Acceptance: `premake5
  --version`-level Lua sanity — run `premake5 vs2022` if the container has premake, else careful
  review; grep shows no unconditional `GFSDK_Aftermath_Lib` link.
- [ ] **P0.3** Device5/CommandList4 QI + `CheckFeatureSupport` (RT tier, SM, typed UAV load) +
  `m_rtSupported`/`m_typedUAVLoads` flags + getters (§6.2) + ImGui caps readout (§6.5).
  Acceptance: grep contracts; USER: builds & shows caps.
- [ ] **P0.4** `CloudShaderCompiler.{h,cpp}` (IDxcCompiler3 hot reload, Debug-only) + ImGui
  "Reload cloud shaders" button calling `FlushGPU()` + PSO rebuild hook (no cloud PSOs exist yet —
  wire the mechanism with an empty list). Acceptance: grep; USER build.

### Phase 1 — sky + scaffold + brute-force (validates the math on screen)
- [ ] **P1.1** `CloudCommon.hlsli` (CloudConstants HLSL mirror, ray setup helpers, PCG3D) +
  `CloudKernels.hlsli` (§5 complete, erf/erfinv tables copied from validate_math.py) + SHADER-CHECK
  scaffolding (a tiny `CloudNull-c.hlsl` including both, deleted in P1.3). MATH-CHECK + SHADER-CHECK.
- [ ] **P1.2** `CloudSystem.{h,cpp}` skeleton: root signature (§3.5), cloudCB[3] persistent-mapped,
  descriptor blocks from Assets section (extend `DescriptorHeap::AllocateBlock` with a section
  parameter if it lacks one — keep default behavior identical), empty Render. Wire into
  AwesomeGraphics (§6.1–6.2). Acceptance: grep all 5 wiring points + descriptor math comment.
- [ ] **P1.3** `SkyAtmosphere.{h,cpp}` + `SkyLUT-c.hlsl` (§4.1); composite-lite: temporary
  `CloudComposite-c.hlsl` that ONLY paints sky+sun where depth==0 (no clouds yet), wired as the last
  cloud dispatch. Sun/IBL unification: time-of-day member drives `Scene::GetSunLight()` direction
  (anchor `"GetSunLight"` in `Scene.h`/`Scene.cpp`); sky-view→cubemap→IBL refresh is DEFERRED to
  P4.4 (keep a TODO). SHADER-CHECK. USER: sky renders, sun moves with slider.
- [ ] **P1.4** Brute-force clouds: `CloudTrace-c.hlsl` with `TRACE_BRUTE` define (§4.8 variant),
  64–256 hand-placed kernels uploaded once from CPU (hardcoded cumulus cluster in
  `CloudGenerator.cpp` CPU path), full-res dispatch, composite blends clouds (§4.12 full version).
  Debug views: analytic-vs-march diff (view #5), transmittance (#3). SHADER-CHECK.
  USER: blobs in sky; diff view near-black including camera-inside-cloud.

### Phase 2 — tiled traversal (+ RQ A/B)
- [ ] **P2.1** Buffers/persistent resources (§3.3) in `CloudGenerator.{h,cpp}` (CPU-upload path
  still), tileBuf, `CloudTileBin-c.hlsl` (§4.7). SHADER-CHECK.
- [ ] **P2.2** `CloudTrace-c.hlsl` tiled path (§4.8) at full res (keep it simple; half-res arrives
  in P5). Toggle tiled vs brute (traversalMode uniform). Debug: visited/surviving heatmaps (#1).
  Acceptance: SHADER-CHECK. USER: identical image tiled vs brute; heatmap sane; first ms numbers.
- [ ] **P2.3** `RTScene.{h,cpp}` + `TRACE_RQ` variant (§4.13). Guard on `m_rtSupported`.
  SHADER-CHECK. USER: 3-way parity + A/B timings.
### Phase 3 — procedural generation + wind
- [ ] **P3.1** `CloudWeather-c.hlsl` + `CloudGenMacro-c.hlsl` + `CloudGenScan-c.hlsl` +
  `CloudGenDetail-c.hlsl` + `CloudGenGrid` (§4.2–4.6), regen-on-dirty orchestration in
  CloudGenerator, kernel-count readback (regen only), L2-budget ImGui readout. SHADER-CHECK.
  USER: procedural cloudscape, Regenerate button, count readout.
- [ ] **P3.2** Wind: windOffset accumulation (CPU, delta·speed·dir), per-octave windPhaseVel into ψ
  (§5), advection delta into reprojection constants (used in P5). USER: drifting, churning clouds.
### Phase 4 — lighting + accumulation
- [ ] **P4.1** Static-camera accumulation mode (§4.11 uniform branch; CPU reset detection).
- [ ] **P4.2** `CloudLighting.{h,cpp}` + `CloudLightCache-c.hlsl` sun channel (§4.9) + trace consumes
  cache (lightMode 0). SHADER-CHECK. USER: shadowed cloud field, noon + sunset screenshots.
- [ ] **P4.3** Six-way variant (lightMode 1) + RT-reference sun rays (lightMode 2, `TRACE_RQ` only)
  + baked-vs-reference diff view (#6). USER: 3-mode comparison, converged.
- [ ] **P4.4** Sky-view→cubemap→`IBLProcess`/`IBLIrradiance` refresh on time-of-day change (anchor
  `"IBLProcess"` in `IBL.cpp`); exposure clamp slider. USER: geometry lighting tracks sunset.
### Phase 5 — LOD, masking, half-res, temporal → the verdict
- [ ] **P5.1** LOD + bounded masking in trace (§4.8), debug views #7 (masking survival), #9 (min-τ).
- [ ] **P5.2** Half-res trace (1280×720) + `CloudDenoise-c.hlsl` + `CloudReproject-c.hlsl` +
  composite upsample (§4.10–4.12). historyTex ping-pong. USER: stable flythrough.
- [ ] **P5.3** TAA sky fix (§6.6). USER: no sky shimmer under rotation.
- [ ] **P5.4** Perf pass: knob defaults tuned via user-reported profiler numbers; acceptance-ladder
  verdict recorded in STATUS.md + README table (§9). USER: the ≤1 ms (or documented ladder-C) result.
### Phase 6 — stretch (each optional, ordered by fun/value)
- [ ] **P6.1** In-register kernel synthesis experiment (regenerate detail kernels from macro seed in
  the trace loop; compare ms + image vs stored kernels).
- [ ] **P6.2** Canonical 6D kernel transfer table (per reservoir winner only).
- [ ] **P6.3** VDB→Gabor fitting tool (`tools/vdb_fit/`, Python, runs in container; Disney cloud).
- [ ] **P6.4** Agility preview bump (SM 6.9 / DXR 1.2 SER/OMM), work-graph generation.

---

## 8. Verification harness

- `tools/check_shaders.sh`: compiles every `src/renderer/shaders/Cloud*.hlsl` + `SkyLUT-c.hlsl`
  entry with DXC (`-T cs_6_8 -E main -I src/renderer/shaders -Wno-ignored-attributes`), each
  `#define` variant (`TRACE_BRUTE`, `TRACE_RQ`). Uses `dxc` from PATH or `tools/dxc/bin/dxc`;
  if unavailable, prints a LOUD warning and exits 0 (Windows build remains authoritative).
- `tools/validate_math.py` (numpy): authoritative erf (A&S 7.1.26) and erfinv (Giles 2010, both
  branches, full coefficient tables) reference implementations + tests: (1) clamped Gaussian
  integral vs `scipy`-free trapezoid quadrature (10k random kernels/rays, rel err < 1e-3);
  (2) Gabor integral likewise; (3) truncated free-flight sampling: KS-style check that sampled t
  histogram matches the truncated density; (4) full-domain fast-path eligibility logic.
- Struct layout: `static_assert` in headers (Windows compile enforces); container-side
  `tools/validate_math.py --layout` recomputes §3.1/3.2 offsets and prints them for review.
- Every USER acceptance step: the exact thing to look at is written in the step (§7).

## 9. Budget & acceptance ladder (RTX 4070, 2560×1440, Release, vsync off)

| pass | res | budget |
|---|---|---|
| bin+sort | 80×45 tiles | 0.02–0.04 ms |
| light-cache slice | 1/8 volume | 0.03–0.08 ms |
| trace (tiled) | 1280×720 | 0.35–0.60 ms |
| denoise ×2 | 720p | 0.05–0.10 ms |
| reproject | 720p | ~0.08 ms |
| composite+sky | 1440p | ~0.08 ms |
| **total** | | **0.68–1.05 ms** |

Ladder: **A** ≤1.0 ms full quality; **B** ≤1.0 ms after knobs (masking↑, tile macro cap, octaves
4→3, kernels/macro↓, quarter-res+checkerboard, msOctaves 3→2, bounds 3σ→2.5σ); **C** documented
quality mode ≤2.5 ms + writeup. Reference/RQ modes exempt.

## Appendix A1 — ImGui panel (public members on CloudSystem, GTAO-style)
Time of day (0–24 h), turbidity, sun intensity; coverage, cloud type, seed, octaves (1–4),
kernels/macro/octave, Regenerate; wind speed/dir, per-octave phase rates; traversal mode combo,
lighting mode combo + cache-convergence bar; quality: masking aggressiveness, survival floor,
tile macro cap, temporal α, accumulation toggle; readouts: kernel count + MB of L2 budget, tile
overflow count, RT caps; debug view combo (§A2); hot reload button.

## Appendix A2 — debugView enum
0 off · 1 visited/surviving heatmap (inferno; visited in R ramp, surviving iso-lines) ·
2 frequency bands as RGB (octave contributions) · 3 transmittance · 4 cloud depth ·
5 analytic-vs-march diff ×32 · 6 baked-vs-reference lighting diff ×8 · 7 masking survival rate ·
8 history rejection mask · 9 min-ray-τ (erosion overshoot, blue=negative) · 10 tile macro count ·
11 light-cache slice (Y-slab viewer).

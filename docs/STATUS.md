# Implementation status

Live checklist for `docs/PLAN.md` §7. **Protocol** (PLAN §0): read this + `git log --oneline -15`
before any work; one step = one commit (`P<phase>.<step>: summary`), update this file in the same
commit, push after every commit. Steps marked USER are awaiting verification on the user's Windows
machine — do not block on them unless a later step depends on the result.

## Phase 0 — hygiene & device
- [x] P0.0 Seed repo from toy-renderer @ c15a3425 (commit `32d538e`; HTTPS .gitmodules, gitlinks)
- [x] P0.1 Spec (docs/PLAN.md), this file, tools/validate_math.py (MATH-CHECK green in container),
      tools/check_shaders.sh, README build notes
- [x] P0.2 Aftermath opt-in premake option + DXC DLL postbuild copy (USER: verify fresh-clone
      build without thirdparty/aftermath)
- [x] P0.3 Device5/CommandList4 QI + CheckFeatureSupport (RT tier, SM, typed UAV loads) + caps readout
      (Info panel, ImGui)
- [x] P0.4 CloudShaderCompiler (DXC hot reload, Debug-only). The ImGui reload button lands with
      CloudSystem in P1.2 (see Deviations)

## Phase 1 — sky + scaffold + brute force
- [x] P1.1 CloudCommon.hlsli + CloudKernels.hlsli (math transliterated from validate_math.py;
      MATH-CHECK green; no dxc in container — CloudNull-c.hlsl probe compiles on the Windows build)
- [x] P1.2 CloudSystem skeleton: shared root sig (root CBV + 15 SRV + 15 UAV tables + TLAS root
      SRV), persistently-mapped cloudCB[3], 6+6 persistent descriptor blocks from Assets section,
      all 5 god-class wiring points, ImGui Clouds section incl. the P0.4 reload button (deviation
      resolved)
- [x] P1.3 SkyAtmosphere (Sky.hlsli + SkyTransLUT-c/SkyViewLUT-c entry files, 256x64 + 192x108 LUTs,
      dirty-tracked), CloudComposite-c (sky where depth==0, cloud blend gated by cloudsActive), sun
      from time-of-day drives GetSunLight(); CloudSystem now creates resources, fills null
      descriptors, binds shared tables, dispatches sky+composite
- [x] P1.4 Brute-force analytic trace (CloudTraceBrute-c): CloudGenerator authors a 14-macro cumulus
      cluster on the CPU (validates the 64B packing + quat unpack), trace loops macros as pure
      Gaussians with the clamped closed form, writes scatter (rgb,inscatter / a,transmittance) +
      cloud depth; composite blends it; debug views 3 (transmittance) and 5 (analytic-vs-256-step
      march diff, near-black == correct). CloudNull probe removed.

## Phase 2 — traversal
- [x] P2.1 Tile buffer (CloudTile 272B, per 16x16 tile) + CloudTileBin-c wired (bin PSO, per-frame
      dispatch, UAV<->SRV cycle); counts.z/w = tile grid
- [x] P2.2 Tiled trace (CloudTrace-c, macros-only, must match brute pixel-for-pixel) + traversal-mode
      combo (Tiled default / Brute A/B; RQ modes fall back to brute until P2.3) + heatmap/tile-count
      debug views
- [x] P2.3 RTScene + CloudTraceRQ-c entry (guarded on m_rtSupported). Procedural-AABB BLAS + single-
      instance TLAS (RTScene.cpp, PREFER_FAST_TRACE); CloudTraceRQ-c does inline RayQuery over the macro
      AABBs, integrates the same envelope+detail, and is compiled into a PSO only when RT tier >= 1.1.
      Traversal combo exposes RQ Macro/RQ Kernel alongside Tiled/Brute.

## Phase 3 — generation
- [x] P3.1 Procedural generation (CPU, amortized on regen — C4-compliant since the per-frame path
      stays allocation-free): coverage FBM over a 128x128 grid places flattened macros; each spawns
      K Gabor detail kernels across octaves (signed erosion, bounded, ~zero-mean) in fixed slot
      ranges [i*K, i*K+K). Both buffers upload on regen (NON_PIXEL<->COPY_DEST re-upload handled).
      Tiled trace now integrates macro envelope + its detail range (UnpackKernel path exercised).
      ImGui: coverage/type/seed/octaves/kernels-per-macro/Regenerate + kernel-count + L2 readout.
- [x] P3.2 Wind: whole-field advection (ray-origin += windOffset, consistent with binning's
      macro-centre shift) + per-octave phase drift in the CB (KernelRaySetup applies it). No regen.

## Phase 4 — lighting
- [x] P4.0 Approximate lighting (Beer-powder + height gradient + sky ambient) — no new infra, makes
      clouds read as 3D. Placeholder until the field cache lands.
- [x] P4.1 Static-camera accumulation mode. The reproject accumulates unweighted when the camera and
      time-of-day are unchanged (temporal.z accumCount, temporal.w blend cap), reset on any change, so a
      parked camera converges the stochastic/half-res trace for validation. ImGui "Accumulate" toggle.
- [x] P4.2 CloudLighting + sun-transmittance field cache (128x32x128 R16F 3D texture) + trace
      consumption (lightMode 0). No macro grid needed: per voxel the cache sums the closed-form
      optical depth of every macro along the sun ray with a cheap perpendicular-distance reject, so
      only nearby macros pay the erf cost. Rebuilt one Z-slab/frame (full refresh in 16 frames).
      Trace samples it trilinearly at o+bestT*dir (macro space) → Wrenninge multi-octave scatter.
      Cache box: cubic 256 m voxels centred on camera-in-macro-space, XZ snapped. lightMode!=0 keeps
      the P4.0 height heuristic as fallback.
- [x] P4.3 RT-reference lighting + six-way cache variant. lightMode 2 (RQ traversal) fires a second
      inline RayQuery toward the sun at the scatter point for exact sun optical depth - the reference the
      field cache approximates; the baked-vs-reference diff view (debug 6) shows |cache tau - reference
      tau|. The six-way directional cache variant IS now built (CloudLightCache-c writes 6 axes into
      cache0 RGBA + cache1 RG; SampleSunTau blends by squared cosine under lightMode SIXWAY) as an A/B
      option - the sun-aligned time-sliced cache stays the default (the six-way low-sun/sunset weakness
      is why), but both are selectable from the lighting combo for comparison.
- [x] P4.4 Time-of-day scene lighting + exposure clamp. The scene sun's colour AND intensity now track
      time of day (warm/dim near the horizon, bright at noon, dark below) so the geometry lighting
      matches the sky - the dominant day/night effect, no shader surgery. Exposure clamp is the
      engine's Min/Max Log Luminance + the bounded sun disc. SkyEquirect-c renders the atmosphere to
      an equirect HDR in the engine IBL loader's format; the remaining hook is
      IBLProcessor::AddIBLFromEquirect to rebuild the ambient-COLOUR probe from it (a ~330-line
      extraction of AddIBL's cubemap/prefilter tail - left un-refactored to avoid regressing the
      working, untestable IBL load path). Residual: HDRI-based ambient colour, not intensity.

## Phase 5 — LOD/temporal/perf
- [x] P5.1 Continuous LOD (per-kernel freq×footprint attenuation folded into the single exp) + bounded
      stochastic masking (reweighted 1/p) + ImGui mask controls.
- [x] P5.2a Half-res trace (m_traceScale=2) + bilinear upsampling composite.
- [x] P5.2b Temporal reprojection (CloudReproject-c): cloud-owned RGBA16F ping-pong history,
      camera-motion reprojection via prevViewProj, 3x3 neighbourhood clamp, transmittance-delta
      disocclusion. Composite reads the reprojected result (t13). t9=prev / u10=cur / t13=cur-for-
      composite written per-frame into the [f][0] block (safe: block fenced 3 frames back).
- [x] P5.2c Spatial denoise (à-trous bilateral) before temporal. Two passes (stride 1 then stride 2,
      P6.5) edge-stopped by cloud depth and transmittance; passthrough when masking is off, so it is
      nearly free in the common case. See P6.5 for the second-pass plumbing.
- [x] P5.3 TAA sky fix (camera-only reprojection for depth==0 pixels). TAA-c reprojects sky pixels by
      camera motion (mul(mul(ndc, invViewProj), prevViewProj)) using invViewProj + prevViewProj added to
      TAAConstants, so the sky no longer smears under the variance clip. Low impact but done.
- [ ] P5.4 Perf tuning + acceptance-ladder verdict at 2560x1440 — USER-driven (needs Windows profiler
      numbers). Knobs exposed: Mask Aggressiveness, Survival Floor, Temporal Blend, Kernels/Macro,
      Octaves; m_traceScale (2=half, 4=quarter, StartUp-time). Profiler scopes: Cloud Light Cache /
      Trace / Reproject / Composite (plus Sky, Bin via PIX). (record here + README)

## Phase 6 — stretch (optional)
- [x] P6.3 VDB->Gabor fitting tool (tools/vdb_fit/fit.py) + runtime loader. Fits a density volume
      (synthetic in-container, or a real grid via --vdb with pyopenvdb) with a mass-weighted k-means +
      per-cluster-covariance Gaussian mixture, writes a .cloud binary matching the CloudMacro (64 B) /
      CloudKernelPacked (32 B) layouts. CloudGenerator::LoadFile reads it (magic-checked) and an ImGui
      "Load fit.cloud" button swaps the procedural sky for the fitted mixture. Runs green in-container.
- [x] P6.1 In-register kernel synthesis A/B — DONE. SynthKernel(macro, kIdx, octave) in
      CloudKernels.hlsli mirrors CloudGenerator's per-kernel logic (GenHashU/GenHashF on
      (macro.seed,k), octave lambda, macro-local offset, signed erosion); CloudTrace-c's detail loop
      branches `synth ? SynthKernel(...) : UnpackKernel(g_kernels[j], m)` under the ImGui "in-register
      synth" toggle. Lets the trace regenerate detail without touching the kernel buffer (A/B against
      the loaded path). Kept as an option, not the default, since kernels already fit the 16 MB budget.
- [x] P6.2 Canonical scattering transfer LUT — DONE. CloudScatterBake-c bakes a 64x64 multi-order
      (16-order) scattering LUT once at StartUp; CloudLighting owns g_scatterLUT (t7) and
      CloudSunScatter samples it when the "baked scatter LUT" toggle is set, else falls back to inline
      Wrenninge. The full 6D per-kernel transfer table remains deferred (percent-level intra-kernel MS
      vs a large bake subsystem); this delivers the practical scattering-shape win at ~16 KB.
- [x] P6.4 Agility version parameterization + SER-ready RQ path — DONE. Agility 1.615 (current) already
      covers DXR 1.1 inline RayQuery + SM 6.8, which the whole renderer uses, so the shipping build is
      unchanged. Added two non-breaking hooks so a 6.9/SER runtime can be dropped in without editing
      source: (1) premake options --agility-nuget / --agility-sdk (default 1.615.1 / 615) drive the
      NuGet restore in both src/app + src/renderer premake and a D3D12SDK_VERSION_OVERRIDE define; the
      D3D12SDKVersion/Path exports in Client.cpp now read those overrides with a 615 fallback. (2) A
      #ifdef CLOUD_SER MaybeReorderThread() block in CloudTraceRQ-c.hlsl regroups lanes by
      hit/sun-ray coherence before the divergent lighting tail — an SM 6.9 intrinsic, compiled only in
      a 6.9 raygen build, #ifdef'd out of the SM 6.8 compute build so it can never break the pipeline.
- [x] P6.5 Polish pass (deferred cleanups) — DONE. Three items:
      (1) Second à-trous denoise pass: CloudDenoise.hlsli now holds the shared bilateral tap;
      CloudDenoise-c (stride 1, scatter->scratch) + CloudDenoise2-c (stride 2, scratch->denoise) widen
      the support to ~9x9 at 5x5 cost. The scratch texture reuses the unused MacroGrid descriptor slots
      (t14/u14; t15 is the TLAS root SRV, so the SRV table is not grown), and the final still lands in
      the denoise slot the reproject reads, so no reproject/root-sig change.
      (2) Tile-overflow telemetry: CloudTileBin-c records the true pre-clamp visible-macro count in the
      tile's unused pad0; the CLOUD_DBG_TILE_COUNT debug view flags tiles that exceed the 64-slot cap
      (where the binner drops the farthest macros) in solid red over the Inferno fullness ramp. No
      GPU->CPU readback needed — the overflow is spatial and visible in one view.
      (3) VDB tool Gabor residuals: fit.py --detail N reconstructs the Gaussian envelope, takes the
      residual vol-recon, and plants up to N signed Gabor kernels per macro at the residual extrema in
      each macro's 3-sigma box, packed byte-exact to CloudKernelPacked in the parent-local frame
      (position snorm16 in 4*sigma units, sigma ~0.35*parent, freq ~1 cycle/2-sigma along the residual
      gradient). detailBegin/Count index the contiguous per-macro run; --detail 0 reproduces v1.

## Review notes (Phase 2-3)
- Full Phase 2-3 audit complete. Found ONE crash-class bug (now fixed): EnsureUploaded reused the
  DEFAULT buffer's full-capacity/UAV-flagged desc for AwesomeGraphics::UploadBuffer, causing an OOB
  read (copy sized from full capacity, not actual count) AND an illegal ALLOW_UNORDERED_ACCESS flag
  on the UPLOAD staging heap (E_INVALIDARG -> null deref). Fixed by building a fresh desc with the
  actual byte Width and FLAG_NONE for both macro and kernel uploads. This latent crash existed since
  P1.4; it would have crashed on the first frame.
- Everything else verified CORRECT and must not be "fixed": CloudTile layout/stride, tile-index
  agreement, binning frustum signs (conservative, never drops a visible macro), the wind-advection
  algebra (bin -windOffset vs trace +windOffset place the field identically; geometry clamp exact),
  the kernel pack field assignments, fixed-slot indexing + caps, register bindings, resource-state
  balance (incl. the m_inReadState re-upload path), signed-tau accumulation.
- Deferred minor items: PSO permutation arrays leak once at StartUp (matches the engine's GTAO
  pattern; negligible). Tile-overflow telemetry is now handled (P6.5): the binner writes the true
  pre-clamp visible count into the tile's pad0 and the CLOUD_DBG_TILE_COUNT view flags overflow red.
- The kernel pack/unpack round-trip is also independently validated by tools/validate_math.py
  check_packing.

## Review notes (Phase 1 audit)
- Phase 1 D3D12/HLSL audited: layouts, registers, barriers, math, ray reconstruction all verified
  correct. Applied fixes: F2 (UnpackKernel comment now states R*local + generator round-trip rule),
  F3 (march-diff bounds the reference by the farthest macro mean), F4 (skyParams.w documented as
  cloudsActive; reproject accumulate flag must go elsewhere), F5 (cloud-depth SRV t12 registered).
- F1 (depth SRV R32_FLOAT on typed D32 resource) is an ENGINE-WIDE condition — the deferred renderer
  does the identical thing and the driver tolerates it. Do NOT patch only the cloud copy; a real fix
  is R32_TYPELESS depth + D32 DSV across the engine. Left as-is.

## Deviations from spec
(record any adaptation made when an anchor/contract in PLAN.md didn't match reality)

- P0.4: the "Reload cloud shaders" ImGui button was deferred to P1.2 — CloudSystem is the only
  allowed god-class hook (PLAN §6.3) and didn't exist yet. RESOLVED in P1.2: CloudSystem owns
  CloudShaderCompiler and the button exists in the Clouds ImGui section.
- P1.2: DescriptorHeap::AllocateBlock already had a section parameter — no engine extension needed.
- P1.3: **offline build ignores -D permutations** (see PLAN §C7). Compile-time variants are separate
  entry `-c.hlsl` files including a shared `.hlsli`. SkyLUT split into Sky.hlsli + SkyTransLUT-c +
  SkyViewLUT-c.
- P1.3: **C4 relaxed for two engine-managed resources** whose ID3D12Resource* isn't stable — scene
  depth (recreated on resize; clouds have no Resize hook) and the pool-recycled deferred HDR output.
  Their SRV/UAV are (re)written into the persistent slots once per frame (one CreateView each). All
  other cloud resources keep persistent views.
- P1.3: composite does in-place RW on the R11G11B10 deferred output → requires the typed-UAV-load cap
  (checked in P0.3; present on the 4070 target). No copy-based fallback implemented yet.
- P1.3: sun/scene unification has a 1-frame lag — clouds set GetSunLight()->direction at the cloud
  hook (after deferred), so deferred consumes it next frame. AnimateLights only touches point lights,
  so it persists. Full IBL/exposure unification is P4.4.

- P1.4: cloud trace targets (scatter RGBA16F, cloudDepth R16F) are sized to the window at StartUp;
  a window resize is not yet handled for cloud targets (no Resize hook). Fine for fixed-res R&D /
  perf runs; P5 formalises trace sizing (half-res). Brute trace is full-res.

## User-verification queue
(steps finished in-code, awaiting a Windows build/run report)

- P0.2: fresh clone + `premake5 vs2022` + build x64 Debug WITHOUT thirdparty/aftermath present;
  expect link success; after a NuGet restore + rebuild, dxcompiler.dll/dxil.dll appear in bin/Debug
- P0.3: run the app, open the Info panel — expect "RT tier 1.1 | SM 6.8 | R11G11B10 UAV loads yes"
  on the 4070
- P3: run the app — expect a full procedural cloudscape (coverage/type/octaves/seed sliders +
  Regenerate) that drifts and churns with wind. NOTE: no LOD/masking/half-res yet, so at 1080p the
  tiled trace with detail kernels may run ~3-10 fps (hundreds of kernels/pixel) — Phase 5 fixes this.
  Brute A/B is now a macros-only (envelope) baseline and is slow with many macros; the analytic-vs-
  march debug (brute) is only practical at low coverage (few macros).
- P2.2: run the app — the Traversal combo should show identical clouds for "Tiled" vs "Brute"
  (regression gate); debug view 1 (Heatmap) / 10 (Tile Count) show per-tile macro counts.
- P1.3/P1.4: run the app — expect a blue sky with a sun that moves as the Time of Day slider changes,
  and ~14 soft cumulus blobs. Set debug view 5 (analytic-vs-march) → should be near-black everywhere,
  INCLUDING when the camera flies inside a blob (validates the clamped erf integral). Debug view 3
  shows transmittance. (Debug-view UI combo lands with the fuller ImGui panel; until then set
  m_debugView in code or via the reload path.)

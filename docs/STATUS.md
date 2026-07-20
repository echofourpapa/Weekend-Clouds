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
- [ ] P2.1 Persistent buffers + CloudTileBin-c.hlsl
- [ ] P2.2 Tiled trace + visited/surviving heatmaps + brute-vs-tiled parity
- [ ] P2.3 RTScene + TRACE_RQ variant (guarded on m_rtSupported)

## Phase 3 — generation
- [ ] P3.1 Weather/GenMacro/GenScan/GenDetail/GenGrid + regen orchestration + L2 readout
- [ ] P3.2 Wind (phase velocity + advection offset)

## Phase 4 — lighting
- [ ] P4.1 Static-camera accumulation mode
- [ ] P4.2 CloudLighting + sun-channel light cache + trace consumption
- [ ] P4.3 Six-way variant + RT-reference mode + baked-vs-reference diff view
- [ ] P4.4 Sky→cubemap→IBL refresh + exposure clamp

## Phase 5 — LOD/temporal/perf
- [ ] P5.1 LOD + bounded masking + debug views
- [ ] P5.2 Half-res trace + denoise + reproject + upsampling composite
- [ ] P5.3 TAA sky fix
- [ ] P5.4 Perf tuning + acceptance-ladder verdict (record here + README)

## Phase 6 — stretch (optional)
- [ ] P6.1 In-register kernel synthesis A/B
- [ ] P6.2 Canonical 6D kernel transfer table
- [ ] P6.3 VDB→Gabor fitting tool
- [ ] P6.4 Agility preview / DXR 1.2 / work graphs

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
- P1.3/P1.4: run the app — expect a blue sky with a sun that moves as the Time of Day slider changes,
  and ~14 soft cumulus blobs. Set debug view 5 (analytic-vs-march) → should be near-black everywhere,
  INCLUDING when the camera flies inside a blob (validates the clamped erf integral). Debug view 3
  shows transmittance. (Debug-view UI combo lands with the fuller ImGui panel; until then set
  m_debugView in code or via the reload path.)

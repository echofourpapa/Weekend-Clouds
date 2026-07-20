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
- [ ] P1.1 CloudCommon.hlsli + CloudKernels.hlsli (math transliterated from validate_math.py)
- [ ] P1.2 CloudSystem skeleton: root sig, cloudCB[3], Assets-section descriptor blocks, engine wiring
- [ ] P1.3 SkyAtmosphere + SkyLUT-c.hlsl + sky-only composite + sun drives GetSunLight()
- [ ] P1.4 Brute-force trace (TRACE_BRUTE), hand-placed kernels, full composite, diff debug view

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

- P0.4: the "Reload cloud shaders" ImGui button is deferred to P1.2 — CloudSystem is the only
  allowed god-class hook (PLAN §6.3) and it doesn't exist yet; CloudShaderCompiler will be owned by
  CloudSystem, which exposes the reload (FlushGPU + PSO rebuild) and the button.

## User-verification queue
(steps finished in-code, awaiting a Windows build/run report)

- P0.2: fresh clone + `premake5 vs2022` + build x64 Debug WITHOUT thirdparty/aftermath present;
  expect link success; after a NuGet restore + rebuild, dxcompiler.dll/dxil.dll appear in bin/Debug
- P0.3: run the app, open the Info panel — expect "RT tier 1.1 | SM 6.8 | R11G11B10 UAV loads yes"
  on the 4070

# Weekend-Clouds

Real-time volumetric cloud renderer R&D (D3D12): clouds as mixtures of analytic **Gaussian/Gabor
kernel primitives** — closed-form ray integrals instead of raymarching — after Condor et al.,
[Gabor Fields](https://arxiv.org/abs/2602.05081) and
[Don't Splat your Gaussians](https://arxiv.org/abs/2405.15425). Built on the
[toy-renderer](https://github.com/echofourpapa/toy-renderer) deferred D3D12 engine.

Target: **≤1 ms GPU at 2560×1440 on an RTX 4070-class card.**

- **Spec / design doc:** [`docs/PLAN.md`](docs/PLAN.md) (canonical — read §0 before contributing)
- **Progress:** [`docs/STATUS.md`](docs/STATUS.md)

## Features

Procedural, animated, self-shadowed volumetric clouds as analytic Gaussian/Gabor
primitives — no voxel raymarching:

- **Tiled compute traversal** (primary) with a DXR 1.1 inline-**RayQuery** A/B path.
- **Procedural generation**: weather-FBM macro Gaussians + Gabor detail octaves (signed erosion),
  wind (advection + per-octave phase drift), or **load a `.cloud` fit** from `tools/vdb_fit/`.
- **Analytic closed-form** ray integration (erf), validated against quadrature in `tools/validate_math.py`.
- **Continuous LOD + stochastic masking**, **half-res** trace, **à-trous denoise**, **temporal
  reprojection** (cloud-owned history), TAA sky-pixel fix.
- **Lighting**: sun-transmittance **field cache** (analytic self-shadowing) + Wrenninge multi-octave
  scatter; ray-traced reference + baked-vs-reference diff; height/powder fallback; Hillaire sky LUTs.
- Rich ImGui panel (time-of-day, coverage, wind, quality knobs, traversal/lighting modes) + 12 debug views.

Two things remain, both hardware-in-the-loop: a first **Windows build** to shake out first-compile
issues, and **profiler-driven tuning** to the 1 ms target (all knobs are exposed). A handful of
comparison/stretch items are documented deferrals in `docs/STATUS.md`.

## Building (Windows)

```bat
git clone --recurse-submodules https://github.com/echofourpapa/Weekend-Clouds.git
:: or, after a plain clone:
git submodule update --init thirdparty/tomlplusplus thirdparty/imgui thirdparty/stb ^
    thirdparty/assimp thirdparty/MikkTSpace thirdparty/entt
:: (thirdparty/JoltPhysics is declared but unused by the build - safe to skip)

premake5 vs2022
:: open build/Awesome-Thing.sln, build x64 Debug or Release, run Awesome-Client
```

NVIDIA Nsight Aftermath is **off by default** (no SDK needed). If you have the Aftermath SDK in
`thirdparty/aftermath/`, enable it with `premake5 --aftermath vs2022`.

## Container-side checks (no Windows needed)

```sh
python3 tools/validate_math.py   # closed-form kernel math vs numeric quadrature (authoritative)
tools/check_shaders.sh           # DXC compile-check of all cloud shaders (needs dxc)
```

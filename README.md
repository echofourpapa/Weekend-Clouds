# Weekend-Clouds

Real-time volumetric cloud renderer R&D (D3D12): clouds as mixtures of analytic **Gaussian/Gabor
kernel primitives** — closed-form ray integrals instead of raymarching — after Condor et al.,
[Gabor Fields](https://arxiv.org/abs/2602.05081) and
[Don't Splat your Gaussians](https://arxiv.org/abs/2405.15425). Built on the
[toy-renderer](https://github.com/echofourpapa/toy-renderer) deferred D3D12 engine.

Target: **≤1 ms GPU at 2560×1440 on an RTX 4070-class card.**

- **Spec / design doc:** [`docs/PLAN.md`](docs/PLAN.md) (canonical — read §0 before contributing)
- **Progress:** [`docs/STATUS.md`](docs/STATUS.md)

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

# VDB → Gabor fitting (Phase 6)

Fits a density volume with a mixture of anisotropic Gaussian macro primitives and
writes a `.cloud` binary the renderer can load (`CloudGenerator::LoadFile`, or the
ImGui **Load fit.cloud** button — place the file in the app's working directory).

```sh
# synthetic cloud (no dependencies beyond numpy), 512 macros + 6 detail kernels each
python3 tools/vdb_fit/fit.py --macros 512 --detail 6 --out fit.cloud

# macros only (runtime layers its own procedural detail)
python3 tools/vdb_fit/fit.py --macros 256 --detail 0 --out macros_only.cloud

# a real OpenVDB grid (needs pyopenvdb)
python3 tools/vdb_fit/fit.py --vdb wdas_cloud.vdb --macros 2000 --world 6000 --out cloud.cloud
```

## File format (`.cloud`)

Little-endian. Header: `uint32 magic ("CLD1" = 0x434C4431), macroCount, kernelCount, pad`.
Then `macroCount` × 64-byte `CloudMacro` records, then `kernelCount` × 32-byte
`CloudKernelPacked` records — byte-identical to the runtime structs
(`src/renderer/includes/CloudGenerator.h`).

## Method

Mass-weighted k-means over occupied voxels places macro centers; each cluster's
mass-weighted covariance gives an anisotropic Gaussian (eigendecomposition →
per-axis σ + orientation quaternion).

`--detail N` then fits a layer of signed Gabor kernels to the **residual** the
Gaussian envelope leaves behind. The macros are reconstructed into a normalized
density grid (peak-scaled anisotropic Gaussians, max-combined for coverage), the
residual `vol − recon` is taken, and each macro gets up to `N` kernels planted at
the strongest residual extrema inside its 3σ box (greedily spaced apart). Each
kernel stores its position in the parent's local frame (snorm16 in units of
`4·σ`, exactly as `UnpackKernel` reconstructs it), a σ at ~0.35 of the parent, a
frequency of ~1 cycle per 2σ oriented along the local residual gradient, phase 0,
and the signed residual amplitude (clamped). `detailBegin`/`detailCount` on each
macro index the contiguous per-macro run in the global kernel array, matching the
procedural generator's layout. `--detail 0` reproduces the macros-only v1 file.

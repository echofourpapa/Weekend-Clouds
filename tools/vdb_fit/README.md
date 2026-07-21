# VDB → Gabor fitting (Phase 6)

Fits a density volume with a mixture of anisotropic Gaussian macro primitives and
writes a `.cloud` binary the renderer can load (`CloudGenerator::LoadFile`, or the
ImGui **Load fit.cloud** button — place the file in the app's working directory).

```sh
# synthetic cloud (no dependencies beyond numpy), 512 macros
python3 tools/vdb_fit/fit.py --macros 512 --out fit.cloud

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
per-axis σ + orientation quaternion). Detail Gabor kernels are left empty in v1
(the runtime can still layer procedural detail); fitting per-band Gabor residuals
is the natural next step.

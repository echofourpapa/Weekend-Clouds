#!/usr/bin/env python3
"""VDB -> Gabor/Gaussian mixture fitter (docs/PLAN.md Phase 6 / P6.3).

Fits a density volume with a mixture of anisotropic Gaussian macro primitives
(mass-weighted k-means + per-cluster covariance) and writes a binary .cloud file
whose records match the runtime's CloudMacro (64 B) / CloudKernelPacked (32 B)
layouts, so CloudGenerator::LoadFile can load it directly.

Runs in this container on a synthetic procedural cloud (no VDB dependency); if
pyopenvdb is installed, --vdb <file> loads a real grid instead. numpy only.

Usage:
    python3 tools/vdb_fit/fit.py --macros 512 --out disney.cloud
    python3 tools/vdb_fit/fit.py --vdb cloud.vdb --macros 2000 --out cloud.cloud
"""
import argparse
import struct
import sys

import numpy as np

MAGIC = 0x43_4C_44_31   # "CLD1"


def synthetic_volume(n=96):
    """A lumpy cumulus-like density field on an n^3 grid, values in [0,1]."""
    rng = np.random.default_rng(7)
    x = np.linspace(-1, 1, n)
    gx, gy, gz = np.meshgrid(x, x, x, indexing="ij")
    d = np.zeros((n, n, n), dtype=np.float32)
    for _ in range(24):
        c = rng.uniform(-0.6, 0.6, 3)
        r = rng.uniform(0.12, 0.32)
        s = np.array([r, r * rng.uniform(0.4, 0.7), r])   # flattened
        q = ((gx - c[0]) / s[0]) ** 2 + ((gy - c[1]) / s[1]) ** 2 + ((gz - c[2]) / s[2]) ** 2
        d += np.exp(-0.5 * q * 4.0)
    d -= d.mean() * 0.6                                    # erode to make gaps
    d = np.clip(d, 0, None)
    d *= gy < 0.4                                          # flat-ish top
    return d / (d.max() + 1e-6)


def load_vdb(path):
    try:
        import pyopenvdb as vdb
    except ImportError:
        print("pyopenvdb not available; use the synthetic path (omit --vdb).", file=sys.stderr)
        sys.exit(2)
    grid = vdb.readAllGridMetadata(path)[0]
    grid = vdb.read(path, grid.name)
    bbox = grid.evalActiveVoxelBoundingBox()
    dims = np.array(bbox[1]) - np.array(bbox[0]) + 1
    arr = np.zeros(tuple(int(x) for x in dims), dtype=np.float32)
    grid.copyToArray(arr, ijk=bbox[0])
    return arr / (arr.max() + 1e-6)


def fit_gaussians(vol, n_macros, iters=12):
    """Mass-weighted k-means over occupied voxels; returns per-cluster
    (mean, covariance, mass) in normalized [-1,1] world coordinates."""
    n = vol.shape[0]
    idx = np.argwhere(vol > 0.02)
    if len(idx) == 0:
        return []
    w = vol[idx[:, 0], idx[:, 1], idx[:, 2]].astype(np.float64)
    pts = (idx / (n - 1)) * 2.0 - 1.0                      # -> [-1,1]

    rng = np.random.default_rng(0)
    seeds = rng.choice(len(pts), size=min(n_macros, len(pts)), replace=False, p=w / w.sum())
    centers = pts[seeds].copy()

    for _ in range(iters):
        d2 = ((pts[:, None, :] - centers[None, :, :]) ** 2).sum(-1)
        assign = d2.argmin(1)
        for k in range(len(centers)):
            m = assign == k
            if m.any():
                wk = w[m]
                centers[k] = (pts[m] * wk[:, None]).sum(0) / wk.sum()

    out = []
    for k in range(len(centers)):
        m = assign == k
        if m.sum() < 4:
            continue
        p = pts[m]; wk = w[m]
        mean = (p * wk[:, None]).sum(0) / wk.sum()
        cov = np.cov((p - mean).T, aweights=wk) + np.eye(3) * 1e-6
        out.append((mean, cov, float(wk.sum())))
    return out


def pack_snorm16x2(a, b):
    def q(v):
        v = max(-1.0, min(1.0, float(v)))
        return int(round(v * 32767.0)) & 0xFFFF
    return q(a) | (q(b) << 16)


def mat_to_quat(R):
    """Rotation matrix (columns = eigenvectors) -> unit quaternion, w>=0."""
    t = np.trace(R)
    if t > 0:
        s = np.sqrt(t + 1.0) * 2
        w = 0.25 * s
        x = (R[2, 1] - R[1, 2]) / s
        y = (R[0, 2] - R[2, 0]) / s
        z = (R[1, 0] - R[0, 1]) / s
    else:
        i = np.argmax(np.diag(R))
        j, k = (i + 1) % 3, (i + 2) % 3
        s = np.sqrt(1.0 + R[i, i] - R[j, j] - R[k, k]) * 2
        q = [0, 0, 0]
        q[i] = 0.25 * s
        q[j] = (R[j, i] + R[i, j]) / s
        q[k] = (R[k, i] + R[i, k]) / s
        w = (R[k, j] - R[j, k]) / s
        x, y, z = q
    v = np.array([x, y, z, w])
    v /= np.linalg.norm(v) + 1e-9
    if v[3] < 0:
        v = -v
    return v


def write_cloud(path, macros, world_scale):
    """macros: list of (mean, cov, mass). world_scale: meters per [-1,1] unit."""
    recs = []
    for (mean, cov, mass) in macros:
        evals, evecs = np.linalg.eigh(cov)
        evals = np.clip(evals, 1e-8, None)
        sigma = np.sqrt(evals) * world_scale               # world-space std
        pos = mean * world_scale
        q = mat_to_quat(evecs)
        amp = min(0.12, 0.02 + mass * 1e-5)
        bound = 3.0 * float(sigma.max())
        height = float(np.clip(mean[1] * 0.5 + 0.5, 0, 1))
        rec = struct.pack(
            "<3f f 3f 2I 3I f f 2I",
            float(pos[0]), float(pos[1] + 2000.0), float(pos[2]), float(amp),
            float(sigma[0]), float(sigma[1]), float(sigma[2]),
            pack_snorm16x2(q[0], q[1]), pack_snorm16x2(q[2], q[3]),
            0, 0, 0,                                        # detailBegin/Count/seed
            bound, height, 0, 0,                            # octaveMask, pad0
        )
        assert len(rec) == 64
        recs.append(rec)

    with open(path, "wb") as f:
        f.write(struct.pack("<IIII", MAGIC, len(recs), 0, 0))   # header: magic, macroCount, kernelCount, pad
        for r in recs:
            f.write(r)
    return len(recs)


def reconstruct_error(vol, macros, world_scale):
    """Cheap validation: sample reconstructed density at cluster means."""
    n = vol.shape[0]
    err = 0.0
    for (mean, cov, mass) in macros:
        ijk = np.clip(((mean * 0.5 + 0.5) * (n - 1)).astype(int), 0, n - 1)
        err += abs(vol[ijk[0], ijk[1], ijk[2]] - min(1.0, mass * 1e-4))
    return err / max(len(macros), 1)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--vdb", default=None)
    ap.add_argument("--macros", type=int, default=512)
    ap.add_argument("--world", type=float, default=4000.0, help="meters per [-1,1] unit")
    ap.add_argument("--out", default="fit.cloud")
    args = ap.parse_args()

    vol = load_vdb(args.vdb) if args.vdb else synthetic_volume()
    print(f"volume {vol.shape}, occupancy {(vol > 0.02).mean():.3f}")
    macros = fit_gaussians(vol, args.macros)
    n = write_cloud(args.out, macros, args.world)
    print(f"fit {n} macro Gaussians -> {args.out} ({n * 64 + 16} bytes)")
    print(f"mean recon error (coarse): {reconstruct_error(vol, macros, args.world):.4f}")


if __name__ == "__main__":
    main()

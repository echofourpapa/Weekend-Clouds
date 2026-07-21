#!/usr/bin/env python3
"""VDB -> Gabor/Gaussian mixture fitter (docs/PLAN.md Phase 6 / P6.3).

Fits a density volume with a mixture of anisotropic Gaussian macro primitives
(mass-weighted k-means + per-cluster covariance) and, optionally, a layer of
signed Gabor detail kernels fitted to the *residual* the Gaussians leave behind
(the wisps and erosion a smooth envelope can't represent). Writes a binary
.cloud file whose records match the runtime's CloudMacro (64 B) /
CloudKernelPacked (32 B) layouts, so CloudGenerator::LoadFile loads it directly.

Runs in this container on a synthetic procedural cloud (no VDB dependency); if
pyopenvdb is installed, --vdb <file> loads a real grid instead. numpy only.

Usage:
    python3 tools/vdb_fit/fit.py --macros 512 --detail 6 --out disney.cloud
    python3 tools/vdb_fit/fit.py --vdb cloud.vdb --macros 2000 --out cloud.cloud
    python3 tools/vdb_fit/fit.py --macros 256 --detail 0 --out macros_only.cloud
"""
import argparse
import struct
import sys

import numpy as np

MAGIC = 0x43_4C_44_31   # "CLD1"
Y_OFFSET = 2000.0       # cloud layer altitude (m); matches write_cloud + runtime


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


# --- packing helpers (mirror CloudKernels.hlsli / CloudGenerator) --------------

def pack_snorm16x2(a, b):
    def q(v):
        v = max(-1.0, min(1.0, float(v)))
        return int(round(v * 32767.0)) & 0xFFFF
    return q(a) | (q(b) << 16)


def pack_snorm8x4(x, y, z, w):
    def q(v):
        v = max(-1.0, min(1.0, float(v)))
        return int(round(v * 127.0)) & 0xFF
    return q(x) | (q(y) << 8) | (q(z) << 16) | (q(w) << 24)


def pack_f16x2(a, b):
    ha = int(np.float16(a).view(np.uint16))
    hb = int(np.float16(b).view(np.uint16))
    return ha | (hb << 16)


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


def quat_to_rows(q):
    """Quaternion -> 3x3 rotation whose rows are row0,row1,row2 (matches the
    HLSL QuatToRows in CloudKernels.hlsli, world -> kernel frame)."""
    x, y, z, w = q
    return np.array([
        [1 - 2 * (y * y + z * z), 2 * (x * y - z * w),     2 * (x * z + y * w)],
        [2 * (x * y + z * w),     1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
        [2 * (x * z - y * w),     2 * (y * z + x * w),     1 - 2 * (x * x + y * y)],
    ])


def macro_frames(macros, world_scale):
    """Precompute the world-space frame each macro is written with, so the
    Gaussian record and its child detail kernels share one orientation/scale."""
    frames = []
    for (mean, cov, mass) in macros:
        evals, evecs = np.linalg.eigh(cov)
        evals = np.clip(evals, 1e-8, None)
        sigma = np.sqrt(evals) * world_scale               # world-space std, per axis
        pos = mean * world_scale
        pos = np.array([pos[0], pos[1] + Y_OFFSET, pos[2]])
        quat = mat_to_quat(evecs)
        amp = min(0.12, 0.02 + mass * 1e-5)
        bound = 3.0 * float(sigma.max())
        height = float(np.clip(mean[1] * 0.5 + 0.5, 0, 1))
        frames.append(dict(mean=mean, cov=cov, mass=mass, sigma=sigma,
                           pos=pos, quat=quat, amp=amp, bound=bound, height=height))
    return frames


# --- residual-driven Gabor detail ---------------------------------------------

def build_recon(vol, frames):
    """Coarse reconstruction of vol from the macro Gaussians, in normalized
    density units. Each macro contributes a peak-scaled anisotropic Gaussian;
    overlaps take the max (coverage), matching how the envelope reads."""
    n = vol.shape[0]
    recon = np.zeros_like(vol, dtype=np.float32)
    ax = np.linspace(-1, 1, n)
    for fr in frames:
        mean, cov = fr["mean"], fr["cov"]
        P = np.linalg.inv(cov)
        # local box (3 sigma) in voxel indices to keep the eval cheap
        sig_vox = np.sqrt(np.clip(np.diag(cov), 1e-8, None)) * (n - 1) * 0.5
        c_vox = ((mean * 0.5 + 0.5) * (n - 1)).astype(int)
        lo = np.clip(c_vox - np.ceil(3 * sig_vox).astype(int), 0, n - 1)
        hi = np.clip(c_vox + np.ceil(3 * sig_vox).astype(int) + 1, 0, n)
        if np.any(hi <= lo):
            continue
        sx, sy, sz = (slice(lo[i], hi[i]) for i in range(3))
        gx, gy, gz = np.meshgrid(ax[sx], ax[sy], ax[sz], indexing="ij")
        dx = gx - mean[0]; dy = gy - mean[1]; dz = gz - mean[2]
        d2 = (P[0, 0] * dx * dx + P[1, 1] * dy * dy + P[2, 2] * dz * dz
              + 2 * (P[0, 1] * dx * dy + P[0, 2] * dx * dz + P[1, 2] * dy * dz))
        peak = float(vol[min(c_vox[0], n - 1), min(c_vox[1], n - 1), min(c_vox[2], n - 1)])
        recon[sx, sy, sz] = np.maximum(recon[sx, sy, sz], peak * np.exp(-0.5 * d2))
    return recon


def fit_gabor_residual(vol, frames, k_per_macro, world_scale):
    """Place up to k_per_macro signed Gabor kernels per macro at the strongest
    residual extrema inside that macro's 3-sigma box. Returns (records, counts)
    where records is the flat packed-kernel list and counts[i] is macro i's
    detail count (detailBegin is the running offset)."""
    n = vol.shape[0]
    recon = build_recon(vol, frames)
    residual = (vol - recon).astype(np.float32)
    # residual gradient -> per-voxel dominant detail direction (normalized world)
    grad = np.gradient(residual)   # d/di, d/dj, d/dk in voxel units
    ax = np.linspace(-1, 1, n)

    records = []
    counts = []
    for mi, fr in enumerate(frames):
        if k_per_macro <= 0:
            counts.append(0)
            continue
        mean, sigma, quat = fr["mean"], fr["sigma"], fr["quat"]
        R = quat_to_rows(quat)
        sig_vox = (sigma / world_scale) * (n - 1) * 0.5
        c_vox = ((mean * 0.5 + 0.5) * (n - 1)).astype(int)
        lo = np.clip(c_vox - np.ceil(3 * sig_vox).astype(int), 0, n - 1)
        hi = np.clip(c_vox + np.ceil(3 * sig_vox).astype(int) + 1, 0, n)
        box = residual[lo[0]:hi[0], lo[1]:hi[1], lo[2]:hi[2]]
        if box.size == 0:
            counts.append(0)
            continue
        # strongest |residual| voxels in the box, greedily spaced apart
        flat = np.argsort(np.abs(box).ravel())[::-1]
        chosen = []
        min_sep = max(1, int(0.5 * np.mean(np.maximum(sig_vox, 1.0))))
        for f in flat:
            v = np.unravel_index(f, box.shape)
            if abs(box[v]) < 0.03:
                break
            if all(max(abs(v[0] - c[0]), abs(v[1] - c[1]), abs(v[2] - c[2])) >= min_sep for c in chosen):
                chosen.append(v)
            if len(chosen) >= k_per_macro:
                break

        emitted = 0
        for v in chosen:
            ijk = (lo[0] + v[0], lo[1] + v[1], lo[2] + v[2])
            child_norm = np.array([ax[ijk[0]], ax[ijk[1]], ax[ijk[2]]])
            child_world = child_norm * world_scale
            delta = child_world - mean * world_scale       # Y_OFFSET cancels
            local = R @ delta                              # world -> parent frame
            pxyz = local / (4.0 * np.maximum(sigma, 1e-3)) # snorm units
            if np.any(np.abs(pxyz) > 1.0):
                continue                                   # outside packable box
            # detail scale: a fraction of the parent, clamped to sane meters
            ksig = np.clip(0.35 * sigma, 20.0, 800.0)
            # detail frequency along the local residual gradient (cycles/m)
            g = np.array([grad[0][ijk], grad[1][ijk], grad[2][ijk]])
            gn = np.linalg.norm(g)
            gdir = g / gn if gn > 1e-8 else np.array([1.0, 0.0, 0.0])
            freq_mag = 0.5 / float(np.mean(ksig))          # ~1 cycle per 2 sigma
            freq = gdir * freq_mag
            amp = float(np.clip(box[v] * 0.5, -0.08, 0.08))
            seed = (mi * 2654435761 + emitted * 40503) & 0xFFFF

            a_x = pack_snorm16x2(pxyz[0], pxyz[1])
            a_y = pack_snorm16x2(pxyz[2], 0.0)             # hi 16 = flags (0)
            a_z = pack_snorm8x4(0.0, 0.0, 0.0, 1.0)        # identity kernel rot
            a_w = pack_f16x2(ksig[0], ksig[1])
            b_x = pack_f16x2(ksig[2], amp)
            b_y = pack_f16x2(freq[0], freq[1])
            b_z = pack_f16x2(freq[2], 0.0)                 # phase 0
            b_w = seed & 0xFFFF
            rec = struct.pack("<8I", a_x, a_y, a_z, a_w, b_x, b_y, b_z, b_w)
            assert len(rec) == 32
            records.append(rec)
            emitted += 1
        counts.append(emitted)
    return records, counts


def write_cloud(path, frames, kernels, counts):
    """frames from macro_frames(); kernels flat packed list; counts[i] = macro i
    detail count. detailBegin is the running kernel offset."""
    recs = []
    begin = 0
    for fr, kc in zip(frames, counts):
        pos, sigma, q = fr["pos"], fr["sigma"], fr["quat"]
        rec = struct.pack(
            "<3f f 3f 2I 3I f f 2I",
            float(pos[0]), float(pos[1]), float(pos[2]), float(fr["amp"]),
            float(sigma[0]), float(sigma[1]), float(sigma[2]),
            pack_snorm16x2(q[0], q[1]), pack_snorm16x2(q[2], q[3]),
            begin, kc, 0,                                   # detailBegin/Count/seed
            fr["bound"], fr["height"], 0, 0,                # octaveMask, pad0
        )
        assert len(rec) == 64
        recs.append(rec)
        begin += kc

    with open(path, "wb") as f:
        f.write(struct.pack("<IIII", MAGIC, len(recs), len(kernels), 0))   # magic, macroCount, kernelCount, pad
        for r in recs:
            f.write(r)
        for k in kernels:
            f.write(k)
    return len(recs), len(kernels)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--vdb", default=None)
    ap.add_argument("--macros", type=int, default=512)
    ap.add_argument("--detail", type=int, default=6, help="Gabor residual kernels per macro (0 = macros only)")
    ap.add_argument("--world", type=float, default=4000.0, help="meters per [-1,1] unit")
    ap.add_argument("--out", default="fit.cloud")
    args = ap.parse_args()

    vol = load_vdb(args.vdb) if args.vdb else synthetic_volume()
    print(f"volume {vol.shape}, occupancy {(vol > 0.02).mean():.3f}")
    macros = fit_gaussians(vol, args.macros)
    frames = macro_frames(macros, args.world)

    if args.detail > 0:
        recon = build_recon(vol, frames)
        res0 = float(np.abs(vol - recon).mean())
        kernels, counts = fit_gabor_residual(vol, frames, args.detail, args.world)
        print(f"residual mean |vol-recon| before detail: {res0:.4f}; "
              f"fitted {len(kernels)} Gabor kernels over {sum(c > 0 for c in counts)} macros")
    else:
        kernels, counts = [], [0] * len(frames)

    nm, nk = write_cloud(args.out, frames, kernels, counts)
    total = 16 + nm * 64 + nk * 32
    print(f"wrote {nm} macros + {nk} kernels -> {args.out} ({total} bytes)")


if __name__ == "__main__":
    main()

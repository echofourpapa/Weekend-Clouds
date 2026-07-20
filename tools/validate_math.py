#!/usr/bin/env python3
"""Authoritative reference + validation for the closed-form kernel math in
src/renderer/shaders/CloudKernels.hlsli (spec: docs/PLAN.md section 5).

THE COEFFICIENT TABLES AND FORMULAS HERE ARE THE SOURCE OF TRUTH.
The HLSL must be a transliteration of these functions. If you change one,
change the other and re-run:  python3 tools/validate_math.py

Exit code 0 = all checks pass. Requires numpy (pip install numpy).
"""
import math
import sys

import numpy as np

RNG = np.random.default_rng(0xC10D5)


# ----------------------------------------------------------------------------
# Reference primitives (transliterate these to HLSL verbatim)
# ----------------------------------------------------------------------------

def erf_as(x):
    """Abramowitz-Stegun 7.1.26, max abs err 1.5e-7. Vectorized."""
    x = np.asarray(x, dtype=np.float64)
    s = np.sign(x)
    ax = np.abs(x)
    t = 1.0 / (1.0 + 0.3275911 * ax)
    p = t * (0.254829592 + t * (-0.284496736 + t * (1.421413741
        + t * (-1.453152027 + t * 1.061405429))))
    return s * (1.0 - p * np.exp(-ax * ax))


def erfinv_giles(x):
    """Giles 2010 'Approximating the erfinv function', single-precision variant.
    Valid for |x| < 1. Vectorized. HLSL: same two branches, same tables."""
    x = np.asarray(x, dtype=np.float64)
    w = -np.log((1.0 - x) * (1.0 + x))
    p = np.empty_like(w)

    lo = w < 5.0
    wl = w[lo] - 2.5
    pl = np.full_like(wl, 2.81022636e-08)
    for c in (3.43273939e-07, -3.5233877e-06, -4.39150654e-06, 0.00021858087,
              -0.00125372503, -0.00417768164, 0.246640727, 1.50140941):
        pl = c + pl * wl
    p[lo] = pl

    hi = ~lo
    wh = np.sqrt(w[hi]) - 3.0
    ph = np.full_like(wh, -0.000200214257)
    for c in (0.000100950558, 0.00134934322, -0.00367342844, 0.00573950773,
              -0.0076224613, 0.00943887047, 1.00167406, 2.83297682):
        ph = c + ph * wh
    p[hi] = ph

    return p * x


def kernel_ray_setup(mu, rot, sigma, o, d):
    """p, q, a, b, c, tbar for kernel (mu, 3x3 rot, sigma[3]) and ray o + t*d."""
    p = rot @ (o - mu) / sigma
    q = rot @ d / sigma
    a = float(q @ q)
    b = float(p @ q)
    c = float(p @ p)
    tbar = -b / a
    return p, q, a, b, c, tbar


def tau_gauss_clamped(amp, a, b, c, t0, t1):
    """Closed-form optical depth of amp*exp(-0.5*(a t^2 + 2 b t + c)) over [t0,t1]."""
    tbar = -b / a
    env = -0.5 * (c - b * b / a)
    s = math.sqrt(a / 2.0)
    bracket = erf_as(s * (t1 - tbar)) - erf_as(s * (t0 - tbar))
    return amp * math.sqrt(2.0 * math.pi / a) * 0.5 * math.exp(env) * float(bracket)


def tau_gabor_clamped(amp, a, b, c, omega, psi, t0, t1):
    """Bracket-scaled Gabor optical depth (exact for full-domain; approximate for
    partial clamps - the approximation the renderer ships, error quantified below)."""
    tbar = -b / a
    env = -0.5 * (c - b * b / a)
    gab = -(omega * omega) / (2.0 * a)
    s = math.sqrt(a / 2.0)
    bracket = erf_as(s * (t1 - tbar)) - erf_as(s * (t0 - tbar))
    return (amp * math.sqrt(2.0 * math.pi / a) * 0.5
            * math.exp(env + gab) * math.cos(omega * tbar + psi) * float(bracket))


def freeflight_truncated(a, tbar, t0, t1, xi):
    """Sample t in [t0,t1] proportional to exp(-a/2 (t-tbar)^2). Vectorized in xi."""
    s = math.sqrt(a / 2.0)
    e0 = erf_as(np.array(s * (t0 - tbar)))
    e1 = erf_as(np.array(s * (t1 - tbar)))
    u = e0 + (e1 - e0) * xi
    return tbar + math.sqrt(2.0 / a) * erfinv_giles(u)


def fullpath_eligible(t_geom_inf, tbar, a):
    """Full-domain fast path predicate (PLAN.md section 5)."""
    return t_geom_inf and (tbar - 3.0 * math.sqrt(2.0 / a) > 0.0)


# ----------------------------------------------------------------------------
# Checks
# ----------------------------------------------------------------------------

_TRAPZ = getattr(np, "trapezoid", None) or np.trapz  # numpy 2.x renamed trapz


def quad_tau(density_fn, t0, t1, n=8192):
    t = np.linspace(t0, t1, n)
    return _TRAPZ(density_fn(t), t)


def random_kernel_ray():
    mu = RNG.uniform(-100, 100, 3)
    sigma = RNG.uniform(0.5, 50.0, 3)
    # random rotation via normalized quaternion
    qv = RNG.normal(size=4)
    qx, qy, qz, qw = qv / np.linalg.norm(qv)
    rot = np.array([
        [1 - 2 * (qy * qy + qz * qz), 2 * (qx * qy - qz * qw), 2 * (qx * qz + qy * qw)],
        [2 * (qx * qy + qz * qw), 1 - 2 * (qx * qx + qz * qz), 2 * (qy * qz - qx * qw)],
        [2 * (qx * qz - qy * qw), 2 * (qy * qz + qx * qw), 1 - 2 * (qx * qx + qy * qy)],
    ])
    o = RNG.uniform(-200, 200, 3)
    d = RNG.normal(size=3)
    d /= np.linalg.norm(d)
    return mu, rot, sigma, o, d


def check_erf():
    xs = np.linspace(-4, 4, 20001)
    ref = np.array([math.erf(v) for v in xs])
    err = np.max(np.abs(erf_as(xs) - ref))
    assert err < 2e-7, f"erf_as max err {err}"
    print(f"  erf_as       max abs err {err:.2e}  OK")


def check_erfinv():
    xs = np.linspace(-0.9999, 0.9999, 20001)
    err = np.max(np.abs(erf_as(erfinv_giles(xs)) - xs))
    assert err < 5e-6, f"erfinv roundtrip err {err}"
    print(f"  erfinv_giles roundtrip err {err:.2e}  OK")


def check_gauss_integral(n_cases=2000):
    worst = 0.0
    for _ in range(n_cases):
        mu, rot, sigma, o, d = random_kernel_ray()
        p, q, a, b, c, tbar = kernel_ray_setup(mu, rot, sigma, o, d)
        span = 6.0 / math.sqrt(a / 2.0)
        t0 = tbar + RNG.uniform(-1.5, 0.5) * span   # includes partial clips
        t1 = t0 + RNG.uniform(0.1, 2.0) * span
        amp = RNG.uniform(0.001, 0.1)
        closed = tau_gauss_clamped(amp, a, b, c, t0, t1)
        num = quad_tau(lambda t: amp * np.exp(-0.5 * (a * t * t + 2 * b * t + c)), t0, t1)
        # normalize by the kernel's full-ray mass: far-tail intervals have
        # closed ~ num ~ 0 where plain relative error only measures erf approx
        # noise (1.5e-7 abs), which is invisible in transmittance
        mass = abs(amp) * math.sqrt(2.0 * math.pi / a) * math.exp(-0.5 * (c - b * b / a))
        denom = max(mass, 1e-12)
        worst = max(worst, abs(closed - num) / denom)
    assert worst < 1e-3, f"gauss integral mass-relative err {worst}"
    print(f"  gauss integral (clamped) worst mass-relative err {worst:.2e}  OK")


def check_gabor_integral(n_cases=2000):
    worst_full = 0.0
    worst_partial = 0.0
    for _ in range(n_cases):
        mu, rot, sigma, o, d = random_kernel_ray()
        p, q, a, b, c, tbar = kernel_ray_setup(mu, rot, sigma, o, d)
        f = RNG.normal(size=3) * RNG.uniform(0.0, 0.5) / np.mean(sigma)
        omega = 2 * math.pi * float(f @ d)
        psi = 2 * math.pi * float(f @ (o - mu)) + RNG.uniform(0, 2 * math.pi)
        amp = RNG.uniform(-0.1, 0.1)
        span = 6.0 / math.sqrt(a / 2.0)

        def density(t):
            return amp * np.exp(-0.5 * (a * t * t + 2 * b * t + c)) * np.cos(omega * t + psi)

        # full-domain (support fully inside)
        t0, t1 = tbar - 2 * span, tbar + 2 * span
        closed = tau_gabor_clamped(amp, a, b, c, omega, psi, t0, t1)
        num = quad_tau(density, t0, t1, n=16384)
        env = math.exp(-0.5 * (c - b * b / a))
        scale = max(abs(amp) * math.sqrt(2 * math.pi / a) * env, 1e-12)  # ray-mass scale
        worst_full = max(worst_full, abs(closed - num) / scale)

        # partial clamp: report only (documented approximation)
        t0p = tbar + RNG.uniform(-0.5, 0.5) * span
        t1p = t0p + RNG.uniform(0.05, 0.5) * span
        closed_p = tau_gabor_clamped(amp, a, b, c, omega, psi, t0p, t1p)
        num_p = quad_tau(density, t0p, t1p, n=16384)
        worst_partial = max(worst_partial, abs(closed_p - num_p) / scale)

    assert worst_full < 1e-3, f"gabor full-domain rel err {worst_full}"
    print(f"  gabor integral (full support) worst rel err {worst_full:.2e}  OK")
    print(f"  gabor integral (partial clamp) worst rel err {worst_partial:.2e}"
          f"  [documented approximation, report-only]")


def check_freeflight(n=200_000):
    mu, rot, sigma, o, d = random_kernel_ray()
    _, _, a, b, c, tbar = kernel_ray_setup(mu, rot, sigma, o, d)
    span = 3.0 / math.sqrt(a / 2.0)
    t0, t1 = tbar - 0.3 * span, tbar + 1.2 * span   # asymmetric truncation
    xi = RNG.uniform(0, 1, n)
    ts = freeflight_truncated(a, tbar, t0, t1, xi)
    assert np.all((ts >= t0 - 1e-6) & (ts <= t1 + 1e-6)), "samples escaped truncation"
    hist, edges = np.histogram(ts, bins=64, range=(t0, t1), density=True)
    centers = 0.5 * (edges[:-1] + edges[1:])
    pdf = np.exp(-0.5 * a * (centers - tbar) ** 2)
    pdf /= _TRAPZ(pdf, centers)
    err = np.max(np.abs(hist - pdf)) / np.max(pdf)
    assert err < 0.05, f"freeflight histogram err {err}"
    print(f"  truncated free-flight sampling max pdf err {err:.3f}  OK")


def check_fastpath_predicate():
    # kernel behind camera must NOT be eligible
    a = 1.0
    assert not fullpath_eligible(True, tbar=-1.0, a=a)
    # kernel straddling camera must NOT be eligible
    assert not fullpath_eligible(True, tbar=2.0, a=a)   # 3*sqrt(2/1)=4.24 > 2
    # far kernel, sky pixel: eligible
    assert fullpath_eligible(True, tbar=100.0, a=a)
    # geometry pixel: never
    assert not fullpath_eligible(False, tbar=100.0, a=a)
    print("  full-domain fast-path predicate  OK")


def check_packing():
    """Round-trip the 32B CloudKernelPacked: CPU pack (CloudGenerator.cpp) ->
    HLSL unpack (CloudKernels.hlsli). Mirrors both sides exactly."""
    # --- CPU pack helpers (mirror CloudGenerator.cpp) ---
    def packS16(v):
        v = max(-1.0, min(1.0, v)); return int(round(v * 32767.0)) & 0xFFFF
    def packS16x2(a, b): return packS16(a) | (packS16(b) << 16)
    def packS8(v):
        v = max(-1.0, min(1.0, v)); return int(round(v * 127.0)) & 0xFF
    def packS8x4(x, y, z, w): return packS8(x) | (packS8(y) << 8) | (packS8(z) << 16) | (packS8(w) << 24)
    def packF16x2(a, b):
        ha = int(np.float16(a).view(np.uint16)); hb = int(np.float16(b).view(np.uint16))
        return ha | (hb << 16)

    # --- HLSL unpack helpers (mirror CloudKernels.hlsli) ---
    def u_s16x2(v):
        lo = v & 0xFFFF; hi = (v >> 16) & 0xFFFF
        def sx(h): return max((h - 0x10000 if h & 0x8000 else h) / 32767.0, -1.0)
        return sx(lo), sx(hi)
    def u_s8x4(v):
        def sx(b): return max((b - 0x100 if b & 0x80 else b) / 127.0, -1.0)
        return sx(v & 0xFF), sx((v >> 8) & 0xFF), sx((v >> 16) & 0xFF), sx((v >> 24) & 0xFF)
    def u_f16x2(v):
        lo = np.uint16(v & 0xFFFF).view(np.float16); hi = np.uint16((v >> 16) & 0xFFFF).view(np.float16)
        return float(lo), float(hi)

    worst = 0.0
    for _ in range(5000):
        lx, ly, lz = RNG.uniform(-1, 1, 3)
        q = RNG.normal(size=4); q /= np.linalg.norm(q)
        if q[3] < 0: q = -q
        sig = RNG.uniform(20, 2000); amp = RNG.uniform(-0.1, 0.1)
        fx, fy, fz = RNG.uniform(-0.01, 0.01, 3); phase = RNG.uniform(0, 1)
        flags = RNG.integers(0, 8); seed16 = int(RNG.integers(0, 0xFFFF))

        a0 = packS16x2(lx, ly)
        a1 = packS16(lz) | (int(flags) << 16)
        a2 = packS8x4(*q)
        a3 = packF16x2(sig, sig)
        b0 = packF16x2(sig, amp)
        b1 = packF16x2(fx, fy)
        b2 = packF16x2(fz, phase)
        b3 = seed16

        # unpack (as UnpackKernel does)
        rlx, rly = u_s16x2(a0); rlz = u_s16x2(a1)[0]; rflags = a1 >> 16
        rq = u_s8x4(a2)
        rsx, rsy = u_f16x2(a3); rsz, ramp = u_f16x2(b0)
        rfx, rfy = u_f16x2(b1); rfz, rphase = u_f16x2(b2)

        # errors (snorm16 ~3e-5, snorm8 ~8e-3, f16 relative ~1e-3)
        worst = max(worst, abs(rlx - lx), abs(rly - ly), abs(rlz - lz))
        assert rflags == flags, f"flags {rflags} != {flags}"
        assert b3 == seed16
        assert all(abs(a - b) < 0.02 for a, b in zip(rq, q)), f"quat {rq} vs {q}"
        assert abs(ramp - amp) < 1e-3 + 1e-3 * abs(amp), f"amp {ramp} vs {amp}"
        assert abs(rphase - phase) < 1e-3
    assert worst < 3e-4, f"snorm16 pos error {worst}"
    print(f"  kernel pack/unpack round-trip  OK (worst snorm16 {worst:.2e})")


def print_layout():
    rows = [
        ("u0", "posX snorm16 | posY snorm16"),
        ("u1", "posZ snorm16 | flags16 (oct 0-1, shadow bit2)"),
        ("u2", "quat 4x snorm8 (w>=0)"),
        ("u3", "sigmaX f16 | sigmaY f16"),
        ("u4", "sigmaZ f16 | amplitude f16 (signed)"),
        ("u5", "freqX f16 | freqY f16"),
        ("u6", "freqZ f16 | phaseTurns f16"),
        ("u7", "seed16 | reserved"),
    ]
    print("CloudKernelPacked - 32 bytes, 8x uint32:")
    for name, desc in rows:
        print(f"  {name}: {desc}")
    print("CloudMacro - 64 bytes (see docs/PLAN.md 3.2)")


def main():
    if "--layout" in sys.argv:
        print_layout()
        return
    print("validate_math.py — CloudKernels.hlsli reference checks")
    check_erf()
    check_erfinv()
    check_gauss_integral()
    check_gabor_integral()
    check_freeflight()
    check_fastpath_predicate()
    check_packing()
    print("ALL MATH CHECKS PASSED")


if __name__ == "__main__":
    main()

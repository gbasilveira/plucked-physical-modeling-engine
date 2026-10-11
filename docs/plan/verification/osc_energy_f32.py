# SPDX-License-Identifier: MIT
# Planning-time numerical check for SI-0009 (docs/spec-issues/SI-0009-orc02-float32-energy-tolerance.md).
# Float32 round-off drift of the discrete energy of the SPEC-OSC difference form, lossless, 10 s at 48 kHz.
# numpy float32 array arithmetic rounds every operation (no FMA); fma=True emulates a fused u - kappa*q.
import sys
import numpy as np

FS = 48000.0
N = 480000                      # 10 s at 48 kHz
U32 = 2.0 ** -24                # float32 unit round-off
C = 24.0                        # SI-0009 constant


def coeffs(f):
    """SPEC-OSC (SI-0008 form), sigma = 0: returns beta, kappa, omegaHat^2 dt^2 (float64)."""
    y = 2 * np.pi * np.asarray(f, dtype=np.float64) / FS
    w2 = 4 * np.sin(y / 2) ** 2
    return np.ones_like(w2), w2.copy(), w2


def tol(f, n=N):
    _, _, w2 = coeffs(f)
    return C * U32 * np.sqrt(n) / (1 - w2 / 4)


def drift(f, q0, phase, n=N, fma=False, defect="none", u0=None):
    """Max over steps of |H - H0| / H0, H = 0.5 u'^2 + 0.5 w2 q q' (times dt^2), float64 energy."""
    beta, kappa, w2 = coeffs(f)
    if defect == "beta_ulp_low":
        beta = np.full_like(beta, float(np.nextafter(np.float32(1), np.float32(0))))
    elif defect == "beta_ulp_high":
        beta = np.full_like(beta, float(np.nextafter(np.float32(1), np.float32(2))))
    elif defect == "kappa_1e-3":
        kappa = kappa * (1 + 1e-3)
    elif defect == "kappa_naive":   # omega^2 dt^2 in place of omegaHat^2 dt^2
        kappa = (2 * np.pi * np.asarray(f) / FS) ** 2
    b32, k32 = beta.astype(np.float32), kappa.astype(np.float32)
    y = np.arccos(1 - w2 / 2)
    q = (q0 * np.cos(phase)).astype(np.float32)
    u = (q0 * np.cos(phase + y) - q0 * np.cos(phase)).astype(np.float32)
    if u0 is not None:
        u = np.asarray(u0).astype(np.float32)
    e0, md = None, np.zeros_like(w2)
    for _ in range(n):
        if defect == "euler":       # q advanced with the old u: not energy conserving
            un = b32 * u - k32 * q
            qn = q + u
        elif fma:
            un = (b32.astype(np.float64) * u - k32.astype(np.float64) * q.astype(np.float64)).astype(np.float32)
            qn = q + un
        else:
            un = b32 * u - k32 * q
            qn = q + un
        e = 0.5 * un.astype(np.float64) ** 2 + 0.5 * w2 * q.astype(np.float64) * qn.astype(np.float64)
        if e0 is None:
            e0 = e
        np.fmax(md, np.abs(e - e0) / e0, out=md)
        u, q = un, qn
    return md


if __name__ == "__main__":
    np.seterr(all="ignore")         # the defect "euler" overflows on purpose
    rng = np.random.default_rng(7)
    pinned = np.array([27.5, 261.6, 4186.0, 21600.0])
    print("Tolerance 24 u sqrt(N) / (1 - w2/4):", " ".join(f"{f:g} Hz {t:.2e}" for f, t in zip(pinned, tol(pinned))))
    m = 50
    F = np.repeat(pinned, m)
    Q = rng.uniform(0.2, 1.0, F.size)
    P = rng.uniform(0, 2 * np.pi, F.size)
    for fma in (False, True):
        md = drift(F, Q, P, fma=fma).reshape(pinned.size, m)
        r = md.max(axis=1) / tol(pinned)
        print(f"correct, fma={fma}: worst drift", " ".join(f"{x:.2e}" for x in md.max(axis=1)),
              " worst / tolerance", " ".join(f"{x:.2f}" for x in r))
    for d in ("beta_ulp_low", "beta_ulp_high", "kappa_1e-3", "kappa_naive", "euler"):
        md = drift(pinned, np.ones(4), np.zeros(4), defect=d)
        print(f"defect {d:14s} drift", " ".join(f"{x:.2e}" for x in md),
              " / tolerance", " ".join(f"{x:.2g}" for x in md / tol(pinned)))
    if "--sweep" in sys.argv:       # 1500 log-uniform frequencies 20 Hz to 21.6 kHz, about 20 s per pass
        F = np.exp(rng.uniform(np.log(20), np.log(21600), 1500))
        near = np.min(np.abs((F / FS)[:, None] / np.array([1 / 6, 1 / 4, 1 / 3]) - 1), axis=1)
        F = F[near > 0.01]
        Q = rng.uniform(0.2, 1.0, F.size)
        P = rng.uniform(0, 2 * np.pi, F.size)
        for fma in (False, True):
            r = drift(F, Q, P, fma=fma) / tol(F)
            print(f"sweep fma={fma} n={F.size}: drift/tolerance median {np.median(r):.3f} "
                  f"p99 {np.quantile(r, 0.99):.3f} max {r.max():.3f}")
    if "--evidence" in sys.argv:    # the A1 test-author's cases: q0 = 1, u0 = 0.5 sqrt(kappa)
        _, k, _ = coeffs(pinned)
        md = drift(pinned, np.ones(4), np.zeros(4), u0=0.5 * np.sqrt(k))
        print("evidence cases drift", " ".join(f"{x:.3e}" for x in md), " / tolerance", " ".join(f"{x:.2g}" for x in md / tol(pinned)))
    if "--resonance" in sys.argv:   # exactly at fs/3 kappa = 3 is exact in float32: secular drift
        md = drift(np.array([16000.0]), np.array([0.7]), np.array([0.0]), u0=0.5 * np.sqrt(3.0))
        print(f"fs/3, q0 = 0.7: drift {md[0]:.2e}, tolerance {tol(np.array([16000.0]))[0]:.2e}")

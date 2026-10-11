# SPDX-License-Identifier: MIT
"""Generate the A1-T1 reference table (60-digit oscillator coefficients).

Run (not part of CI; mpmath 1.3.0 was used for the committed table):

    uv run --no-project --with mpmath==1.3.0 python engine/tests/osc/make_a1_t1_reference.py

Grid (A1-T1): 64 values of omega*dt log-spaced over [1e-4, 0.95 pi (1 - 1e-9)] and 41 values of
sigma*dt (0, then 40 log-spaced over [1e-9, 0.1]), fs = 48000. The top of the omega grid sits
1e-9 relative below 0.95 pi so that the accept/reject verdict is not decided by a double-rounding
knife edge. Inputs omega, sigma, fs are stored as the exact doubles (hexadecimal floats). The
reference values are evaluated in 60-digit arithmetic from those exact doubles with the
definitions of SPEC-OSC: sigmaHat dt = tanh(sigma dt), omegaHat^2 dt^2 = 2 - 2 (1 + sigmaHat dt)
exp(-sigma dt) cos(omega_d dt) (the cancellation-prone printed form of SI-0008, harmless at 60
digits), and beta, kappa, gamma from their definitions.
"""

import json
import pathlib

import mpmath as mp

mp.mp.dps = 60
FS = 48000.0
N_OMEGA = 64
N_SIGMA = 40
DIGITS = 30


def logspace(lo, hi, n):
    lo, hi = mp.mpf(lo), mp.mpf(hi)
    return [lo * (hi / lo) ** (mp.mpf(i) / (n - 1)) for i in range(n)]


def fmt(v):
    return mp.nstr(v, DIGITS, min_fixed=1, max_fixed=0)


def main():
    fs = mp.mpf(FS)
    dt = 1 / fs
    limit = mp.mpf("0.95") * mp.pi
    omega_dt = logspace("1e-4", limit * (1 - mp.mpf("1e-9")), N_OMEGA)
    sigma_dt = [mp.mpf(0)] + logspace("1e-9", "0.1", N_SIGMA)
    entries = []
    min_margin = mp.mpf(10)
    for wdt in omega_dt:
        omega = float(wdt * fs)
        for sdt in sigma_dt:
            sigma = float(sdt * fs)
            w, s = mp.mpf(omega), mp.mpf(sigma)
            entry = {"omega": omega.hex(), "sigma": sigma.hex(), "fs": FS.hex()}
            if s >= w:
                entry["rejected"] = True
                min_margin = min(min_margin, abs(s - w) / w)
            else:
                wd_dt = dt * mp.sqrt((w - s) * (w + s))
                min_margin = min(min_margin, abs(wd_dt - limit) / limit)
                if wd_dt > limit:
                    entry["rejected"] = True
                else:
                    x = s * dt
                    t = mp.tanh(x)
                    w2dt2 = 2 - 2 * (1 + t) * mp.exp(-x) * mp.cos(wd_dt)
                    entry["rejected"] = False
                    entry["sigmaHatDt"] = fmt(t)
                    entry["omegaHat2Dt2"] = fmt(w2dt2)
                    entry["beta"] = fmt((1 - t) / (1 + t))
                    entry["kappa"] = fmt(w2dt2 / (1 + t))
                    entry["gamma"] = fmt(dt * dt / (1 + t))
            entries.append(entry)
    assert min_margin > mp.mpf("1e-10"), min_margin
    doc = {
        "description": "A1-T1 reference: oscillator coefficients at 60-digit precision",
        "generator": "engine/tests/osc/make_a1_t1_reference.py",
        "mpmath": mp.__version__,
        "digits": DIGITS,
        "entries": entries,
    }
    out = pathlib.Path(__file__).with_name("a1_t1_reference.json")
    out.write_text(json.dumps(doc, indent=1) + "\n")
    accepted = sum(1 for e in entries if not e["rejected"])
    print(f"wrote {out}: {len(entries)} pairs, {accepted} accepted, min margin {mp.nstr(min_margin, 5)}")


if __name__ == "__main__":
    main()

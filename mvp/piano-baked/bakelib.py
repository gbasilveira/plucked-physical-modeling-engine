# SPDX-License-Identifier: MIT
"""0.6 step 2 helpers (numpy only): partial detection, band extraction, matrix-pencil fit, error metrics."""
import numpy as np

FS = 48000.0


def read_wav_float(path):
    """Minimal RIFF reader: returns (fs, array[n, ch] float64) for 16-bit PCM or 32-bit float."""
    b = open(path, 'rb').read()
    pos = 12
    fmt = None
    while pos < len(b):
        cid = b[pos:pos + 4]
        size = int.from_bytes(b[pos + 4:pos + 8], 'little')
        body = b[pos + 8:pos + 8 + size]
        if cid == b'fmt ':
            tag = int.from_bytes(body[0:2], 'little')
            ch = int.from_bytes(body[2:4], 'little')
            fs = int.from_bytes(body[4:8], 'little')
            bits = int.from_bytes(body[14:16], 'little')
            fmt = (tag, ch, fs, bits)
        elif cid == b'data':
            tag, ch, fs, bits = fmt
            if tag == 3 and bits == 32:
                x = np.frombuffer(body, dtype='<f4').astype(np.float64)
            elif tag == 1 and bits == 16:
                x = np.frombuffer(body, dtype='<i2').astype(np.float64) / 32768.0
            else:
                raise ValueError('unsupported wav')
            return fs, x.reshape(-1, ch)
        pos += 8 + size + (size & 1)
    raise ValueError('no data')


def parabolic(mag, i):
    a, b, c = np.log(mag[i - 1] + 1e-300), np.log(mag[i] + 1e-300), np.log(mag[i + 1] + 1e-300)
    d = a - 2 * b + c
    return i + (0.5 * (a - c) / d if d != 0 else 0.0)


def find_partials(F, key, t0=0.15, t1=1.5, floor_db=-85.0, nfft=1 << 20, kmax=140):
    """Peak-pick partial frequencies of the bridge force: returns list of (k, f_hz, mag) with a tracked (f0, B) model."""
    seg = F[int(t0 * FS):int(t1 * FS)]
    mag = np.abs(np.fft.rfft(seg * np.hanning(len(seg)), nfft))
    df = FS / nfft
    gmax = mag.max()
    thr = gmax * 10 ** (floor_db / 20)
    f0n = 440.0 * 2 ** ((key - 69) / 12.0)
    found = []
    f0, B = f0n, 0.0
    misses = 0
    for k in range(1, kmax + 1):
        fp = k * f0 * np.sqrt(1 + B * k * k)
        if fp > 19000:
            break
        hw = 0.03 * fp if k == 1 else min(0.3 * f0, max(1.5, 0.006 * fp))
        lo, hi = int((fp - hw) / df), int((fp + hw) / df)
        i = lo + int(np.argmax(mag[lo:hi + 1]))
        if i <= 1 or i >= len(mag) - 2 or mag[i] < thr:
            misses += 1
            if misses >= 6:
                break
            continue
        misses = 0
        fi = parabolic(mag, i) * df
        found.append((k, fi, mag[i]))
        if len(found) < 3:   # too few for the (f0, B) fit: take f0 from this partial
            f0 = fi / (k * np.sqrt(1 + B * k * k))
        if len(found) >= 3:   # f_k^2 / k^2 = f0^2 (1 + B k^2), weighted LS
            ks = np.array([x[0] for x in found], float)
            fs_ = np.array([x[1] for x in found], float)
            w = np.array([x[2] for x in found], float)
            A = np.vstack([np.ones_like(ks), ks ** 2]).T
            coef, *_ = np.linalg.lstsq(A * w[:, None] ** 0.25, (fs_ ** 2 / ks ** 2) * w ** 0.25, rcond=None)
            if coef[0] > 0:
                f0 = np.sqrt(coef[0])
                B = max(coef[1] / coef[0], 0.0)
    return found, f0, B


def band_signal(x, fc, W, taper=0.2):
    """Complex amplitude envelope a(t) of the content of the real signal x within fc +- W Hz (analytic, demodulated to
    baseband and decimated). Returns (y, dt, fcb): x_band(t) = Re(y(t) exp(i 2 pi fcb t))."""
    N = len(x)
    X = np.fft.fft(x)
    df = FS / N
    kc = int(round(fc / df))
    K = int(W / df)
    Ld = 1
    while Ld < 1.15 * (2 * K + 1):
        Ld <<= 1
    j = np.arange(-K, K + 1)
    win = np.ones(len(j))
    e = max(int(taper * K), 1)
    ramp = 0.5 * (1 - np.cos(np.pi * (np.arange(e) + 0.5) / e))
    win[:e] = ramp
    win[-e:] = ramp[::-1]
    Xs = np.zeros(Ld, complex)
    Xs[j % Ld] = X[kc + j] * win
    y = np.fft.ifft(Xs) * Ld * (2.0 / N)
    return y, N / (Ld * FS), kc * df


def matrix_pencil(y, dt, max_order=4, tol=0.01):
    """Fit y[n] ~ sum a_m z_m^n. Returns (s = log z / dt, a). Smallest order with relative residual < tol."""
    L = len(y)
    P = max(min(L // 3, 160), max_order + 2)
    H = np.array([y[i:i + P + 1] for i in range(L - P)])
    U, S, Vh = np.linalg.svd(H, full_matrices=False)
    best = None
    for M in range(1, max_order + 1):
        V = Vh[:M].T
        V1, V2 = V[:-1], V[1:]
        z = np.linalg.eigvals(np.linalg.pinv(V1) @ V2)
        z = z / np.maximum(1.0, np.abs(z)) * np.minimum(1.0, np.abs(z)) if False else z
        n = np.arange(L)
        Vd = np.exp(np.outer(n, np.log(z.astype(complex))))
        a, *_ = np.linalg.lstsq(Vd, y, rcond=None)
        res = np.linalg.norm(y - Vd @ a) / (np.linalg.norm(y) + 1e-300)
        best = (z, a, res)
        if res < tol:
            break
    z, a, res = best
    return np.log(z.astype(complex)) / dt, a, res


def fit_force(F, key, ta=0.1, tend=7.0, prune=1e-6, max_order=4, tol=0.01, verbose=False):
    """Fit the force after ta as damped complex sinusoids. Returns dict(components=[(f, sigma, a_complex_at_ta)], partials=...)."""
    N = len(F)
    Fw = F.copy()
    taper_from = int(tend * FS)
    n = N - taper_from
    Fw[taper_from:] *= 0.5 * (1 + np.cos(np.pi * np.arange(n) / n))   # smooth end: no wrap-around discontinuity
    partials, f0, B = find_partials(F, key)
    comps = []
    info = []
    for idx, (k, fk, mg) in enumerate(partials):
        prev = partials[idx - 1][1] if idx else fk - f0
        nxt = partials[idx + 1][1] if idx + 1 < len(partials) else fk + f0
        spacing = min(fk - prev, nxt - fk)
        W = min(0.45 * spacing, 20.0)
        y, dt, fcb = band_signal(Fw, fk, W)
        m0 = int(np.ceil(ta / dt))
        m1 = int(tend / dt)
        yy = y[m0:m1]
        s, a, res = matrix_pencil(yy, dt, max_order, tol)
        t_m0 = m0 * dt
        for sm, am in zip(s, a):
            sigma = -sm.real
            if sigma < 0.02:
                sigma = 0.02
            Om = sm.imag
            if abs(Om) > 2 * np.pi * W * 1.05:
                continue
            amp_ta = am * np.exp(sm * (ta - t_m0)) * np.exp(2j * np.pi * fcb * ta)   # complex amplitude at t = ta incl. the carrier phase
            comps.append((k, fcb + Om / (2 * np.pi), sigma, amp_ta))
        info.append(dict(k=k, f=fk, W=W, order=len(s), residual=float(res)))
    energy = np.array([abs(c[3]) ** 2 / (2 * c[2]) for c in comps])
    keep = energy >= prune * energy.sum()
    comps_k = [c for c, kp in zip(comps, keep) if kp]
    return dict(components=comps_k, all_components=len(comps), partials=partials, f0=f0, B=B, info=info, ta=ta)


def synth_force(fit, F, nfade=960, ta_s=None, n=None):
    """Double-precision resynthesis: attack table of the Full force up to ta+nfade, crossfaded to the modal sum."""
    N = n or len(F)
    ta = int(round(fit['ta'] * FS))
    t = (np.arange(N) - ta) / FS
    out = np.zeros(N)
    comps = fit['components']
    f = np.array([c[1] for c in comps]); sg = np.array([c[2] for c in comps]); am = np.array([c[3] for c in comps])
    for i0 in range(ta, N, 20000):
        i1 = min(N, i0 + 20000)
        tt = t[i0:i1]
        z = am[None, :] * np.exp((-sg[None, :] + 2j * np.pi * f[None, :]) * tt[:, None])
        out[i0:i1] = z.real.sum(1)
    w = np.zeros(N)
    w[ta + nfade:] = 1.0
    r = np.arange(nfade)
    w[ta:ta + nfade] = 0.5 * (1 - np.cos(np.pi * (r + 0.5) / nfade))
    table = F.copy()
    return (1 - w) * table + w * out


def band_edges():
    return [63 * 2 ** i for i in range(8)]   # centres 63 .. 8064


def octave_envelopes(x, centres=None, hop=0.05):
    """RMS envelopes (dB) per octave band, 50 ms frames, via FFT band-pass. Returns (centres, frames_t, env[band, frame])."""
    centres = centres or band_edges()
    N = len(x)
    X = np.fft.rfft(x)
    f = np.fft.rfftfreq(N, 1 / FS)
    H = int(hop * FS)
    nfr = N // H
    env = np.zeros((len(centres), nfr))
    for b, c in enumerate(centres):
        m = (f >= c / np.sqrt(2)) & (f < c * np.sqrt(2))
        xb = np.fft.irfft(X * m, N)
        env[b] = 10 * np.log10(np.maximum((xb[:nfr * H].reshape(nfr, H) ** 2).mean(1), 1e-30))
    return centres, (np.arange(nfr) + 0.5) * hop, env


def octave_envelope_error(ref, test, t0=0.1, t1=8.0, floor_db=-60.0):
    """Per band: rms dB difference of the 50 ms envelopes over [t0, t1], frames where the reference band level is above
    floor_db re the loudest frame of the loudest band. Returns dict band -> (rms_dB, mean_dB, frames)."""
    c, t, er = octave_envelopes(ref)
    _, _, et = octave_envelopes(test)
    top = er.max()
    out = {}
    for b, cc in enumerate(c):
        m = (t >= t0) & (t < t1) & (er[b] > top + floor_db)
        if m.sum() == 0:
            out[cc] = (float('nan'), float('nan'), 0)
            continue
        d = et[b][m] - er[b][m]
        out[cc] = (float(np.sqrt((d ** 2).mean())), float(d.mean()), int(m.sum()))
    return out

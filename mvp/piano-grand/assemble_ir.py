# SPDX-License-Identifier: MIT
"""Assemble the board-mode -> microphone transfer data for one lid state from the reciprocal FDTD runs and the
high-frequency Rayleigh model; write what the offline renderer needs.

For every board mode i and microphone m the pressure per unit generalised force is gamma_{i,m}(t) = -V_i(t)/Q (reciprocity,
tests/test_fdtd_reciprocity.py). Below the crossover (1.4 to 2.0 kHz, raised cosine) it comes from the FDTD (air loading,
rim, cavity, lid, floor included, 1 s record faded out over the last 0.15 s); above it from the baffled Rayleigh integral with lid
reflection (rayleigh.py). The result is stored in port space: E[mic, port, lag] = sum_i phi[port, i] gamma_{i,mic}[lag], so the
renderer needs 88 FIRs per microphone. Also written: the air-loaded frequency and decay rate of the low modes (for the strings' board).
Usage: assemble_ir.py WORK MODES.npz LID OUTPREFIX"""
import sys, json, math, time
from pathlib import Path
import numpy as np
from scipy.interpolate import LinearNDInterpolator
import scene, rayleigh

FS = 48000.0
FS_F = 24000.0
DT_F = 1/FS_F
T0 = 1.00                      # seconds kept (the FDTD record length)
N_E = 1 << 16                  # synthesis grid at 48 kHz (1.37 s, df 0.73 Hz)
F_X1, F_X2 = 1400.0, 2000.0   # crossover FDTD -> Rayleigh (raised cosine)
LOAD_SIGMA = 30.0              # loaded poles are estimated for modes with in-vacuo sigma below this (f < 480 Hz)
FADE = 0.15                    # s, fade-out at the end of the FDTD record

def w_lp(f):
    x = np.clip((f - F_X1)/(F_X2 - F_X1), 0, 1)
    return 0.5 + 0.5*np.cos(np.pi*x)

def load_run(work, lid, mic, n):
    base = Path(work)/f'run_{lid}_{mic}'
    meta = dict(l.split('=') for l in open(str(base)+'.meta.txt').read().split())
    steps, nm = int(meta['steps']), int(meta['nmodes'])
    V = np.fromfile(str(base)+'.vmodes.f32', np.float32).reshape(steps, nm).astype(np.float64)
    q = np.fromfile(str(base)+'.qsrc.f32', np.float32).astype(np.float64)
    return V[:, :n], q

def deconvolve(V, q):
    """gamma = -V/Q per mode, time domain at 24 kHz (the discrete IR carries the sample period; valid below ~6 kHz)."""
    T = V.shape[0]; nf = 1 << int(math.ceil(math.log2(T + 256)))
    Q = np.fft.rfft(q, nf); f = np.fft.rfftfreq(nf, DT_F)
    taper = np.clip((6000.0 - f)/1000.0, 0, 1)
    H = -np.fft.rfft(V, nf, axis=0)/Q[:, None]*taper[:, None]
    return np.fft.irfft(H, nf, axis=0)[:T]

def loaded_poles(gam, mics, f_modes, sigma_v, Nf):
    """Air-loaded frequency and decay rate of the low modes from the narrow-band ring of their own modal velocity: band-pass
    around the spectral peak, analytic signal, linear fit of log amplitude (sigma) and unwrapped phase (omega) over
    0.15 s .. 0.15 s + 3/sigma. Vacuum values are kept when a neighbour mode is closer than the pass band or the envelope is not a
    clean exponential (rms deviation of the log amplitude above 15 % of its range)."""
    f = f_modes[:Nf].copy(); sg = sigma_v[:Nf].copy(); ok = np.zeros(Nf, bool)
    T = gam[mics[0]].shape[0]; nf = 1 << 17
    fr = np.fft.rfftfreq(nf, DT_F)
    for i in range(Nf):
        if sigma_v[i] > LOAD_SIGMA: continue
        S = sum(np.abs(np.fft.rfft(gam[m][:, i], nf))**2 for m in mics)
        sel = (fr > 0.6*f_modes[i]) & (fr < 1.25*f_modes[i]) if i > 0 else (fr > 20) & (fr < 80)
        fpk = fr[sel][np.argmax(S[sel])]
        gaps = np.abs(f_modes - f_modes[i]); gaps[i] = 1e9
        half = min(1.5*sigma_v[i]/np.pi + 2.0, 0.4*gaps.min())
        if half < sigma_v[i]/np.pi: continue
        band = (fr > fpk - half) & (fr < fpk + half)
        t0 = int(0.15*FS_F); t1 = min(T - 1, t0 + int(3.0/max(sigma_v[i], 3.0)*FS_F))
        if t1 - t0 < 600: continue
        best = None; bestamp = 0
        for m in mics:
            Xa = np.zeros(nf, complex); Xf = np.fft.rfft(gam[m][:, i], nf); Xa[:len(Xf)] = np.where(band, Xf*2, 0)
            an = np.fft.ifft(Xa)[:T]
            a = np.abs(an[t0:t1]).max()
            if a > bestamp: best, bestamp = an, a
        t = np.arange(t0, t1)*DT_F
        la = np.log(np.abs(best[t0:t1]) + 1e-300); ph = np.unwrap(np.angle(best[t0:t1]))
        pa = np.polyfit(t, la, 1); pp = np.polyfit(t, ph, 1)
        res = np.sqrt(np.mean((la - np.polyval(pa, t))**2))/max(la.max() - la.min(), 1e-9)
        if res > 0.15 or pa[0] >= 0 or pp[0] <= 0: continue
        f[i] = pp[0]/(2*np.pi); sg[i] = -pa[0]; ok[i] = True
    return f, sg, ok

def main(work, modes_npz, lid, outprefix, mic_list=None):
    t_start = time.time()
    work = Path(work)
    d = np.load(modes_npz); f_modes = d['f']; Nall = len(f_modes)
    phi_keys = d['phi']                     # (88, Nall)
    omega_v = 2*np.pi*f_modes; sigma_v = 0.5*0.02*omega_v
    mics = list(scene.MICS) if mic_list is None else mic_list.split(',')
    info = json.loads((work/f'scene_{lid}'/'info.json').read_text()); Nf = info['modes_coupled']
    gam = {}
    for m in mics:
        V, q = load_run(work, lid, m, Nf); gam[m] = deconvolve(V, q)
    T = gam[mics[0]].shape[0]
    f_loaded, sig_loaded, ok_loaded = loaded_poles(gam, mics, f_modes, sigma_v, Nf)
    print(f'{lid}: {Nf} FDTD modes; loaded poles for {ok_loaded.sum()} modes: f_loaded/f_vacuum ' +
          ' '.join(f'{a/b:.3f}' for a, b in zip(f_loaded[ok_loaded][:12], f_modes[:Nf][ok_loaded][:12])) + '; sigma_loaded/sigma_vacuum ' +
          ' '.join(f'{a/b:.2f}' for a, b in zip(sig_loaded[ok_loaded][:12], sigma_v[:Nf][ok_loaded][:12])), flush=True)
    solid = np.fromfile(work/f'scene_{lid}'/'solid.u8', np.uint8).reshape(scene.NZ, scene.NY, scene.NX)
    X, Y, dA = rayleigh.surface_points(0.012)
    interp = LinearNDInterpolator(d['nodes'], d['W'].astype(np.float64))
    Phi_pts = np.nan_to_num(interp(np.stack([X, Y], 1))).T            # (Nall, npts)
    fgrid = np.arange(1000.0, 8001.0, 10.0)
    f48 = np.fft.rfftfreq(N_E, 1/FS); w48 = 2*np.pi*f48
    nT0 = int(T0*FS)
    E = np.zeros((len(mics), 88, nT0), np.float32)
    diag = {}
    # FDTD record: fade out the last FADE seconds
    nf24 = N_E//2
    fade = np.ones(nf24); i0 = T - int(FADE*FS_F); fade[i0:T] = 0.5 + 0.5*np.cos(np.pi*np.arange(T - i0)/(T - i0)); fade[T:] = 0
    wl = w_lp(f48)
    Y_i = 1j*w48[:, None]/(omega_v[None, :]**2 - w48[:, None]**2 + 2j*sigma_v[None, :]*w48[:, None])
    for mi, m in enumerate(mics):
        K = rayleigh.kernels(lid, m, fgrid, solid, Phi_pts, X, Y, dA)      # (nf, Nall): pressure per unit modal velocity
        r0 = np.linalg.norm(np.array(scene.MICS[m]) - np.array([0.6, 1.1, 0.625]))
        tau0 = max(r0 - 0.7, 0.0)/rayleigh.C                                 # remove the bulk delay before interpolating
        Kd = K*np.exp(1j*2*np.pi*fgrid*tau0)[:, None]
        sel = (f48 >= fgrid[0]) & (f48 <= fgrid[-1])
        Ki = np.zeros((len(f48), Nall), complex)
        for i in range(Nall):
            Ki[sel, i] = np.interp(f48[sel], fgrid, Kd[:, i].real) + 1j*np.interp(f48[sel], fgrid, Kd[:, i].imag)
        Ki *= np.exp(-1j*w48*tau0)[:, None]
        GRn = Ki*Y_i                                                        # Rayleigh transfer function, all frequencies in 1-8 kHz
        g = np.zeros((nf24, Nf)); g[:T] = gam[m]*fade[:T, None]
        RF = np.fft.rfft(g, nf24, axis=0)                                  # df = 24000/nf24 = 48000/N_E: the same grid as f48
        GF0 = np.zeros((len(f48), Nall), complex); GF0[:RF.shape[0], :Nf] = RF
        Gs = GF0*wl[:, None] + GRn*(1 - wl)[:, None]
        e = np.fft.irfft(Gs, N_E, axis=0)[:nT0]                           # (nT0, Nall) discrete IR at 48 kHz
        E[mi] = (phi_keys @ e.T).astype(np.float32)
        diag[m] = dict(peak=float(np.abs(E[mi]).max()), shadowed_fraction_of_surface=None)
        for lo in (1000, 1260, 1587):                                       # FDTD against Rayleigh where both are used (third octaves)
            hi = lo*2**(1/3); s_ = (f48 >= lo) & (f48 < hi)
            pf = np.sum(np.abs(GF0[s_, :Nf])**2); pr = np.sum(np.abs(GRn[s_, :Nf])**2)
            # pr == 0 when every Rayleigh path is shadowed (no line of sight): the ratio is undefined, not large
            diag[m][f'fdtd_over_rayleigh_dB_{lo}'] = float(10*np.log10(pf/pr)) if pr > 1e-12*pf else None
        print(f'  {lid} {m}: {time.time()-t_start:.0f}s ', {k: (None if v is None else float('%.3g' % v)) for k, v in diag[m].items()}, flush=True)
    np.savez(outprefix + f'_{lid}.npz', E=E, mics=np.array(mics), f_loaded=f_loaded, sigma_loaded=sig_loaded, ok_loaded=ok_loaded)
    om = omega_v.copy(); sg = sigma_v.copy()
    om[:Nf] = 2*np.pi*f_loaded; sg[:Nf] = sig_loaded
    np.savez(outprefix + f'_{lid}_poles.npz', omega=om, sigma=sg, omega_vacuum=omega_v, sigma_vacuum=sigma_v, ok=ok_loaded)
    Path(outprefix + f'_{lid}_diag.json').write_text(json.dumps(dict(
        lid=lid, Nf=int(Nf), loaded_modes=int(ok_loaded.sum()),
        loaded_over_vacuum_frequency=[float(a/b) for a, b in zip(f_loaded[ok_loaded], f_modes[:Nf][ok_loaded])],
        loaded_over_vacuum_sigma=[float(a/b) for a, b in zip(sig_loaded[ok_loaded], sigma_v[:Nf][ok_loaded])],
        loaded_mode_index=[int(i) for i in np.where(ok_loaded)[0]], per_mic=diag), indent=1))
    print('assembled', lid, f'{time.time()-t_start:.0f}s')

if __name__ == '__main__':
    main(*sys.argv[1:6])

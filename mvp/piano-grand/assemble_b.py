# SPDX-License-Identifier: MIT
"""Piano 0.7b: assemble the port -> microphone impulse responses E[mic, port, lag] of one lid state.

Per microphone the pressure per unit generalised force on board mode i is spliced across frequency (raised cosines, complex):
  coarse FDTD (dx 25 mm, trusted below ~1 kHz)  ->  fine FDTD (dx 12.5 mm, close microphones, trusted to ~2.2 kHz)  ->  Rayleigh + diffraction.
The Rayleigh part is the FEM modes' (to 3.6 kHz) and the statistical modes' (3.6 to 10 kHz, hfboard.py) radiation through the same path model
(rayleigh_b.py: direct + lid image, Fresnel knife-edge weights for obstructed legs, iron plate as an obstacle).
Variant switches (for the ablation): plate (use the plate scene / runs), fine, diffraction (False = the 0.7a rule: obstructed leg = 0), hf (add statistical modes).
Usage: assemble_b.py WORK MODES.npz HF.npz LID OUTPREFIX VARIANT [mics]     VARIANT = v1 (0.7a geometry + HF modes) | full
"""
import sys, json, math, time
from pathlib import Path
import numpy as np
from scipy.interpolate import LinearNDInterpolator
import scene_b as S, rayleigh_b as RB, hfboard as H

FS = 48000.0
N_E = 1 << 16
T0 = 1.0
FADE = 0.15
X_COARSE = (900.0, 1300.0)          # coarse -> fine (fine available)
X_FINE = (2200.0, 3000.0)           # fine -> Rayleigh
X_COARSE_ONLY = (1400.0, 2000.0)    # coarse -> Rayleigh (no fine run)
HF_GROUPS = [(0.0, 10000.0, 0.006), (10000.0, 1.0e9, 0.004)]   # (f_lo, f_hi, surface spacing)

def cos_ramp(f, f1, f2):
    x = np.clip((f - f1)/(f2 - f1), 0, 1)
    return 0.5 + 0.5*np.cos(np.pi*x)       # 1 below f1, 0 above f2

def load_run(work, name, n):
    base = Path(work)/name
    meta = dict(l.split('=') for l in open(str(base) + '.meta.txt').read().split())
    steps, nm = int(meta['steps']), int(meta['nmodes'])
    V = np.fromfile(str(base) + '.vmodes.f32', np.float32).reshape(steps, nm).astype(np.float64)
    q = np.fromfile(str(base) + '.qsrc.f32', np.float32).astype(np.float64)
    return V[:, :n], q, float(meta['dt'])

def gamma_spectrum(V, q, dt, nfft):
    """Pressure per unit force on each mode, FFT on the bins of an nfft-point transform at the run's rate; faded record."""
    T = V.shape[0]; fs = 1/dt
    fade = np.ones(T); i0 = T - int(FADE*fs); fade[i0:] = 0.5 + 0.5*np.cos(np.pi*np.arange(T - i0)/(T - i0))
    Q = np.fft.rfft(q, nfft); f = np.fft.rfftfreq(nfft, dt)
    H = -np.fft.rfft(V*fade[:, None], nfft, axis=0)/Q[:, None]
    H *= np.clip((0.45*fs - f)/(0.05*fs), 0, 1)[:, None]
    return H

def rayleigh_fem(paths, Phi_pts, dA, fgrid, tau0):
    """K[f, mode] pressure per unit modal velocity, bulk delay removed (smooth in f)."""
    K = np.zeros((len(fgrid), Phi_pts.shape[0]), complex)
    for fi, f in enumerate(fgrid):
        acc = paths.weights(f)
        K[fi] = (Phi_pts @ (acc*dA))*(1j*2*math.pi*f*RB.RHO/(2*math.pi))*np.exp(1j*2*math.pi*f*tau0)
    return K

def main(work, modes_npz, hf_npz, lid, outprefix, variant, mic_list=None):
    t_start = time.time(); work = Path(work)
    full = variant in ('full', 'fullnc'); match = variant == 'full'     # fullnc: no Rayleigh-to-FDTD level matching (ablation)
    plate, fine_ok, diffraction = full, full, full
    d = np.load(modes_npz); f_fem = d['f']; Nall = len(f_fem); phi_fem = d['phi']
    omega_v = 2*np.pi*f_fem; sigma_v = 0.5*0.02*omega_v
    mics = list(S.MICS) if mic_list is None else mic_list.split(',')
    hf = H.load_hf_set(hf_npz); sb = hf['sb']; phi_hf = hf['phi']
    f48 = np.fft.rfftfreq(N_E, 1/FS); w48 = 2*np.pi*f48; nT0 = int(T0*FS)
    sc = RB.Scene(lid, plate=plate)
    # surface sampling
    X12, Y12, dA12 = RB.surface_points(0.012)
    interp = LinearNDInterpolator(d['nodes'], d['W'].astype(np.float64))
    Phi12 = np.nan_to_num(interp(np.stack([X12, Y12], 1))).T
    fgrid = np.arange(1000.0, 5001.0, 10.0)
    # statistical mode shapes on the surface grids (6 mm below 10 kHz, 4 mm above: the Rayleigh integrand must be sampled at < lambda_b / 2), mass-normalised
    Nh = len(sb.f); groups = []
    for (flo, fhi, sp) in HF_GROUPS:
        idx = np.where((sb.f >= flo) & (sb.f < fhi))[0]
        if not len(idx): continue
        Xg, Yg, dAg = RB.surface_points(sp)
        cache = Path(hf_npz).with_suffix(f'.shapes{int(sp*1000)}mm.npy')
        if cache.exists() and np.load(cache, mmap_mode='r').shape == (len(idx), len(Xg)):
            Pg = np.load(cache)
        else:
            hs = S.geo.thickness(Xg, Yg); ds_ = S.geo.edge_distance(Xg, Yg); Pg = np.zeros((len(idx), len(Xg)), np.float32)
            for i0 in range(0, len(idx), 20):
                ii = idx[i0:i0 + 20]; Pg[i0:i0 + 20] = sb.shape(ii, Xg, Yg, ds_, hs).astype(np.float32)
            np.save(cache, Pg)
        groups.append((idx, Xg, Yg, dAg, Pg))
    print(f'{lid}/{variant}: HF shapes on the surface grids done {time.time()-t_start:.0f}s', flush=True)
    Yfem = 1j*w48[:, None]/(omega_v[None, :]**2 - w48[:, None]**2 + 2j*sigma_v[None, :]*w48[:, None])
    om_h = 2*np.pi*sb.f; sg_h = 0.5*0.02*om_h
    E = np.zeros((len(mics), 88, nT0), np.float32); diag = {}
    for mi, m in enumerate(mics):
        r0 = np.linalg.norm(np.array(S.MICS[m]) - np.array([0.6, 1.1, 0.625])); tau0 = max(r0 - 0.7, 0.0)/RB.C
        # ---- FDTD spectra
        cname = f'run_{lid}P_{m}' if plate else f'run_{lid}_{m}'
        Vc, qc, dtc = load_run(work, cname, 191)
        Gc = gamma_spectrum(Vc, qc, dtc, N_E//2)                          # 24 kHz rate: bins match f48[:N_E//4+1+...]
        GcF = np.zeros((len(f48), Nall), complex); GcF[:Gc.shape[0], :Gc.shape[1]] = Gc
        have_fine = fine_ok and (work/f'runF_{lid}P_{m}.meta.txt').exists()
        GfF = None
        if have_fine:
            Vf, qf, dtf = load_run(work, f'runF_{lid}P_{m}', 286)
            Gf = gamma_spectrum(Vf, qf, dtf, N_E)
            GfF = np.zeros((len(f48), Nall), complex); GfF[:, :Gf.shape[1]] = Gf
            wc = cos_ramp(f48, *X_COARSE); wf = cos_ramp(f48, *X_FINE) - wc
        else:
            wc = cos_ramp(f48, *X_COARSE_ONLY); wf = np.zeros_like(wc)
        wr = 1 - wc - wf
        # ---- Rayleigh for the FEM modes
        P12 = RB.Paths(sc, m, X12, Y12)
        if not diffraction: P12.weights = _binary_weights(P12)
        K = rayleigh_fem(P12, Phi12, dA12, fgrid, tau0)
        Ki = np.zeros((len(f48), Nall), complex); sel = (f48 >= fgrid[0]) & (f48 <= fgrid[-1])
        for i in range(Nall):
            Ki[sel, i] = np.interp(f48[sel], fgrid, K[:, i].real) + 1j*np.interp(f48[sel], fgrid, K[:, i].imag)
        below = f48 < fgrid[0]
        Ki[~sel & ~below] = Ki[np.where(sel)[0][-1]]
        Ki *= np.exp(-1j*w48*tau0)[:, None]
        GR = Ki*Yfem
        corr = 1.0
        if match:
            # Rayleigh + diffraction is a ray model of the open board; the wave solver also has the rim tray, lid and plate. Match the model to the best FDTD
            # where both are valid (all converged FEM modes, no recording): fine grid 1.0-2.5 kHz, coarse grid 1.0-1.26 kHz otherwise.
            bands = (1000, 1260, 1587, 2000, 2520) if have_fine else (1000, 1260)
            ref_ = GfF if have_fine else GcF
            ratios = []
            for lo in bands:
                s_ = (f48 >= lo) & (f48 < lo*2**(1/3)); pr = np.sum(np.abs(GR[s_, :191])**2); pf = np.sum(np.abs(ref_[s_, :191])**2)
                if pr > 0: ratios.append(10*np.log10(pf/pr))
            corr_dB = float(np.mean(ratios)); corr = 10**(corr_dB/20); GR = GR*corr
        Gfem = GcF*wc[:, None] + (GfF*wf[:, None] if GfF is not None else 0) + GR*wr[:, None]
        Ef = Gfem @ phi_fem.T                                              # (nf, 88)
        # ---- statistical modes (Rayleigh only, narrow band)
        if hf is not None and variant in ('v1', 'full', 'fullnc'):
            A = np.zeros(Nh, complex)
            for (idx, Xg, Yg, dAg, Pg) in groups:
                Pp = RB.Paths(sc, m, Xg, Yg)
                if not diffraction: Pp.weights = _binary_weights(Pp)
                for q, i in enumerate(idx):
                    acc = Pp.weights(sb.f[i])
                    A[i] = corr*(Pg[q].astype(np.float64) @ acc)*dAg*(1j*om_h[i]*RB.RHO/(2*math.pi))*np.exp(1j*om_h[i]*tau0)
            for i0 in range(0, Nh, 200):
                ii = slice(i0, min(Nh, i0 + 200))
                Yh = 1j*w48[:, None]/(om_h[None, ii]**2 - w48[:, None]**2 + 2j*sg_h[None, ii]*w48[:, None])
                Ef += (Yh*A[None, ii]*np.exp(-1j*w48*tau0)[:, None]) @ phi_hf[:, ii].T
        E[mi] = np.fft.irfft(Ef, N_E, axis=0)[:nT0].T.astype(np.float32)
        # diagnostics: FDTD against the Rayleigh model for the FEM modes where both are used (third-octave bands)
        dg = {}
        for lo in (1000, 1260, 1587, 2000, 2520):
            hi = lo*2**(1/3); s_ = (f48 >= lo) & (f48 < hi)
            pr = np.sum(np.abs(GR[s_, :191])**2)/corr**2; pc = np.sum(np.abs(GcF[s_, :191])**2)
            dg[f'coarse_over_rayleigh_dB_{lo}'] = float(10*np.log10(pc/pr)) if pr > 1e-12*pc else None
            if GfF is not None:
                pf = np.sum(np.abs(GfF[s_, :191])**2)
                dg[f'fine_over_rayleigh_dB_{lo}'] = float(10*np.log10(pf/pr)) if pr > 1e-12*pf else None
                dg[f'fine_over_coarse_dB_{lo}'] = float(10*np.log10(pf/pc)) if pc > 0 else None
        dg['rayleigh_to_fdtd_correction_dB'] = 20*float(np.log10(corr)); dg['peak'] = float(np.abs(E[mi]).max()); dg['fine_used'] = bool(have_fine); diag[m] = dg
        print(f'  {lid} {m}: {time.time()-t_start:.0f}s', {k: (None if v is None else float("%.3g" % v)) for k, v in dg.items()}, flush=True)
    np.savez(outprefix + f'_{lid}.npz', E=E, mics=np.array(mics))
    Path(outprefix + f'_{lid}_diag.json').write_text(json.dumps(dict(lid=lid, variant=variant, per_mic=diag), indent=1))
    print('assembled', lid, variant, f'{time.time()-t_start:.0f}s')

def _binary_weights(paths):
    """The 0.7a rule: an obstructed leg contributes nothing."""
    def weights(f):
        k = 2*math.pi*f/RB.C; acc = np.zeros(len(paths.r[0]), complex)
        for r, legs in zip(paths.r, paths.legs):
            ok = np.isfinite(r)
            for c in legs: ok &= (c <= RB.H_CLEAR + 1)
            acc[ok] += np.exp(-1j*k*r[ok])/r[ok]
        return acc
    return weights

if __name__ == '__main__':
    main(*sys.argv[1:7], *(sys.argv[7:8]))

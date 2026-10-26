# SPDX-License-Identifier: MIT
"""Piano 0.7b: continuity of the board model across the end of the converged FEM band. Two quantities, third-octave bands:
 (a) the driving-point mobility Re Y_k(f) of the combined modal set (FEM modes + statistical continuation) at the string points, median and spread over keys;
 (b) the radiated mean-square pressure per unit port force at one microphone, from the FEM modes and from a statistical set built for the same band
     (2.2-3.6 kHz), both through the same Rayleigh + diffraction model: a test of the statistical shapes, density and calibration against the FEM,
     which are not fitted to it (the only input from the FEM is the per-key mobility target of the 2.2-3.2 kHz band).
Usage: hf_continuity.py WORK MODES.npz HF.npz OUT.json [lid] [mic]"""
import sys, json, math, time
import numpy as np
from scipy.interpolate import LinearNDInterpolator
import hfboard as H, rayleigh_b as RB, scene_b as S, geometry as geo

def band_edges(lo, hi):
    return H.third_octave_edges(lo, hi)

def main(work, modes_npz, hf_npz, out, lid='open', mic='closeL'):
    d = np.load(modes_npz); f = d['f']; phi = d['phi']; hf = H.load_hf_set(hf_npz); sb = hf['sb']
    res = {}
    # (a) mobility
    edges = band_edges(1600, 6000); fc = np.sqrt(edges[:-1]*edges[1:])
    fe = np.geomspace(1600, 6000, 60)
    Yf = H.fem_mobility(f, phi, 0.01, fe)
    Ys = H.fem_mobility(sb.f, hf['phi'], 0.01, fe)
    Yall = Yf + Ys
    rows = []
    for lo, hi in zip(edges[:-1], edges[1:]):
        s = (fe >= lo) & (fe < hi)
        if not s.any(): continue
        m = Yall[:, s].mean(axis=1); rows.append((math.sqrt(lo*hi), float(np.median(10*np.log10(m))), float(np.percentile(10*np.log10(m), 10)), float(np.percentile(10*np.log10(m), 90))))
    res['mobility_dB_re_1m_per_sN_median_p10_p90'] = rows
    # step across 3.6 kHz: median over keys of the mean over [3.0, 3.5] kHz vs [3.7, 4.2] kHz
    def band_db(lo, hi):
        s = (fe >= lo) & (fe < hi); return 10*np.log10(Yall[:, s].mean(axis=1))
    res['mobility_step_dB_3p7_4p2_minus_3_3p5'] = float(np.median(band_db(3700, 4200) - band_db(3000, 3500)))
    # (b) radiated power in 2.2-3.6 kHz
    print('building statistical set for 2.2-3.6 kHz', flush=True)
    hft = H.build_hf_set(modes_npz, 3600.0, seed=11, f_lo=2200.0)
    sbt = hft['sb']
    sc = RB.Scene(lid, plate=True)
    X, Y, dA = RB.surface_points(0.012); P = RB.Paths(sc, mic, X, Y)
    interp = LinearNDInterpolator(d['nodes'], d['W'].astype(np.float64)); Phi_pts = np.nan_to_num(interp(np.stack([X, Y], 1))).T
    hs = geo.thickness(X, Y); ds = geo.edge_distance(X, Y)
    fgrid = np.arange(2200.0, 3601.0, 25.0)
    def power(modes_f, phi_ports, shapes):
        om = 2*np.pi*modes_f; sg = 0.5*0.02*om; Pw = np.zeros(len(fgrid))
        for a, ff in enumerate(fgrid):
            acc = P.weights(ff); K = (shapes @ (acc*dA))*(1j*2*math.pi*ff*RB.RHO/(2*math.pi))
            w = 2*math.pi*ff; Y_ = 1j*w/(om**2 - w*w + 2j*sg*w)
            E = phi_ports @ (K*Y_)
            Pw[a] = np.sum(np.abs(E)**2)
        return Pw
    sel = (f >= 1900) & (f <= 4000)
    Pf = power(f[sel], phi[:, sel], Phi_pts[sel])
    idx = np.arange(len(sbt.f)); Shp = np.zeros((len(idx), len(X)))
    for i0 in range(0, len(idx), 40):
        ii = idx[i0:i0 + 40]; Shp[ii] = sbt.shape(ii, X, Y, ds, hs)
    Ps = power(sbt.f, hft['phi'], Shp)
    rows = []
    edges = band_edges(2300, 3500)
    for lo, hi in zip(edges[:-1], edges[1:]):
        s = (fgrid >= lo) & (fgrid < hi)
        rows.append((math.sqrt(lo*hi), float(10*np.log10(Ps[s].mean()/Pf[s].mean()))))
    res['radiated_power_stat_over_fem_dB'] = rows
    res['radiated_power_stat_over_fem_mean_dB'] = float(np.mean([r[1] for r in rows]))
    res['mic'] = mic; res['lid'] = lid
    json.dump(res, open(out, 'w'), indent=1); print(json.dumps(res, indent=1))

if __name__ == '__main__':
    main(*sys.argv[1:7])

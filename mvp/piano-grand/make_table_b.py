# SPDX-License-Identifier: MIT
"""Piano 0.7b stage 4: build the air-loaded board table for the strings from the kinematic FDTD runs (runK_openP_*): impedance matrix,
exact 88 x 88 admittance, modal reduction, passivity/reciprocity report, and the 512-row table read by grand_render.
Usage: make_table_b.py WORK MODES.npz OUTPREFIX [nmodes=144]"""
import sys, json
from pathlib import Path
import numpy as np
import admittance as A

def main(work, modes_npz, outprefix, n=144):
    n = int(n); d = np.load(modes_npz); f_modes = d['f']; Phi_all = d['phi']
    om_all = 2*np.pi*f_modes; sg_all = 0.01*om_all
    runs, dt = A.load_kin_runs(work, 'runK_openP_', range(0, n, 16), 191)
    missing = [j for j in range(n) if j not in runs]
    assert not missing, f'missing kinematic runs for modes {missing[:5]}'
    f, Z = A.impedance_matrix(runs, dt, n)
    Zs = A.symmetrise(Z)
    om, sg, Phi = om_all[:n], sg_all[:n], Phi_all[:, :n]
    asym = np.linalg.norm(Z - np.swapaxes(Z, 1, 2), axis=(1, 2))/np.maximum(np.linalg.norm(Z, axis=(1, 2)), 1e-300)
    fsel = np.arange(30.0, 2000.0, 10.0)
    ia = [int(np.argmin(np.abs(f - x))) for x in fsel]
    Y = A.loaded_admittance(f, Zs[ia], om, sg, Phi, fsel)
    Y0 = A.vacuum_admittance(om, sg, Phi, fsel)
    rep = A.passivity_report(Zs[ia], Y)
    oL, sL, phiL, nonprop = A.reduce_modes(f, Zs, om, sg, Phi)
    Yr = A.vacuum_admittance(oL, sL, phiL, fsel)
    err = np.linalg.norm(Yr - Y, axis=(1, 2))/np.linalg.norm(Y, axis=(1, 2))
    err_vac = np.linalg.norm(Y0 - Y, axis=(1, 2))/np.linalg.norm(Y, axis=(1, 2))
    # band summaries
    bands = [(30, 100), (100, 250), (250, 500), (500, 1000), (1000, 2000)]
    summ = []
    for lo, hi in bands:
        s = (fsel >= lo) & (fsel < hi)
        summ.append(dict(band=[lo, hi], median_err_reduced=float(np.median(err[s])), median_err_vacuum=float(np.median(err_vac[s])),
                         median_Re_mobility_ratio=float(np.median(np.real(np.einsum('fkk->fk', Y[s]))/np.real(np.einsum('fkk->fk', Y0[s]))))))
    k = np.arange(n)
    out = dict(modes=n, z_asymmetry_median=float(np.median(asym[(f > 30) & (f < 2000)])), passivity=rep, bands=summ,
               f_loaded_over_vacuum={str(i): float(oL[i]/om[i]) for i in range(0, n, 8)}, sigma_loaded_over_vacuum={str(i): float(sL[i]/sg[i]) for i in range(0, n, 8)},
               median_f_ratio_first20=float(np.median(oL[:20]/om[:20])), median_sigma_ratio_first20=float(np.median(sL[:20]/sg[:20])),
               median_f_ratio_20_144=float(np.median(oL[20:]/om[20:])), median_sigma_ratio_20_144=float(np.median(sL[20:]/sg[20:])),
               nonproportional_median=float(np.median(nonprop)), nonproportional_max=float(nonprop.max()))
    Path(outprefix + '_report.json').write_text(json.dumps(out, indent=1))
    np.savez(outprefix + '_admittance.npz', f=fsel, Y=Y.astype(np.complex64), Y_vacuum=Y0.astype(np.complex64))
    # table for the renderer: loaded poles for the first n modes, vacuum for the rest
    N = len(f_modes); omega = om_all.copy(); sigma = sg_all.copy(); phi = Phi_all.copy()
    omega[:n], sigma[:n], phi[:, :n] = oL, sL, phiL
    tab = np.full(512, 1.0e6), np.zeros(512), np.zeros((88, 512))
    tab[0][:N] = omega; tab[1][:N] = sigma; tab[2][:, :N] = phi
    Path(outprefix + '_table.bin').write_bytes(np.concatenate([tab[0], tab[1], tab[2].ravel()]).astype('<f8').tobytes())
    print(json.dumps(out, indent=1))

if __name__ == '__main__':
    main(*sys.argv[1:5])

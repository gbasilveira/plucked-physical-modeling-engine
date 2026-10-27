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
    asym = np.concatenate([np.linalg.norm(Z[a:a+256] - np.swapaxes(Z[a:a+256], 1, 2), axis=(1, 2))/np.maximum(np.linalg.norm(Z[a:a+256], axis=(1, 2)), 1e-300) for a in range(0, len(Z), 256)])
    Zp, removed = A.enforce_passive(Z); del Z
    om, sg, Phi = om_all[:n], sg_all[:n], Phi_all[:, :n]
    fsel = np.arange(30.0, 2000.0, 10.0)
    ia = [int(np.argmin(np.abs(f - x))) for x in fsel]
    Y = A.loaded_admittance(f, Zp[ia], om, sg, Phi, fsel)
    Y0 = A.vacuum_admittance(om, sg, Phi, fsel)
    rep = A.passivity_report(Zp[ia], Y)
    oD, sD = A.diagonal_loading(f, Zp, om, sg)
    oE, sE, phiE, nonprop = A.reduce_modes(f, Zp, om, sg, Phi)
    diag_ = lambda M: np.einsum('fkk->fk', M)
    def derr(Ya): return 20*np.log10(np.abs(diag_(Ya))/np.abs(diag_(Y)))
    YD = A.vacuum_admittance(oD, sD, Phi, fsel); YE = A.vacuum_admittance(oE, sE, phiE, fsel)
    summ = []
    for lo, hi in [(30, 100), (100, 250), (250, 500), (500, 1000), (1000, 2000)]:
        s = (fsel >= lo) & (fsel < hi)
        row = dict(band=[lo, hi])
        for nm, Ya in (('vacuum', Y0), ('diagonal_loading', YD), ('eigen_reduction', YE)):
            e = derr(Ya)[s]
            row[nm] = dict(median_abs_dB_own_port=float(np.median(np.abs(e))), median_ReY_ratio=float(np.median((diag_(Ya).real/diag_(Y).real)[s])))
        summ.append(row)
    out = dict(modes=n, z_asymmetry_median=float(np.median(asym[(f > 30) & (f < 2000)])), re_Z_removed_fraction=removed, passivity_projected=rep, bands=summ,
               table='diagonal loading (per-mode mass and damping from Z_ii at the mode frequency, vacuum participations)',
               f_loaded_over_vacuum={str(i): float(oD[i]/om[i]) for i in range(0, n, 8)}, sigma_loaded_over_vacuum={str(i): float(sD[i]/sg[i]) for i in range(0, n, 8)},
               median_f_ratio_first20=float(np.median(oD[:20]/om[:20])), median_sigma_ratio_first20=float(np.median(sD[:20]/sg[:20])),
               median_f_ratio_20_144=float(np.median(oD[20:]/om[20:])), median_sigma_ratio_20_144=float(np.median(sD[20:]/sg[20:])),
               eigen_nonproportional_median=float(np.median(nonprop)), eigen_nonproportional_max=float(nonprop.max()))
    oL, sL, phiL = oD, sD, Phi
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

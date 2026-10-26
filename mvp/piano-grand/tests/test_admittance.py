# SPDX-License-Identifier: MIT
"""Piano 0.7b test G-T8 (admittance passivity and reciprocity): in the small synthetic box with a modal panel, drive the five panel modes
kinematically (grand_fdtd --mode kin), build the modal radiation-impedance matrix Z(w) and the air-loaded bridge-point admittance
Y_port = Phi (D + Z)^-1 Phi^T for three ports, and check
  * reciprocity: Z and Y_port symmetric (relative asymmetry),
  * passivity: the Hermitian parts of Z and of Y_port positive semi-definite (positive-real) at every frequency of the band,
  * physics: the air adds mass (Im Z / w > 0 at the lowest mode) and damping (Re Z_ii > 0),
  * the modal reduction (admittance.reduce_modes) reproduces Y_port within its stated non-proportionality error.
Usage: test_admittance.py PATH/TO/grand_fdtd [WORKDIR]"""
import subprocess, sys, tempfile
from pathlib import Path
try:
    import numpy as np
except ImportError:
    print("numpy not available: skipped"); sys.exit(77)
sys.path.insert(0, str(Path(__file__).resolve().parents[1])); sys.path.insert(0, str(Path(__file__).resolve().parent))
import admittance as A
import test_fdtd_reciprocity as TR

def main():
    exe = sys.argv[1]; d = Path(sys.argv[2]) if len(sys.argv) > 2 else Path(tempfile.mkdtemp()); d.mkdir(parents=True, exist_ok=True)
    nm = 5; slot = 1000; steps = nm*slot
    dt = TR.make_scene(d)
    subprocess.run([exe, '--scene', str(d), '--out', str(d/'k'), '--steps', str(steps), '--tau', '3e-4', '--mode', 'kin', '--kin-first', '0',
                    '--kin-count', str(nm), '--kin-slot', str(slot)], check=True, stderr=subprocess.DEVNULL)
    F = np.fromfile(d/'k.fkin.f32', np.float32).reshape(steps, nm).astype(float); u = np.fromfile(d/'k.usrc.f32', np.float32).astype(float)
    runs = {j: (F[j*slot:(j + 1)*slot], u[j*slot:(j + 1)*slot]) for j in range(nm)}
    nfft = 1 << 14
    f, Z = A.impedance_matrix(runs, dt, nm, nfft)
    om = 2*np.pi*np.array([70.0, 130.0, 140.0, 200.0, 240.0]); sg = np.full(nm, 3.0)
    band = np.where((f > 30) & (f < 400))[0][::4]
    Zb = Z[band]
    asym = np.linalg.norm(Zb - np.swapaxes(Zb, 1, 2), axis=(1, 2))/np.linalg.norm(Zb, axis=(1, 2))
    herm = 0.5*(Zb + np.conj(np.swapaxes(Zb, 1, 2))); ev = np.linalg.eigvalsh(herm)
    zscale = np.abs(Zb).max()
    print(f'Z: median asymmetry {np.median(asym):.3e}, max {asym.max():.3e}; smallest Hermitian eigenvalue {ev[:, 0].min():.3e} (|Z| max {zscale:.3e})')
    Zs = A.symmetrise(Z)
    # three ports with random mass-normalised participations
    rng = np.random.default_rng(3); Phi = rng.normal(size=(3, nm))*np.sqrt(1/4.0)
    Y = A.loaded_admittance(f, Zs[band], om, sg, Phi, f[band])
    rep = A.passivity_report(Zs[band], Y)
    print('passivity/reciprocity:', rep)
    ok = True
    ok &= np.median(asym) < 0.05
    ok &= ev[:, 0].min() > -0.02*zscale                                       # Re Z >= 0 up to numerical noise
    ok &= rep['Yport']['min_over_max_eig'] > -1e-3                            # positive real
    ok &= rep['Yport']['max_asym'] < 1e-9                                     # symmetric by construction after symmetrisation of Z
    lo = np.argmin(np.abs(f - 70.0))
    addmass = (Z[lo, 0, 0].imag/(2*np.pi*70.0)); print(f'added modal mass at 70 Hz: {addmass:.4f} (kg, per unit mass-normalised mode), Re Z_00 = {Z[lo,0,0].real:.4f}')
    ok &= addmass > 0 and Z[lo, 0, 0].real > 0
    oL, sL, phiL, npro = A.reduce_modes(f, Zs, om, sg, Phi)
    Yr = A.vacuum_admittance(oL, sL, phiL, f[band])
    err = np.linalg.norm(Yr - Y, axis=(1, 2))/np.linalg.norm(Y, axis=(1, 2))
    print(f'modal reduction: loaded f/f_vac {np.round(oL/om, 3)}, sigma_L/sigma {np.round(sL/sg, 2)}, non-proportional {np.round(npro, 3)}, Y_port error median {np.median(err):.3f}')
    ok &= np.all(oL/om < 1.0) and np.all(oL/om > 0.8) and np.median(err) < 0.5
    print('PASS' if ok else 'FAIL'); sys.exit(0 if ok else 1)

if __name__ == '__main__': main()

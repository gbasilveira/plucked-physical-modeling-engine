# SPDX-License-Identifier: MIT
"""Piano 0.7a test G-T3 (reciprocity): in a small synthetic box with a modal panel, the pressure at a microphone cell
caused by a force on board mode j (forward run) equals minus the velocity of mode j caused by a volume source at
that cell (reciprocal run), per unit input. Usage: test_fdtd_reciprocity.py PATH/TO/grand_fdtd [WORKDIR]"""
import subprocess, sys, tempfile, math
from pathlib import Path
try:
    import numpy as np
except ImportError:
    print("numpy not available: skipped"); sys.exit(77)

def make_scene(d, nm=5, sigma_val=3.0, sponge=0.4):
    nx, ny, nz, kb = 36, 36, 28, 10
    dx = 0.06; dt = 1.0e-4                  # c dt / dx = 0.572 < 1/sqrt 3
    L = 0.6; mu = 4.0
    cells = [(i, j) for i in range(8, 28) for j in range(8, 28)]       # 20 x 20 cells = 1.2 m
    L = 20*dx
    solid = np.zeros((nz, ny, nx), np.uint8)
    # rigid ring around the panel from kb-1 to kb+8 (closed on the sides, open top and bottom)
    for k in range(kb - 1, kb + 9):
        for i in range(7, 29):
            for j in range(7, 29):
                if not (8 <= i < 28 and 8 <= j < 28): solid[k, j, i] = 1
    modes = [(1, 1), (1, 2), (2, 1), (2, 2), (1, 3)][:nm]
    freqs = [70.0, 130.0, 140.0, 200.0, 240.0][:nm]
    phi = np.zeros((len(cells), nm), np.float32)
    for c, (i, j) in enumerate(cells):
        x = (i - 8 + 0.5)*dx; y = (j - 8 + 0.5)*dx
        for m, (a, b) in enumerate(modes):
            phi[c, m] = math.sqrt(4/(mu*L*L))*math.sin(a*math.pi*x/L)*math.sin(b*math.pi*y/L)
    omega = 2*np.pi*np.array(freqs); sigma = np.full(nm, sigma_val)
    (d/'scene.txt').write_text(f"nx={nx}\nny={ny}\nnz={nz}\nkb={kb}\ndx={dx}\ndt={dt!r}\nncols={len(cells)}\nnmodes={nm}\nsponge={sponge}\n")
    (d/'solid.u8').write_bytes(solid.tobytes()); (d/'columns.i32').write_bytes(np.array(cells, np.int32).tobytes())
    (d/'phi.f32').write_bytes(phi.tobytes()); (d/'modes.f64').write_bytes(np.concatenate([omega, sigma]).tobytes())
    return dt

def run(exe, d, name, args, steps):
    subprocess.run([exe, '--scene', str(d), '--out', str(d/name), '--steps', str(steps), '--tau', '3e-4'] + args, check=True, stderr=subprocess.DEVNULL)

def main():
    exe = sys.argv[1]
    d = Path(sys.argv[2]) if len(sys.argv) > 2 else Path(tempfile.mkdtemp())
    d.mkdir(parents=True, exist_ok=True)
    dt = make_scene(d); steps = 6000
    cell = (14, 18, 17)
    nm = 5
    run(exe, d, 'r', ['--mode', 'recip', '--src', ','.join(map(str, cell))], steps)
    V = np.fromfile(d/'r.vmodes.f32', np.float32).reshape(steps, nm).astype(float)
    q = np.fromfile(d/'r.qsrc.f32', np.float32).astype(float)
    n = 1 << 15; f = np.fft.rfftfreq(n, dt)
    Q = np.fft.rfft(q, n); worst = 0
    for j in range(nm):
        run(exe, d, f'f{j}', ['--mode', 'fwd', '--mode-index', str(j), '--rec', ','.join(map(str, cell))], steps)
        p = np.fromfile(d/f'f{j}.prec.f32', np.float32).astype(float)
        F = np.fft.rfft(np.fromfile(d/f'f{j}.qsrc.f32', np.float32).astype(float), n)
        Hf = np.fft.rfft(p, n)*np.exp(-2j*np.pi*f*dt)/F          # p after step n is at (n+1) dt, F at n dt
        Hr = np.fft.rfft(V[:, j], n)/Q                           # both on the half-step lattice
        band = (f > 20) & (f < 400)
        w = np.abs(Hr[band]) > 0.05*np.abs(Hr[band]).max()
        err = np.abs(Hf[band][w] + Hr[band][w])/np.abs(Hr[band][w])
        print(f'mode {j}: max rel. mismatch |Hf + Hr|/|Hr| = {err.max():.4f}, median {np.median(err):.4f} (|Hr| max {np.abs(Hr[band]).max():.3e})')
        worst = max(worst, np.median(err))
    ok = worst < 0.02
    print('PASS' if ok else 'FAIL', 'worst median mismatch', worst)
    sys.exit(0 if ok else 1)

if __name__ == '__main__': main()

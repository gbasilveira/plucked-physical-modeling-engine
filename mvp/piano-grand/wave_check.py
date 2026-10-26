# SPDX-License-Identifier: MIT
"""Piano 0.7b: free-field accuracy of the FDTD grids (empty domain, rigid floor): pressure at receivers from a unit volume-velocity source against the
analytic  p/Q = j w rho exp(-j k r) / (4 pi r)  plus its floor image. Reports the magnitude error in third-octave bands for the coarse (25 mm) and fine
(12.5 mm) grids. Usage: wave_check.py WORK [coarse|fine] [steps]"""
import sys, subprocess, json, math
from pathlib import Path
import numpy as np
import scene_b as S

def main(work, grid='fine', steps=None):
    g = S.GRIDS[grid]; work = Path(work); sd = work/f'scene_wave_{grid}'; sd.mkdir(exist_ok=True)
    (sd/'scene.txt').write_text(f"nx={g.NX}\nny={g.NY}\nnz={g.NZ}\nkb={g.KB}\ndx={g.dx}\ndt={g.dt!r}\nncols=0\nnmodes=0\nsponge=0.5\n")
    (sd/'solid.u8').write_bytes(np.zeros(g.NX*g.NY*g.NZ, np.uint8).tobytes())
    steps = int(steps) if steps else int(0.045/g.dt)                              # 45 ms: long enough for the 2 m paths, before sponge reflections return
    src = np.array([0.4, 0.5, 1.0]); recs = [src + np.array([d, 0, dz]) for d, dz in ((0.5, 0.0), (1.0, 0.0), (0.7, 0.4))]
    cell = lambda p: (int((p[0]-g.X0)//g.dx), int((p[1]-g.Y0)//g.dx), int(p[2]//g.dx))
    base = work/f'wave_{grid}'
    if not Path(str(base) + '.meta.txt').exists():
        subprocess.run([str(work/'grand_fdtd_b'), '--scene', str(sd), '--mode', 'wave', '--out', str(base), '--steps', str(steps), '--src', ','.join(map(str, cell(src))),
                        '--rec', ';'.join(','.join(map(str, cell(r))) for r in recs), '--tau', '7e-5'], check=True, stderr=subprocess.DEVNULL)
    p = np.fromfile(str(base) + '.prec.f32', np.float32).reshape(steps, len(recs)).astype(float); q = np.fromfile(str(base) + '.qsrc.f32', np.float32).astype(float)
    n = 1 << 14; f = np.fft.rfftfreq(n, g.dt); w = 2*np.pi*f; Q = np.fft.rfft(q, n)
    res = {}
    # cell centres (the grid snaps source and receivers to cell centres)
    ctr = lambda p_: np.array([g.X0 + (cell(p_)[0] + 0.5)*g.dx, g.Y0 + (cell(p_)[1] + 0.5)*g.dx, (cell(p_)[2] + 0.5)*g.dx])
    s_c = ctr(src); out = []
    for i, r_ in enumerate(recs):
        rc = ctr(r_); Pn = np.fft.rfft(p[:, i], n)/Q*np.exp(-1j*w*g.dt/2)       # p after step n is at (n+1) dt, the source is on the half-step lattice
        def G(pos):
            d = np.linalg.norm(rc - pos); return 1j*w*1.2*np.exp(-1j*w*d/343.0)/(4*np.pi*d)
        img = s_c*np.array([1, 1, -1])
        Pa = G(s_c) + G(img)
        rows = []
        for lo in (250, 500, 800, 1000, 1250, 1600, 2000, 2500, 3150, 4000):
            hi = lo*2**(1/3); s = (f >= lo) & (f < hi)
            rows.append((lo, float(20*np.log10(np.abs(Pn[s]).mean()/np.abs(Pa[s]).mean())), float(np.abs(Pn[s] - Pa[s]).mean()/np.abs(Pa[s]).mean())))
        out.append(rows)
    res = dict(grid=grid, dx=g.dx, bands_lo_Hz=[r[0] for r in out[0]], magnitude_error_dB=[[r[1] for r in rows] for rows in out], complex_relative_error=[[r[2] for r in rows] for rows in out])
    (work/f'wave_check_{grid}.json').write_text(json.dumps(res, indent=1)); print(json.dumps(res, indent=1))

if __name__ == '__main__':
    main(*sys.argv[1:4])

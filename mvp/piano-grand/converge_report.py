# SPDX-License-Identifier: MIT
"""Analyse convergence.json (board_study.py convergence): mode-by-mode frequency error against the finest mesh,
error vs frequency, and the highest frequency below which every mode is within 1 % / 2 %."""
import json, sys
import numpy as np
from pathlib import Path

def main(path, out=None):
    d = json.loads(Path(path).read_text())
    hs = sorted(d, key=float, reverse=True)
    fine = hs[-1]; ref = np.array(d[fine]['f'])
    lines = [f'reference mesh: h = {fine} m ({d[fine]["info"]["dofs"]} dofs, {len(ref)} modes, f1 = {ref[0]:.2f} Hz)']
    lines.append('h [m]   dofs    solve[s]  f1[Hz]   N(<1kHz) N(<2kHz) N(<3kHz) | modes<500Hz: max|err| | <1kHz | <2kHz | <3kHz  [% vs finest, mode-by-mode]')
    res = {}
    for h in hs:
        f = np.array(d[h]['f']); n = min(len(f), len(ref))
        err = np.abs(f[:n]/ref[:n] - 1)*100
        def mx(fmax):
            sel = ref[:n] < fmax
            return err[sel].max() if sel.any() else float('nan')
        # highest f such that all modes below are within tol
        def limit(tol):
            bad = np.where(err > tol)[0]
            return ref[bad[0]] if len(bad) else ref[n-1]
        res[h] = dict(e500=mx(500), e1k=mx(1000), e2k=mx(2000), e3k=mx(3000), l1=limit(1.0), l2=limit(2.0), l5=limit(5.0))
        lines.append(f'{float(h):<7} {d[h]["info"]["dofs"]:<7} {d[h]["solve_s"]:<9.0f} {f[0]:<8.2f} {(f<1000).sum():<8} {(f<2000).sum():<8} {(f<3000).sum():<8} | {res[h]["e500"]:6.2f} | {res[h]["e1k"]:6.2f} | {res[h]["e2k"]:6.2f} | {res[h]["e3k"]:6.2f}   within 1%: to {res[h]["l1"]:.0f} Hz, 2%: to {res[h]["l2"]:.0f} Hz, 5%: to {res[h]["l5"]:.0f} Hz')
    # observed order from three finest (modes < 1 kHz)
    if len(hs) >= 3:
        h1, h2, h3 = (float(x) for x in hs[-3:])
        f1, f2, f3 = (np.array(d[x]['f']) for x in hs[-3:])
        n = min(len(f1), len(f2), len(f3)); sel = f3[:n] < 1000
        a = np.abs(f1[:n]-f2[:n])[sel]; b = np.abs(f2[:n]-f3[:n])[sel]
        ok = (b > 0)
        r = np.median(a[ok]/b[ok]); p = np.log(r)/np.log(h1/h2) if r > 1 else float('nan')
        lines.append(f'median observed order of convergence (3 finest meshes, modes < 1 kHz): ratio {r:.2f} -> p = {p:.2f}')
        # Richardson with p=2 from the two finest meshes -> estimated infinite-mesh error of the finest
        fr = f3[:n] + (f3[:n]-f2[:n])/((h2/h3)**2 - 1)
        e = np.abs(f3[:n]/fr - 1)*100
        for fm in (500, 1000, 2000, 3000):
            s = fr < fm
            if s.any(): lines.append(f'  finest mesh vs Richardson (p=2) extrapolation: modes < {fm} Hz: max {e[s].max():.2f} %, median {np.median(e[s]):.2f} %')
    lines.append('')
    lines.append('error percentiles per band, each mesh against the finest (mode-by-mode, % of frequency):')
    for h in hs[:-1]:
        f = np.array(d[h]['f']); n = min(len(f), len(ref)); err = np.abs(f[:n]/ref[:n] - 1)*100
        row = []
        for lo, hi in ((0, 250), (250, 500), (500, 1000), (1000, 2000), (2000, 3000)):
            s_ = (ref[:n] >= lo) & (ref[:n] < hi)
            row.append(f'{lo}-{hi} Hz: med {np.median(err[s_]):.2f} p95 {np.percentile(err[s_], 95):.2f} max {err[s_].max():.2f}')
        lines.append(f'h = {h}: ' + ' | '.join(row))
    text = '\n'.join(lines)
    print(text)
    if out: Path(out).write_text(text + '\n')
if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else None)

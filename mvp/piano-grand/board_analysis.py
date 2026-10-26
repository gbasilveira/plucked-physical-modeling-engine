# SPDX-License-Identifier: MIT
"""Figures and numbers for the grand soundboard: geometry, low mode shapes, driving-point mobility at the
bridge against the infinite-plate impedance. Usage: board_analysis.py modes.npz outdir"""
import sys, json, math
from pathlib import Path
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import matplotlib.tri as mtri
import geometry as geo, fem

def mobility(f_modes, phi_k, freqs, eta=0.02):
    w = 2*np.pi*freqs[:, None]; wi = 2*np.pi*f_modes[None, :]; sg = 0.5*eta*wi
    return (1j*w/(wi**2 - w**2 + 2j*sg*w) * phi_k[None, :]**2).sum(1)

def infinite_plate_mobility(h=0.0082):
    # Cremer: Z = 8 sqrt(mu sqrt(Dx Dy)) (orthotropic, Kirchhoff); bare plate, mean thickness
    Q = fem.plane_stress_Q()
    Dx = Q[1, 1]*h**3/12; Dy = Q[0, 0]*h**3/12     # along grain (y), across (x)
    mu = fem.RHO*h
    return 1.0/(8*math.sqrt(mu*math.sqrt(Dx*Dy)))

def main(npz, outdir):
    out = Path(outdir); out.mkdir(parents=True, exist_ok=True)
    d = np.load(npz); f = d['f']; nodes = d['nodes']; tri = d['tri']; W = d['W'].astype(float); phi = d['phi']; key = d['key']
    lines = []
    # --- geometry
    fig, ax = plt.subplots(figsize=(6, 7.5))
    ol = geo.outline(0.01); ax.plot(*np.vstack([ol, ol[:1]]).T, 'k-', lw=1.5)
    for pth in geo.rib_paths(): ax.plot(*pth.T, color='tab:green', lw=1)
    ax.plot(*geo.TREBLE_BRIDGE.T, color='tab:red', lw=3); ax.plot(*geo.BASS_BRIDGE.T, color='tab:blue', lw=3)
    ax.plot(d['kx'], d['ky'], 'k.', ms=3)
    ax.set_aspect('equal'); ax.set_xlabel('x, bass to treble [m]'); ax.set_ylabel('y, front to tail [m]')
    ax.set_title(f'Board outline (area {geo.area():.2f} m2), 12 ribs, treble/bass bridges, 88 string points')
    fig.savefig(out/'geometry.png', dpi=110, bbox_inches='tight'); plt.close(fig)
    # --- mode shapes
    fig, axs = plt.subplots(2, 4, figsize=(12, 8))
    T = mtri.Triangulation(nodes[:, 0], nodes[:, 1], tri)
    for i, ax in enumerate(axs.ravel()):
        z = W[:, i]; z = z/np.abs(z).max()
        ax.tricontourf(T, z, levels=np.linspace(-1, 1, 21), cmap='RdBu'); ax.set_aspect('equal'); ax.set_title(f'mode {i+1}: {f[i]:.1f} Hz'); ax.axis('off')
    fig.savefig(out/'modes_low.png', dpi=100, bbox_inches='tight'); plt.close(fig)
    # --- mobility
    freqs = np.arange(20, 3500, 1.0)
    yinf = infinite_plate_mobility()
    lines.append(f'infinite-plate mobility (bare orthotropic plate, 8.2 mm): {yinf*1e3:.2f} mm/s/N  (Z = {1/yinf:.0f} N s/m)')
    fig, ax = plt.subplots(figsize=(9, 5))
    bands = [(50, 100), (100, 200), (200, 400), (400, 800), (800, 1600), (1600, 3200)]
    lines.append('mobility at the bridge, band-averaged Re(Y) (mm/s/N) [the real part is what a string sees as loss]; mean over keys listed')
    lines.append('key    ' + ' '.join(f'{lo:>4}-{hi:<5}' for lo, hi in bands))
    for kname, kk in (('A0', 21), ('C2', 36), ('C3', 48), ('C4', 60), ('C5', 72), ('C6', 84), ('C7', 96), ('C8', 108)):
        i = int(np.where(key == kk)[0][0]); Y = mobility(f, phi[i], freqs)
        row = []
        for lo, hi in bands:
            s = (freqs >= lo) & (freqs < hi); row.append(np.mean(Y[s].real))
        lines.append(f'{kname:<4} {kk:>3} ' + ' '.join(f'{v*1e3:9.2f}' for v in row))
        if kk in (21, 60, 84, 108): ax.semilogx(freqs, 20*np.log10(np.abs(Y)), lw=0.7, label=f'{kname} (key {kk})')
    ax.axhline(20*np.log10(yinf), color='k', ls='--', label='infinite plate')
    ax.set_xlabel('Hz'); ax.set_ylabel('|Y| dB re 1 m/s/N'); ax.legend(); ax.grid(True, which='both', alpha=.3)
    ax.set_title('Driving-point mobility at four bridge points (in vacuo, loss factor 0.02)')
    fig.savefig(out/'mobility.png', dpi=110, bbox_inches='tight'); plt.close(fig)
    # mean real part (all 88 keys) per band vs infinite plate
    Ys = np.array([mobility(f, phi[i], freqs).real for i in range(88)])
    lines.append('all 88 keys, mean Re(Y) / infinite-plate Y per band: ' + ' '.join(f'{lo}-{hi}: {np.mean(Ys[:, (freqs>=lo)&(freqs<hi)])/yinf:.2f}' for lo, hi in bands))
    lines.append(f'modes: {len(f)} up to {f[-1]:.0f} Hz; first ten (Hz): ' + ', '.join(f'{x:.1f}' for x in f[:10]))
    Nt = [(lim, int((f < lim).sum())) for lim in (500, 1000, 2000, 3000)]
    lines.append('mode counts: ' + ', '.join(f'N(<{l} Hz) = {n}' for l, n in Nt))
    lines.append(f'modal density 1-3 kHz: {(((f>1000)&(f<3000)).sum())/2000:.3f} modes/Hz; Weyl estimate for a thin plate of this area and mean thickness: ' )
    text = '\n'.join(lines); print(text); (out/'board_analysis.txt').write_text(text + '\n')

if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])

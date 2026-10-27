# SPDX-License-Identifier: MIT
"""Piano 0.7c: prune the direct string radiation table. A partial is kept when its direct pressure is not more than THRESH_DB (40) below the
pressure the same partial produces through the board, at any of the six microphones:
  direct  = |G_n| V                         (V: amplitude of the analytic modal velocity)
  board   = |E^(k, m, w_n)| |a_n| V / w_n |lp(w_n)|   (E^: DFT of the 0.7b response of the string's port; a_n: bridge-force coefficient, so the force
            amplitude is |a_n| V / w_n; lp: the bridge-mass low pass of the engine) smoothed in third-octave bands.
The horizontal polarisation is judged against the vertical partial of the same number (it does not drive the board in the engine).
Usage: prune_direct.py WORK OUT.npz [THRESH_DB]"""
import struct, sys
from pathlib import Path
import numpy as np
sys.path.insert(0, str(Path(__file__).resolve().parent))
import build_live_table as B, string_radiation as SR

def read_terms(path):
    b = open(path, 'rb').read(); p = 0; out = []
    for k in range(88):
        cnt = struct.unpack_from('<i', b, p)[0]; p += 4; ss = []
        for j in range(cnt):
            n = struct.unpack_from('<i', b, p)[0]; p += 4; ss.append(np.frombuffer(b, '<f4', n, p).astype(float)); p += 4*n
        out.append(ss)
    return out

def third_octave_smooth(f, mag2, fc, width=2**(1/6)):
    out = np.zeros(len(fc))
    for i, c in enumerate(fc):
        k = (f >= c/width) & (f <= c*width)
        out[i] = np.sqrt(mag2[k].mean()) if k.any() else np.nan
    return out

def main(work, out, thresh_db=40.0):
    work = Path(work); strings = SR.read_strings(work/'strings.bin'); terms = read_terms(work/'terms.bin'); G = B.read_strrad(work/'strrad_plate.bin')
    E = np.load(work/'irc_open.npz')['E']                                   # (6, 88, N) the open lid is the loudest and the reference
    N = E.shape[2]; f = np.fft.rfftfreq(N, 1/48000.0); fc_bridge = 0.0
    corner = 1.0/(2*np.pi*0.055*1.47e-3)                                  # kBridgeMassCornerHz (bridge_mass.hpp), about 1969 Hz
    thr = 10**(-thresh_db/20); keep = {}; tot = kept = 0; per_oct = {}
    for k in range(88):
        Ek = np.fft.rfft(E[:, k].astype(np.float64), axis=1); M2 = np.abs(Ek)**2        # (6, bins)
        for j, (gV, gH) in enumerate(G[k]):
            om = strings[k + 21][j]['omega'][:len(gV)]; a = np.abs(terms[k][j][:len(gV)]); fn = om/(2*np.pi)
            bm = np.zeros((6, len(fn)))
            for m in range(6): bm[m] = third_octave_smooth(f, M2[m], fn)
            lp = 1/np.sqrt(1 + (fn/corner)**2)
            board = bm*(a*lp/om)[None, :]                                          # (6, n) pressure per unit V
            for name, g in (('V', gV), ('H', gH)):
                gm = np.hypot(g[:, :, 0], g[:, :, 1]).T                             # (6, n)
                ratio = gm/np.maximum(board, 1e-30)
                kp = np.nanmax(ratio, axis=0) >= thr
                keep[f'{name}_{k}_{j}'] = kp; tot += len(kp); kept += int(kp.sum())
                for oc in range(0, 16):
                    sel = (fn >= 31.25*2**oc) & (fn < 31.25*2**(oc + 1)); per_oct.setdefault(oc, [0, 0]); per_oct[oc][0] += int(sel.sum()); per_oct[oc][1] += int(kp[sel].sum())
    np.savez(out, **keep)
    print(f'kept {kept} of {tot} partials ({100*kept/tot:.1f} %) at {thresh_db} dB; per octave from 31 Hz: ' + ', '.join(f'{31.25*2**o:.0f}Hz {v[1]}/{v[0]}' for o, v in per_oct.items() if v[0]))

if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2], float(sys.argv[3]) if len(sys.argv) > 3 else 40.0)

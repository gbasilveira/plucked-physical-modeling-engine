# SPDX-License-Identifier: MIT
"""Piano 0.7c: assemble the real-time grand table ("PGL2", read by mvp/piano-v4/grand_live.cpp) from the fits of fit_live.py.
Usage: build_live_table.py WORK OUT.bin ORDER [--prune KEEP.npz] [--gain G]
Sections (little endian, 16-byte aligned): header (184 bytes), low-range poles (omega, sigma per lid), low-range residues (re, im planes,
[set][ch][port][P]), high-range modes (omega, sigma), participations (chunk-major float), complex output weights and DC-zero feedthrough
([set][ch][Nh]) with the integer delays, the pruned direct string radiation, and the strings' board (the 0.7b air-loaded 144-mode table)."""
import struct, sys, json
from pathlib import Path
import numpy as np
sys.path.insert(0, str(Path(__file__).resolve().parent))
import fit_live as FL

LIDS = ('open', 'half', 'closed')
MICS = ('closeL', 'closeR', 'playerL', 'playerR', 'audL', 'audR')

def pad_to(n, m): return (n + m - 1)//m*m

def pad16(buf):
    while len(buf) % 16: buf += b'\0'
    return buf

def read_strrad(path):
    b = open(path, 'rb').read(); assert b[:4] == b'SRAD'
    nmic = struct.unpack('<i', b[4:8])[0]; o = 8; keys = []
    for k in range(88):
        cnt = struct.unpack('<i', b[o:o+4])[0]; o += 4
        nm = struct.unpack('<%di' % cnt, b[o:o+4*cnt]); o += 4*cnt
        strings = []
        for j in range(cnt):
            n = nm[j]; sz = n*nmic*2
            gV = np.frombuffer(b, '<f4', sz, o).reshape(n, nmic, 2); o += 4*sz
            gH = np.frombuffer(b, '<f4', sz, o).reshape(n, nmic, 2); o += 4*sz
            strings.append((gV, gH))
        keys.append(strings)
    assert o == len(b)
    return keys

def main(work, out, order, prune=None, gain=None):
    work = Path(work)
    fits = [np.load(work/f'fit/lf_{lid}_{order}.npz') for lid in LIDS]
    sides = [np.load(work/f'irc_{lid}_hf.npz') for lid in LIDS]
    hfd = np.load(work/'hfboard_0.7b16.npz', allow_pickle=True)
    # ---- low range
    counts = [pad_to(len(f['a']), 8) for f in fits]; P = max(counts)
    lfOm = np.zeros((3, P)); lfSg = np.zeros((3, P)); nlight = 0
    Rre = np.zeros((9, 2, 88, P), np.float32); Rim = np.zeros_like(Rre)
    for l, f in enumerate(fits):
        a = f['a']; R = f['R']; n = len(a)                 # R: (6 mics, 88 ports, n)
        lfOm[l, :n] = a.imag; lfSg[l, :n] = -a.real
        nlight = max(nlight, int(np.sum(a.imag/(2*np.pi) < 2600.0)))
        for preset in range(3):
            for ch in range(2):
                r = R[2*preset + ch]
                Rre[l*3 + preset, ch, :, :n] = r.real; Rim[l*3 + preset, ch, :, :n] = r.imag
    nlight = pad_to(nlight, 8)
    # ---- high range
    f_hf = sides[0]['f_hf']; Nh = pad_to(len(f_hf), 32)
    om_h = np.zeros(Nh); sg_h = np.zeros(Nh); om_h[:len(f_hf)] = 2*np.pi*f_hf; sg_h[:len(f_hf)] = 0.5*0.02*om_h[:len(f_hf)]
    phi = np.zeros((88, Nh), np.float32); phi[:, :len(f_hf)] = hfd['phi']
    phiT = phi                                                       # port-major [port][Nh]
    Wre = np.zeros((9, 2, Nh), np.float32); Wim = np.zeros_like(Wre); We = np.zeros_like(Wre); delay = np.zeros((9, 2), np.uint16)
    for l, side in enumerate(sides):
        lam, W, D, e = FL.hf_weights(side)
        for preset in range(3):
            for ch in range(2):
                m = 2*preset + ch; s = l*3 + preset
                Wre[s, ch, :len(f_hf)] = W[m].real; Wim[s, ch, :len(f_hf)] = W[m].imag; We[s, ch, :len(f_hf)] = e[m]; delay[s, ch] = D[m]
    # ---- direct radiation
    keys = read_strrad(work/'strrad_plate.bin')
    keep = np.load(prune, allow_pickle=True) if prune else None
    import scene_b as S, string_radiation as SR
    centre = np.array([0.6, 1.1, S.Z_STRINGS]); refs = [SR.ref_samples(S.MICS[m], centre) for m in MICS]
    direct = bytearray(); kept_total = 0; all_total = 0
    for k in range(88):
        direct += struct.pack('<I', len(keys[k]))
        for j, (gV, gH) in enumerate(keys[k]):
            iv = np.arange(len(gV)); ih = np.arange(len(gH)); all_total += 2*len(gV)
            if keep is not None:
                iv = np.where(keep[f'V_{k}_{j}'])[0]; ih = np.where(keep[f'H_{k}_{j}'])[0]
            kept_total += len(iv) + len(ih)
            direct += struct.pack('<II', len(iv), len(ih))
            direct += pad16(iv.astype('<u2').tobytes())[:((len(iv)*2 + 3)//4*4)]
            direct += pad16(ih.astype('<u2').tobytes())[:((len(ih)*2 + 3)//4*4)]
            direct += gV[iv].astype('<f4').tobytes() + gH[ih].astype('<f4').tobytes()
    # ---- strings' board
    board = np.fromfile(work/'tableb_open_table.bin', '<f8'); assert board.size == 1024 + 88*512
    if gain is None:
        gain = json.loads(Path(work.parent/'plucked-physical-modeling-engine/piano-mvp/out/piano-mvp-v4/grand/grand-validation-0.7b.json').read_text())['gains']['A4']
    outScale = gain/0.05
    # ---- layout
    secs = [lfOm.astype('<f8').tobytes(), lfSg.astype('<f8').tobytes(), Rre.astype('<f4').tobytes(), Rim.astype('<f4').tobytes(),
            om_h.astype('<f8').tobytes(), sg_h.astype('<f8').tobytes(), phiT.astype('<f4').tobytes(), Wre.astype('<f4').tobytes(),
            Wim.astype('<f4').tobytes(), We.astype('<f4').tobytes(), delay.astype('<u2').tobytes(), bytes(direct), board.astype('<f8').tobytes()]
    hdr = 184; offs = []; pos = hdr
    for s in secs:
        pos = (pos + 15)//16*16; offs.append(pos); pos += len(s)
    pos = (pos + 15)//16*16
    offs += [0]
    head = struct.pack('<4sIIIIIfI14Q6i2IQ', b'PGL2', 2, P, nlight, Nh, 9, float(outScale), counts[0], *offs, *refs, counts[1], counts[2], pos)
    assert len(head) == hdr
    blob = bytearray(pos); blob[:hdr] = head
    for o, s in zip(offs, secs): blob[o:o+len(s)] = s
    Path(out).write_bytes(bytes(blob))
    print(f'wrote {out}: {len(blob)/1e6:.2f} MB, P={P} (light {nlight}), Nh={Nh}, direct partials kept {kept_total}/{all_total}, per-lid poles {counts}, refs {refs}, outScale {outScale:.5f}')

if __name__ == '__main__':
    a = sys.argv[1:]
    prune = a[a.index('--prune') + 1] if '--prune' in a else None
    gain = float(a[a.index('--gain') + 1]) if '--gain' in a else None
    main(a[0], a[1], int(a[2]), prune, gain)

# SPDX-License-Identifier: MIT
"""Piano 0.7b diagnostic listening renders (out/piano-mvp-v4/grand/diag/).
Usage: make_diag.py WORK REPO_OUT STEP   STEP in {taps, stems, solo}
  taps : body-tap-0.7b.wav, body-tap-compare.wav   (a 1 ms raised-cosine force tap at a bridge point, no strings)
  stems: stems-chopin-op28-no4-0.7b.wav (+ order, residual report) for the pinned excerpt 40-66 s, close AB, open lid
  solo : string-solo-<key>-0.7b.wav, string-solo-<key>-pickup-0.7b.wav, string-solo-<key>-energy.png
Same gain convention as make_renders_b.py (0.7b gain from the validation, 0.7a gain from the 0.7a validation, 0.6 gain 1)."""
import json, subprocess, sys
from pathlib import Path
import numpy as np
from scipy.signal import fftconvolve
import render_grand as R, metrics as M, scene_b as S, make_renders_b as B

FS = 48000
TAP_N = 20.0                                   # peak force of the tap (N): the order of the bridge force of a mezzo-forte note (peak bridge drive 8-25 N in the validation notes)
EXE_TAP = B.REPO/'out/piano-mvp-v4/piano-grand/piano-body-tap-06'
MICS = list(S.MICS)

def tap(n):
    p = int(0.001*FS); g = np.zeros(n); g[:p] = TAP_N*0.5*(1 - np.cos(2*np.pi*(np.arange(p) + 0.5)/p)); return g

def gains(out):
    d = json.loads((Path(out)/'grand-validation-0.7b.json').read_text()); return d['gains']['A4'], d['gain_0p7a']

def body(E, port, mic_idx, n):
    g = tap(n); return np.stack([fftconvolve(g, E[m, port].astype(np.float64))[:n] for m in mic_idx], 1)

def wav_pair(out, name, x, scale=None, target=None):
    pk = np.abs(x).max(); sc = (1/max(1.0, pk*1.05)) if scale is None else scale
    if target is not None: sc = target/pk
    R.write_wav24(out/name, x*sc); return pk, sc

def taps(work, out):
    work = Path(work); out = Path(out); d = out/'diag'; d.mkdir(exist_ok=True)
    gb, ga = gains(out); n = int(2.0*FS); gap = np.zeros((int(0.5*FS), 2))
    Eb = {l: np.load(work/f'irb_full_{l}.npz')['E'] for l in ('open', 'half', 'closed')}
    Ea = np.load(work/'ir_open.npz')['E']
    pts = (('A1', 33), ('C4', 60), ('C6', 84)); segs = []; labels = []
    for nm, key in pts:
        for pr, mm in B.PRESET_MICS.items():
            mi = [MICS.index(m) for m in mm]; segs += [body(Eb['open'], key - 21, mi, n)*gb, gap]; labels.append(f'{nm} (key {key}) {pr} open')
    for lid in ('half', 'closed'):
        mi = [MICS.index(m) for m in B.PRESET_MICS['close']]; segs += [body(Eb[lid], 60 - 21, mi, n)*gb, gap]; labels.append(f'C4 (key 60) close {lid}')
    x = np.concatenate(segs); pk, sc = wav_pair(d, 'body-tap-0.7b.wav', x, target=0.5)
    (d/'body-tap-0.7b-order.txt').write_text('\n'.join(f'{i+1}. {l}' for i, l in enumerate(labels)) + f'\n(each {n/FS:.0f} s + 0.5 s gap; tap: 1 ms raised cosine, {TAP_N:.0f} N peak at the bridge point, no strings; the lid-state impulse responses are 1 s long, faded over the last 0.15 s, so the body rings for 1 s at most here)\nraw peak {pk:.4f}; 24-bit file scaled to a 0.5 peak (x{sc:.2f}; the taps are quiet at the common gain)\n')
    # comparison 0.6 -> 0.7a -> 0.7b, C4, close AB
    subprocess.run([str(EXE_TAP), '60', str(TAP_N), str(n), str(work/'tap06_c4.f32')], check=True)
    x06 = np.fromfile(work/'tap06_c4.f32', np.float32).reshape(-1, 2).astype(np.float64)
    mi = [0, 1]
    x7a = body(Ea, 60 - 21, mi, n)*ga; x7b = body(Eb['open'], 60 - 21, mi, n)*gb
    xs = np.concatenate([x06, gap, x7a, gap, x7b, gap]); pk2, sc2 = wav_pair(d, 'body-tap-compare.wav', xs, target=0.5)
    rms = [float(np.sqrt(np.mean(v[:FS]**2))) for v in (x06, x7a, x7b)]
    (d/'body-tap-compare.txt').write_text(f'0.6 board, 0.7a board, 0.7b board (C4 bridge point, 20 N tap, close AB, open lid), 2 s + 0.5 s gap each.\nfirst-second rms: 0.6 {rms[0]:.3e}, 0.7a {rms[1]:.3e}, 0.7b {rms[2]:.3e}; raw peak {pk2:.4f}; 24-bit file scaled to a 0.5 peak (x{sc2:.2f}), one scale for the three versions\n(each version has its own gain from the C4 v60 level match of the validation, so the levels compare the sound after that normalisation)\n')
    print('taps done', pk, pk2)

def excerpt_events(end_s=64.0):
    ev = [l.split() for l in open(B.REPO/'mvp/piano-v4/listening/chopin-op28-no4.events')]
    end = int(end_s*FS); keep = [l for l in ev if int(l[0]) < end] + [[str(end), '176', '64', '0']]
    return keep

def render_split(work, solo=None):
    work = Path(work); keep = excerpt_events()
    evf = work/'diag_chopin.events'; evf.write_text(''.join(' '.join(l) + '\n' for l in keep))
    tag = 'diag_chopin' if solo is None else f'diag_chopin_solo{solo}'
    pre = work/tag
    if not Path(str(pre) + '.info.txt').exists():
        cmd = [str(B.EXE), str(evf), str(work/'tableb_open_table.bin'), str(pre), '--full', '--tail', '2.0', '--strrad', str(work/'strrad_plate.bin'), '--split']
        if solo: cmd += ['--solo', str(solo)]
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode: raise RuntimeError(r.stderr)
        print(r.stdout.strip(), flush=True)
    return pre

def board_conv(g, E, mi, n):
    """g: (frames, 88) float32, E: (mics, 88, lag). Returns (n, len(mi)) float64."""
    out = np.zeros((n, len(mi)))
    for k in range(88):
        col = g[:, k]
        if not col.any(): continue
        c = col.astype(np.float64)
        for j, m in enumerate(mi):
            y = fftconvolve(c, E[m, k].astype(np.float64)); out[:min(n, len(y)), j] += y[:n]
    return out

def delayed(sf, mi, n):
    out = np.zeros((n, len(mi)))
    for j, m in enumerate(mi):
        r = B.REFS[m]; seg = sf[:max(0, n - r), m].astype(np.float64); out[r:r + len(seg), j] = seg
    return out

def stems(work, out):
    work = Path(work); out = Path(out); d = out/'diag'; d.mkdir(exist_ok=True)
    gb, _ = gains(out); pre = render_split(work)
    g = np.fromfile(str(pre) + '.g.f32', np.float32).reshape(-1, 88); gu = np.fromfile(str(pre) + '.gu.f32', np.float32).reshape(-1, 88)
    n = g.shape[0] + 48000; mi = [MICS.index(m) for m in B.PRESET_MICS['close']]
    E = np.load(work/'irb_full_open.npz')['E']
    s = np.fromfile(str(pre) + '.s.f32', np.float32).reshape(-1, 6); sl = np.fromfile(str(pre) + '.sl.f32', np.float32).reshape(-1, 6)
    gs = g - gu
    body_s = board_conv(gs, E, mi, n); body_u = board_conv(gu, E, mi, n)
    dir_s = delayed(s, mi, n); dir_l = delayed(sl, mi, n)
    t0, t1 = 40*FS, 66*FS
    cut = lambda x: x[t0:t1]*gb
    st = {'a': cut(body_s + body_u + dir_s + dir_l), 'b': cut(body_s + dir_s), 'c': cut(body_u + dir_l), 'd': cut(dir_s + dir_l), 'e': cut(body_s + body_u)}
    res_bc = st['a'] - st['b'] - st['c']; res_de = st['a'] - st['d'] - st['e']
    rms = lambda x: float(np.sqrt(np.mean(x**2)))
    rep = dict(rms_full=rms(st['a']), residual_b_plus_c_rel_dB=20*np.log10(max(rms(res_bc), 1e-30)/rms(st['a'])), residual_d_plus_e_rel_dB=20*np.log10(max(rms(res_de), 1e-30)/rms(st['a'])),
               peaks={k: float(np.abs(v).max()) for k, v in st.items()}, rms={k: rms(v) for k, v in st.items()},
               rms_ratio_dB_re_full={k: 20*np.log10(max(rms(v), 1e-30)/rms(st['a'])) for k, v in st.items()})
    for k in st:                        # fade each stem like the other excerpts
        st[k][:480] *= np.linspace(0, 1, 480)[:, None]; st[k][-2400:] *= np.linspace(1, 0, 2400)[:, None]
    gap = np.zeros((FS, 2)); x = np.concatenate([np.concatenate([st[k], gap]) for k in 'abcde'])
    pk, sc = wav_pair(d, 'stems-chopin-op28-no4-0.7b.wav', x)
    rep.update(file_raw_peak=float(pk), file_scale=sc)
    (d/'stems-chopin-op28-no4-0.7b-order.txt').write_text(
        'Pinned excerpt (Chopin Op. 28 No. 4, source 40-66 s, pedalled), close AB, open lid; five stems of 26 s with 1 s gaps, one common scale:\n'
        '(a) full mix\n(b) struck strings only: their bridge force through the body plus their own direct radiation\n(c) unstruck (sympathetic, light-model) strings only: through the body plus their direct radiation (vertical modes below 2 kHz)\n'
        '(d) direct string radiation only (struck + unstruck)\n(e) the board without the direct string sound (struck + unstruck bridge force through the body)\n'
        f'Sums: (b)+(c) and (d)+(e) both equal (a): residuals {rep["residual_b_plus_c_rel_dB"]:.1f} dB and {rep["residual_d_plus_e_rel_dB"]:.1f} dB re (a) (float32 records).\n'
        'Voices count as struck from the note-on until they go to sleep, so strings of released notes under the leaky dampers are in (b); (c) is the voices that were never struck since waking.\n'
        'Stem (a) differs from the main render by the unstruck strings\' direct radiation (not in the main renders).\n'
        f'raw peak of (a) {rep["peaks"]["a"]:.3f}; file scaled by {sc:.3f}\n')
    (d/'stems-report.json').write_text(json.dumps(rep, indent=1)); print(json.dumps(rep, indent=1))

def pick_key(work):
    pre = Path(work)/'diag_chopin'
    g = np.fromfile(str(pre) + '.g.f32', np.float32).reshape(-1, 88); gu = np.fromfile(str(pre) + '.gu.f32', np.float32).reshape(-1, 88)
    t0, t1 = 40*FS, 64*FS; ev = [l for l in excerpt_events()]
    strikes = {}
    for l in ev:
        if l[1] == '144' and int(l[3]) > 0 and t0 <= int(l[0]) < t1: strikes.setdefault(int(l[2]), []).append(int(l[0])/FS)
    best = None
    for key, ts in strikes.items():
        p = key - 21; es = float(np.sum((g[t0:t1, p] - gu[t0:t1, p])**2)); eu = float(np.sum(gu[t0:t1, p]**2))
        if 2 <= len(ts) <= 5 and key <= 64 and es > 0 and eu > 0:
            sc = eu/es
            if best is None or sc > best[0]: best = (sc, key, ts, es, eu)
    return best

def solo(work, out):
    import matplotlib; matplotlib.use('Agg'); import matplotlib.pyplot as plt
    work = Path(work); out = Path(out); d = out/'diag'; d.mkdir(exist_ok=True); gb, _ = gains(out)
    render_split(work)
    sc_, key, ts, es, eu = pick_key(work)
    print('solo key', key, 'strikes', ts, 'unstruck/struck energy', sc_, flush=True)
    pre = render_split(work, solo=key); base = render_split(work)
    g = np.fromfile(str(base) + '.g.f32', np.float32).reshape(-1, 88); p = key - 21
    n = g.shape[0] + 48000; mi = [MICS.index(m) for m in B.PRESET_MICS['close']]
    E = np.load(work/'irb_full_open.npz')['E']
    gk = np.zeros_like(g[:, :1].repeat(88, axis=1)); gk[:, p] = g[:, p]
    sf = np.fromfile(str(pre) + '.solo.f32', np.float32).reshape(-1, 7)
    y = board_conv(gk, E, mi, n) + delayed(sf[:, :6], mi, n)
    t0, t1 = 40*FS, 66*FS; y = y[t0:t1]*gb
    y[:480] *= np.linspace(0, 1, 480)[:, None]; y[-2400:] *= np.linspace(1, 0, 2400)[:, None]
    pk, sc = wav_pair(d, f'string-solo-{key}-0.7b.wav', y)
    pick = sf[t0:t1, 6].astype(np.float64); pick = pick/max(np.abs(pick).max(), 1e-30)*0.5
    R.write_wav24(d/f'string-solo-{key}-pickup-0.7b.wav', np.stack([pick, pick], 1))
    # energy plot: mean square of the pickup velocity and of the bridge force, 20 ms blocks
    blk = int(0.02*FS); nb = len(pick)//blk; t = (np.arange(nb) + 0.5)*0.02 + 40
    ev = lambda x: 10*np.log10(np.maximum((x[:nb*blk].reshape(nb, blk)**2).mean(1), 1e-30))
    pv = sf[t0:t1, 6].astype(np.float64); fb = g[t0:t1, p].astype(np.float64)
    fig, ax = plt.subplots(2, 1, figsize=(10, 5), sharex=True)
    ax[0].plot(t, ev(pv), lw=0.8); ax[0].set_ylabel('pickup velocity\n(dB re 1 (m/s)^2)')
    ax[1].plot(t, ev(fb), lw=0.8, color='tab:green'); ax[1].set_ylabel('bridge force\n(dB re 1 N^2)'); ax[1].set_xlabel('source time (s)')
    events = [l for l in excerpt_events()]
    for l in events:
        tt = int(l[0])/FS
        if not (40 <= tt <= 66): continue
        if l[1] == '144' and int(l[2]) == key and int(l[3]) > 0:
            for a in ax: a.axvline(tt, color='r', lw=1.2)
        if l[1] == '176' and l[2] == '64':
            for a in ax: a.axvline(tt, color='k' if int(l[3]) >= 64 else 'gray', lw=0.5, ls=':' if int(l[3]) >= 64 else '--')
    ax[0].set_title(f'key {key} over the excerpt: red = strike, black dotted = pedal down, gray dashed = pedal up')
    fig.tight_layout(); fig.savefig(d/f'string-solo-{key}-energy.png', dpi=110)
    info = dict(key=key, strikes_s=ts, unstruck_over_struck_energy=sc_, raw_peak=float(pk), scale=sc)
    (d/f'string-solo-{key}-info.json').write_text(json.dumps(info, indent=1)); print(info)

if __name__ == '__main__':
    work, out, step = sys.argv[1:4]
    {'taps': taps, 'stems': stems, 'solo': solo}[step](work, out)

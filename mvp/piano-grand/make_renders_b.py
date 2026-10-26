# SPDX-License-Identifier: MIT
"""Piano 0.7b renders and validation (0.6 Full, 0.7a, 0.7b side by side, plus the ablation).
Usage: make_renders_b.py WORK REPO_OUT MODES.npz STEP    STEP in {validate, ab, mics, chopin, etude}
Signals for one note (close AB, open lid), A0..A4:
  A0 = 0.7a (board FEM to 3.6 kHz, air loading by FDTD, Rayleigh above 1.4-2 kHz, no strings, diagonal loaded poles for 3 modes)
  A1 = A0 + statistical HF board           (E: irb_v1)
  A2 = A1 + direct string radiation        (strings over a rigid board)
  A3 = A2 + iron plate, fine FDTD, diffraction  (E: irb_full; strings over the plate)
  A4 = A3 + full loaded admittance        (board table with all 144 low modes air-loaded) = 0.7b
"""
import json, subprocess, sys
from pathlib import Path
import numpy as np
import render_grand as R, metrics as M, make_renders as MR, scene_b as S, string_radiation as SR

REPO = Path(__file__).resolve().parents[2]
EXE = REPO/'out/piano-mvp-v4/piano-grand/piano-grand-render'
LAYERS = {'40': 4, '60': 8, '92': 12}
NOTES = {'A2': 45, 'C4': 60, 'C6': 84}
MICS = list(S.MICS)
PRESET_MICS = MR.PRESET_MICS

def refs():
    centre = np.array([0.6, 1.1, S.Z_STRINGS])
    return [SR.ref_samples(S.MICS[m], centre) for m in MICS]
REFS = refs()

def render(work, name, notes, tag, table, strrad, tail=4.0):
    """piano-grand-render with a board table and a string-radiation table; cached by prefix."""
    work = Path(work); ev = work/f'{name}.events'; ev.write_text(R.events_text(notes))
    pre = work/f'rb_{tag}_{name}'
    if not Path(str(pre) + '.info.txt').exists():
        cmd = [str(EXE), str(ev), str(table), str(pre), '--full', '--tail', str(tail)]
        if strrad: cmd += ['--strrad', str(strrad)]
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode: raise RuntimeError(r.stderr)
        print(tag, name, r.stdout.strip(), flush=True)
    return pre

def strings_signal(pre, mic_idx):
    s = np.fromfile(str(pre) + '.s.f32', np.float32).reshape(-1, 6).astype(np.float64)
    out = np.zeros((s.shape[0] + max(REFS), len(mic_idx)))
    for j, m in enumerate(mic_idx): out[REFS[m]:REFS[m] + s.shape[0], j] = s[:, m]
    return out

def board_signal(pre, E, mic_idx):
    return R.radiate(pre, E, mic_idx, 1.0).T

def add(a, b):
    n = max(len(a), len(b)); out = np.zeros((n, a.shape[1])); out[:len(a)] += a; out[:len(b)] += b[:, :a.shape[1]]; return out

class Ctx:
    def __init__(self, work, modes, repo_out):
        self.work = Path(work); self.modes = modes; self.out = Path(repo_out)
        w = self.work
        self.E_a = np.load(w/'ir_open.npz')['E']
        self.E_v1 = np.load(w/'irb_v1_open.npz') if (w/'irb_v1_open.npz').exists() else None
        self.E_full = np.load(w/'irb_full_open.npz') if (w/'irb_full_open.npz').exists() else None
        self.table_a = w/'table_open.bin'; self.table_b = w/'tableb_open_table.bin'
        self.sr_np = w/'strrad_noplate.bin'; self.sr_p = w/'strrad_plate.bin'
    def idx(self, E, mics):
        names = [str(x) for x in E['mics']]; return [names.index(m) for m in mics]
    def signals(self, name, notes, mics=PRESET_MICS['close'], tail=4.0, which=('A0', 'A1', 'A2', 'A3', 'A4')):
        w = self.work; mi = [MICS.index(m) for m in mics]; sig = {}
        pa = render(w, name, notes, 'a_np', self.table_a, self.sr_np, tail)
        if 'A0' in which: sig['A0'] = board_signal(pa, self.E_a, mi)
        if 'A1' in which or 'A2' in which:
            b1 = board_signal(pa, self.E_v1['E'], self.idx(self.E_v1, mics))
            if 'A1' in which: sig['A1'] = b1
            if 'A2' in which: sig['A2'] = add(b1, strings_signal(pa, mi))
        if 'A3' in which:
            pp = render(w, name, notes, 'a_p', self.table_a, self.sr_p, tail)
            sig['A3'] = add(board_signal(pp, self.E_full['E'], self.idx(self.E_full, mics)), strings_signal(pp, mi))
        if 'A4' in which:
            pb = render(w, name, notes, 'b_p', self.table_b, self.sr_p, tail)
            sig['A4'] = add(board_signal(pb, self.E_full['E'], self.idx(self.E_full, mics)), strings_signal(pb, mi))
        return sig

def validate(ctx, which=('A0', 'A1', 'A2', 'A3', 'A4'), fname='grand-validation-0.7b.json'):
    work, out = ctx.work, ctx.out
    gain_a = MR.gain_of(out)                                   # the 0.7a convention
    c4 = ctx.signals('c4v60', [(0.0, 8.0, 60, 60)], which=which)
    ref06 = M.align(MR.render_06(work, 'c4v60', [(0.0, 8.0, 60, 60)]))
    w = slice(int(0.05*M.FS), int(1.0*M.FS))
    def rms(x): return float(np.sqrt(np.mean(M.align(x)[w]**2)))
    gains = {k: rms(ref06)/rms(v) for k, v in c4.items()}
    rows = {}
    ref_dir = REPO/'out/piano-reference-cache'
    for note, key in NOTES.items():
        for vel, layer in LAYERS.items():
            notes = [(0.0, 8.0, key, int(vel))]; tag = f'{note}_v{vel}'
            ref = M.load_ref(ref_dir/f'{note}v{layer}.flac')
            sig = ctx.signals(f'{note.lower()}v{vel}', notes, which=which)
            rec = {'reference': ref, '0.6': M.align(MR.render_06(work, f'{note.lower()}v{vel}', notes))}
            for k, v in sig.items(): rec[k] = M.align(v*gains[k])
            rows[tag] = {k: {m: (a.tolist() if hasattr(a, 'tolist') else a) for m, a in M.all_metrics(x).items()} for k, x in rec.items()}
            print('validated', tag, flush=True)
    (out/fname).write_text(json.dumps(dict(gains=gains, gain_0p7a=gain_a, rows=rows), indent=1))

def seg(x, dur=5.0, fade=0.3):
    y = x[:int(dur*M.FS)].copy(); n = int(fade*M.FS); y[-n:] *= np.linspace(1, 0, n)[:, None]; return y

def gain_b(ctx):
    d = json.loads((ctx.out/'grand-validation-0.7b.json').read_text()); return d['gains']['A4'], d['gain_0p7a']

def ab(ctx):
    gb, ga = gain_b(ctx); gap = np.zeros((int(0.6*M.FS), 2)); out = []
    items = [('C2', [(0, 4.0, 36, 70)]), ('C4', [(0, 4.0, 60, 70)]), ('C6', [(0, 4.0, 84, 70)]),
             ('chord', [(0, 4.0, 48, 70), (0.02, 4.0, 52, 66), (0.04, 4.0, 55, 68), (0.06, 4.0, 60, 72), (0.08, 4.0, 64, 70)])]
    for name, notes in items:
        a06 = MR.render_06(ctx.work, 'ab_' + name, notes)[:int(5*M.FS)]
        s = ctx.signals('ab_' + name, notes, which=('A0', 'A4'))
        out += [seg(a06), gap, seg(s['A0']*ga), gap, seg(s['A4']*gb), gap, gap]
    x = np.concatenate(out); pk = np.abs(x).max(); print('ab peak', pk)
    sc = 1/max(1, pk*1.05)
    R.write_wav24(ctx.out/'grand-ab-0.7b.wav', x*sc); R.write_wav_f32(ctx.out/'grand-ab-0.7b-raw.wav', x)
    (ctx.out/'grand-ab-0.7b-order.txt').write_text('Each of C2, C4, C6, chord (all velocity 70, close AB, open lid): 0.6 Full, 0.7a, 0.7b; 5 s each with 0.3 s fade, 0.6 s gaps, 1.2 s between groups.\n24-bit copy scaled by %.3f (raw copy unscaled).\n' % sc)

def mics(ctx):
    gb, _ = gain_b(ctx); notes = [(0, 4.0, 48, 70), (0.02, 4.0, 52, 66), (0.04, 4.0, 55, 68), (0.06, 4.0, 60, 72), (0.08, 4.0, 64, 70)]
    out = []; gap = np.zeros((int(0.6*M.FS), 2)); labels = []; w = ctx.work
    for lid in ('open', 'half', 'closed'):
        ef = np.load(w/f'irb_full_{lid}.npz'); E = ef['E']
        table = ctx.table_b if lid == 'open' else ctx.table_b
        pb = render(w, 'mics_chord', notes, 'b_p', table, ctx.sr_p)
        for pr, mm in PRESET_MICS.items():
            mi = [MICS.index(m) for m in mm]; x = board_signal(pb, E, mi); x = add(x, strings_signal(pb, mi))*gb
            out += [seg(x, 5.0), gap]; labels.append(f'{lid}/{pr}')
    x = np.concatenate(out); pk = np.abs(x).max(); print('mics peak', pk, labels)
    sc = 1/max(1, pk*1.05)
    R.write_wav24(ctx.out/'grand-mics-0.7b.wav', x*sc); R.write_wav_f32(ctx.out/'grand-mics-0.7b-raw.wav', x)
    (ctx.out/'grand-mics-0.7b-order.txt').write_text('\n'.join(labels) + '\n(each 5 s + 0.6 s gap; 24-bit copy scaled by %.3f; the strings are radiated directly, the board through the lid-state impulse responses)\n' % sc)

def piece(ctx, events_path, name, t0, t1, tail, end_s):
    gb, _ = gain_b(ctx); w = ctx.work
    ev = [l.split() for l in open(events_path)]
    end = int(end_s*M.FS)
    keep = [l for l in ev if int(l[0]) < end] + [[str(end), '176', '64', '0']]
    evf = w/f'{name}.events'; evf.write_text(''.join(' '.join(l) + '\n' for l in keep))
    pb = w/f'rb_b_p_{name}'
    if not Path(str(pb) + '.info.txt').exists():
        r = subprocess.run([str(EXE), str(evf), str(ctx.table_b), str(pb), '--full', '--tail', str(tail), '--strrad', str(ctx.sr_p)], capture_output=True, text=True)
        if r.returncode: raise RuntimeError(r.stderr)
        print(name, r.stdout.strip(), flush=True)
    for label, mm, ef in (('closeab', PRESET_MICS['close'], 'open'), ('audience', PRESET_MICS['audience'], 'open')):
        E = np.load(w/f'irb_full_{ef}.npz')['E']; mi = [MICS.index(m) for m in mm]
        y = add(board_signal(pb, E, mi), strings_signal(pb, mi))*gb
        y = y[int(t0*M.FS):int(t1*M.FS)]
        y[:480] *= np.linspace(0, 1, 480)[:, None]; y[-2400:] *= np.linspace(1, 0, 2400)[:, None]
        pk = np.abs(y).max(); print(name, label, 'peak', pk, flush=True)
        R.write_wav_f32(ctx.out/f'{name}-0.7b-{label}-raw.wav', y); R.write_wav24(ctx.out/f'{name}-0.7b-{label}.wav', y/max(1, pk*1.05))

def chopin(ctx):
    ev = REPO/'mvp/piano-v4/listening/chopin-op28-no4.events'
    last = int(open(ev).read().split('\n')[-2].split()[0])
    piece(ctx, ev, 'chopin-op28-no4', 0.0, last/M.FS + 3.0, 3.0, last/M.FS)

def etude(ctx):
    ev = REPO/'mvp/piano-v4/listening/chopin-op10-no12.events'
    piece(ctx, ev, 'chopin-op10-no12', 89.0, 101.0, 2.0, 100.0)

if __name__ == '__main__':
    work, out, modes, step = sys.argv[1:5]
    if step == 'validate_partial':
        validate(Ctx(work, modes, out), ('A0', 'A1', 'A2'), 'grand-validation-0.7b-partial.json')
    else:
        {'validate': validate, 'ab': ab, 'mics': mics, 'chopin': chopin, 'etude': etude}[step](Ctx(work, modes, out))

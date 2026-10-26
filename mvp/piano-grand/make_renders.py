# SPDX-License-Identifier: MIT
"""Piano 0.7a renders and Salamander validation.
Usage: make_renders.py WORK REPO_OUT MODES.npz STEP   with STEP in {validate, ab, mics, chopin}
 validate: A2, C4, C6 at velocities 40, 60, 92: 0.6 Full and 0.7a (close AB, open lid) against the Salamander layers 4, 8, 12.
 ab:       grand-ab-0.7a.wav (C2, C4, C6, chord; 0.6 Full then 0.7a close AB)
 mics:     grand-mics-0.7a.wav (one chord at every microphone preset and lid state)
 chopin:   Op. 28 No. 4 excerpt at the close-AB and audience presets (open lid)"""
import json, subprocess, sys
from pathlib import Path
import numpy as np
import render_grand as R, metrics as M, scene

REPO = Path(__file__).resolve().parents[2]
SALAMANDER = {'A2': (45, 4, 40), 'C4': (60, 8, 60), 'C6': (84, 12, 92)}
PRESET_MICS = {'close': ('closeL', 'closeR'), 'player': ('playerL', 'playerR'), 'audience': ('audL', 'audR')}
MICS = list(scene.MICS)

def ensure_render(work, modes, lid, name, notes, tail=4.0):
    work = Path(work); ev = work/f'{name}.events'; ev.write_text(R.events_text(notes))
    table = work/f'table_{lid}.bin'
    if not table.exists(): R.write_table(table, *R.board_table(work, modes, lid))
    pre = work/f'r_{lid}_{name}'
    if not Path(str(pre) + '.info.txt').exists():
        print(name, lid, R.run_render(REPO/'out/piano-mvp-v4/piano-grand/piano-grand-render', ev, table, pre, tail), flush=True)
    return pre

def render_06(work, name, notes):
    work = Path(work); ev = work/f'{name}.events'; ev.write_text(R.events_text(notes))
    out = work/f'r06_{name}.wav'
    if not out.exists():
        subprocess.run([str(REPO/'out/piano-mvp-v4/piano-midi-render'), str(ev), str(out), '--full', '--float'], check=True, capture_output=True)
    return M.read_wav(out)

def load_E(work, lid):
    return np.load(Path(work)/f'ir_{lid}.npz')['E']

def mics_signal(pre, E, mics, gain):
    idx = [MICS.index(m) for m in mics]
    return R.radiate(pre, E, idx, gain).T

def gain_match(work, modes, E):
    """One global gain for all 0.7a renders: close-AB RMS of C4 v60 over 0.05-1 s equals the 0.6 Full render of the same note."""
    notes = [(0.0, 8.0, 60, 60)]
    a = M.align(render_06(work, 'c4v60', notes))
    pre = ensure_render(work, modes, 'open', 'c4v60', notes)
    b = M.align(mics_signal(pre, E, PRESET_MICS['close'], 1.0))
    w = slice(int(0.05*M.FS), int(1.0*M.FS))
    return float(np.sqrt(np.mean(a[w]**2))/np.sqrt(np.mean(b[w]**2)))

def validate(work, repo_out, modes):
    E = load_E(work, 'open'); gain = gain_match(work, modes, E)
    ref_dir = REPO/'out/piano-reference-cache'
    rows = {}
    for note, (key, layer, vel) in SALAMANDER.items():
        notes = [(0.0, 8.0, key, vel)]
        ref = M.load_ref(ref_dir/f'{note}v{layer}.flac')
        a06 = M.align(render_06(work, f'{note.lower()}v{vel}', notes))
        pre = ensure_render(work, modes, 'open', f'{note.lower()}v{vel}', notes)
        a07 = M.align(mics_signal(pre, E, PRESET_MICS['close'], gain))
        rows[f'{note}_v{vel}'] = {k: {m: (v.tolist() if hasattr(v, 'tolist') else v) for m, v in M.all_metrics(x).items()} for k, x in (('reference', ref), ('0.6', a06), ('0.7a', a07))}
        print(note, vel, 'done', flush=True)
    Path(repo_out, 'grand-validation-0.7a.json').write_text(json.dumps(dict(gain=gain, rows=rows), indent=1))
    return rows, gain

def pad(x, n):
    return np.concatenate([x, np.zeros((max(0, n - len(x)), x.shape[1]))])[:n]

def gain_of(repo_out):
    return json.loads((Path(repo_out)/'grand-validation-0.7a.json').read_text())['gain']

def seg(x, dur=5.0, fade=0.3):
    y = x[:int(dur*M.FS)].copy(); n = int(fade*M.FS); y[-n:] *= np.linspace(1, 0, n)[:, None]; return y

def ab(work, repo_out, modes):
    g = gain_of(repo_out); E = load_E(work, 'open'); gap = np.zeros((int(0.6*M.FS), 2)); out = []
    items = [('C2', [(0, 4.0, 36, 70)]), ('C4', [(0, 4.0, 60, 70)]), ('C6', [(0, 4.0, 84, 70)]),
             ('chord', [(0, 4.0, 48, 70), (0.02, 4.0, 52, 66), (0.04, 4.0, 55, 68), (0.06, 4.0, 60, 72), (0.08, 4.0, 64, 70)])]
    for name, notes in items:
        a = render_06(work, 'ab_' + name, notes)[:int(5*M.FS)]
        pre = ensure_render(work, modes, 'open', 'ab_' + name, notes)
        b = mics_signal(pre, E, PRESET_MICS['close'], g)
        out += [seg(a), gap, seg(b), gap, gap]
    x = np.concatenate(out); assert np.abs(x).max() < 1, np.abs(x).max()
    R.write_wav24(Path(repo_out)/'grand-ab-0.7a.wav', x); R.write_wav_f32(Path(repo_out)/'grand-ab-0.7a-raw.wav', x)
    print('ab peak', np.abs(x).max())

def mics(work, repo_out, modes):
    g = gain_of(repo_out); notes = [(0, 4.0, 48, 70), (0.02, 4.0, 52, 66), (0.04, 4.0, 55, 68), (0.06, 4.0, 60, 72), (0.08, 4.0, 64, 70)]
    out = []; gap = np.zeros((int(0.6*M.FS), 2)); labels = []
    for lid in ('open', 'half', 'closed'):
        if not (Path(work)/f'ir_{lid}.npz').exists(): continue
        E = load_E(work, lid); pre = ensure_render(work, modes, lid, 'mics_chord', notes)
        for pr, mm in PRESET_MICS.items():
            out += [seg(mics_signal(pre, E, mm, g), 5.0), gap]; labels.append(f'{lid}/{pr}')
    x = np.concatenate(out)
    pk = np.abs(x).max(); print('mics peak', pk, labels)
    R.write_wav24(Path(repo_out)/'grand-mics-0.7a.wav', x/max(1, pk*1.05)); R.write_wav_f32(Path(repo_out)/'grand-mics-0.7a-raw.wav', x)
    Path(repo_out, 'grand-mics-0.7a-order.txt').write_text('\n'.join(labels) + '\n(each 5 s + 0.6 s gap; 24-bit copy scaled by 1/%.2f if the raw peak exceeds 1)\n' % max(1, pk*1.05))

def chopin(work, repo_out, modes):
    g = gain_of(repo_out); E = load_E(work, 'open')
    ev = [l.split() for l in open(REPO/'mvp/piano-v4/listening/chopin-op28-no4.events')]
    end = 64*M.FS
    keep = [l for l in ev if int(l[0]) < end] + [[str(end), '176', '64', '0']]
    evf = Path(work)/'chopin.events'; evf.write_text(''.join(' '.join(l) + '\n' for l in keep))
    table = Path(work)/'table_open.bin'
    if not table.exists(): R.write_table(table, *R.board_table(work, modes, 'open'))
    pre = Path(work)/'r_open_chopin'
    if not Path(str(pre) + '.info.txt').exists():
        print(R.run_render(REPO/'out/piano-mvp-v4/piano-grand/piano-grand-render', evf, table, pre, 2.0), flush=True)
    for name, mm in (('closeab', PRESET_MICS['close']), ('audience', PRESET_MICS['audience'])):
        y = mics_signal(pre, E, mm, g)[40*M.FS:66*M.FS]
        y[:480] *= np.linspace(0, 1, 480)[:, None]; y[-2400:] *= np.linspace(1, 0, 2400)[:, None]
        pk = np.abs(y).max(); print(name, 'peak', pk)
        R.write_wav_f32(Path(repo_out)/f'chopin-op28-no4-0.7a-{name}-raw.wav', y)
        R.write_wav24(Path(repo_out)/f'chopin-op28-no4-0.7a-{name}.wav', y/max(1, pk*1.05))

if __name__ == '__main__':
    work, repo_out, modes, step = sys.argv[1:5]
    {'validate': validate, 'ab': ab, 'mics': mics, 'chopin': chopin}[step](work, repo_out, modes)

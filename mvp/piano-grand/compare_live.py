# SPDX-License-Identifier: MIT
"""Piano 0.7c: the real-time grand against the 0.7b offline render (same events, close AB and audience, open lid).
  compare_live.py WORK REPO_OUT MODES.npz STEP     STEP in {notes, piece, ab}
 notes: single notes and the chord (velocity 70, 4 s) -> error tables; piece: whole Chopin Op. 28 No. 4 and the Etude excerpt -> error tables;
 ab: grand-live-ab-0.7c.wav and its order file.
The live signals come from build/piano-grand-live-render (Piano, Body Grand, block interface 128, helper thread 1); the offline ones from the cached
bridge-force and string records of make_renders_b.py convolved with the 0.7b responses, times the level factor, as in grand-ab-0.7b.wav."""
import json, subprocess, sys
from pathlib import Path
import numpy as np
sys.path.insert(0, str(Path(__file__).resolve().parent))
import make_renders_b as MB, render_grand as R

REPO = Path(__file__).resolve().parents[2]
EXE = REPO/'out/piano-mvp-v4/piano-grand-live-render'
FS = 48000
BANDS = [(31.25, 62.5), (62.5, 125), (125, 250), (250, 500), (500, 1000), (1000, 2000), (2000, 4000), (4000, 8000), (8000, 16000)]
PRESET = {'close': 0, 'player': 1, 'audience': 2}

def live(work, events, tag, quality, mic, tail, lid=0, extra=()):
    out = Path(work)/f'live_{tag}_{quality}_m{mic}_l{lid}.f32'
    if not out.exists():
        r = subprocess.run([str(EXE), str(events), str(out), '--quality', quality, '--mic', str(mic), '--lid', str(lid), '--tail', str(tail), '--block', '128', '--threads', '1', *extra], capture_output=True, text=True)
        if r.returncode: raise RuntimeError(r.stderr + r.stdout)
        print(tag, quality, mic, r.stdout.strip(), flush=True)
    return np.fromfile(out, np.float32).reshape(-1, 2).astype(np.float64)

def tables(a, b, n=None):
    """a: reference, b: test (frames, 2). Returns rms error (dB re reference rms) and per octave band (error dB, level difference dB)."""
    n = min(len(a), len(b)) if n is None else n
    a, b = a[:n], b[:n]
    tot = 10*np.log10(np.sum((a - b)**2)/np.sum(a**2))
    A = np.fft.rfft(a, axis=0); B = np.fft.rfft(b, axis=0); f = np.fft.rfftfreq(n, 1/FS); rows = []
    for lo, hi in BANDS:
        k = (f >= lo) & (f < hi)
        ea = np.sum(np.abs(A[k])**2); rows.append(dict(lo=lo, hi=hi, err_db=float(10*np.log10(np.sum(np.abs(A[k] - B[k])**2)/ea + 1e-30)), level_db=float(10*np.log10(np.sum(np.abs(B[k])**2)/ea + 1e-30))))
    return dict(rms_err_db=float(tot), bands=rows)

def offline_notes(ctx, name, notes, mics):
    gb, _ = MB.gain_b(ctx)
    pre = MB.render(ctx.work, 'ab_' + name, notes, 'b_p', ctx.table_b, ctx.sr_p)
    E = ctx.E_full['E']; mi = [MB.MICS.index(m) for m in mics]
    return MB.add(MB.board_signal(pre, E, mi), MB.strings_signal(pre, mi))*gb

ITEMS = [('C2', [(0, 4.0, 36, 70)]), ('C4', [(0, 4.0, 60, 70)]), ('C6', [(0, 4.0, 84, 70)]),
         ('chord', [(0, 4.0, 48, 70), (0.02, 4.0, 52, 66), (0.04, 4.0, 55, 68), (0.06, 4.0, 60, 72), (0.08, 4.0, 64, 70)])]

def notes_step(ctx, repo_out):
    rep = {}
    for pr in ('close', 'audience'):
        mm = MB.PRESET_MICS[pr]
        for name, notes in ITEMS:
            off = offline_notes(ctx, name, notes, mm)[:int(5*FS)]
            ev = Path(ctx.work)/f'ab_{name}.events'
            full = live(ctx.work, ev, f'ab_{name}', 'full', PRESET[pr], 4.0)[:int(5*FS)]
            lv = live(ctx.work, ev, f'ab_{name}', 'live', PRESET[pr], 4.0)[:int(5*FS)]
            rep[f'{pr}/{name}'] = dict(full_vs_offline=tables(off, full), live_vs_offline=tables(off, lv), live_vs_full=tables(full, lv))
    Path(repo_out, 'grand-live-notes-0.7c.json').write_text(json.dumps(rep, indent=1)); return rep

def piece_step(ctx, repo_out):
    rep = {}
    gb, _ = MB.gain_b(ctx)
    for name, evf, tail in (('chopin-op28-no4', REPO/'mvp/piano-v4/listening/chopin-op28-no4.events', 3.0), ('chopin-op10-no12', Path(ctx.work)/'chopin-op10-no12.events', 2.0)):
        pre = Path(ctx.work)/f'rb_b_p_{name}'
        for pr in ('close', 'audience'):
            mm = MB.PRESET_MICS[pr]; mi = [MB.MICS.index(m) for m in mm]
            off = MB.add(MB.board_signal(pre, ctx.E_full['E'], mi), MB.strings_signal(pre, mi))*gb
            for q in ('full', 'live'):
                lv = live(ctx.work, evf, name, q, PRESET[pr], tail)
                n = min(len(off), len(lv)); rep.setdefault(f'{pr}/{name}', {})[f'{q}_vs_offline'] = tables(off, lv, n)
                if q == 'live': rep[f'{pr}/{name}']['live_vs_full'] = tables(live(ctx.work, evf, name, 'full', PRESET[pr], tail), lv)
    Path(repo_out, 'grand-live-piece-0.7c.json').write_text(json.dumps(rep, indent=1)); return rep

def seg(x, dur=4.0, fade=0.3):
    y = x[:int(dur*FS)].copy(); n = int(fade*FS); y[-n:] *= np.linspace(1, 0, n)[:, None]; return y

def ab_step(ctx, repo_out):
    gap = np.zeros((int(0.5*FS), 2)); out = []; order = []
    for pr, names in (('close', ('C2', 'C4', 'C6', 'chord')), ('audience', ('C4', 'chord'))):
        mm = MB.PRESET_MICS[pr]
        for name, notes in ITEMS:
            if name not in names: continue
            ev = Path(ctx.work)/f'ab_{name}.events'
            for lab, x in (('0.7b offline', offline_notes(ctx, name, notes, mm)), ('0.7c Full Grand', live(ctx.work, ev, f'ab_{name}', 'full', PRESET[pr], 4.0)),
                           ('0.7c Live Grand', live(ctx.work, ev, f'ab_{name}', 'live', PRESET[pr], 4.0))):
                out += [seg(x), gap]; order.append(f'{pr} {name}: {lab}')
            out.append(gap)
    x = np.concatenate(out); pk = np.abs(x).max(); sc = 1/max(1, pk*1.05)
    R.write_wav24(Path(repo_out)/'grand-live-ab-0.7c.wav', x*sc)
    Path(repo_out, 'grand-live-ab-0.7c-order.txt').write_text('Each item 4 s with a 0.3 s fade, 0.5 s gaps, an extra 0.5 s between groups; velocity 70, open lid. Per group: 0.7b offline (piano-grand-render + 1 s responses), 0.7c Full Grand (the plug-in code, Full quality), 0.7c Live Grand (Live quality, 128-frame blocks, one helper thread).\n' + '\n'.join(f'{i + 1}. {o}' for i, o in enumerate(order)) + f'\n24-bit scaled by {sc:.3f}.\n')

if __name__ == '__main__':
    work, out, modes, step = sys.argv[1:5]
    ctx = MB.Ctx(work, modes, out)
    {'notes': notes_step, 'piece': piece_step, 'ab': ab_step}[step](ctx, out)

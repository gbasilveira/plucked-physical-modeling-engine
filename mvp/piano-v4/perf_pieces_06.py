#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""0.6: whole-piece live-performance measurement with piano-block-bench (48 kHz, 1 helper thread).

  perf_pieces_06.py OUTDIR   -> OUTDIR/perf-raw-0.6/*.txt|series, performance-report-0.6.json, performance-report-0.6.txt
Needs OUTDIR/<piece>-0.6.events (written by render_pieces_06.py), the pieces' Full render JSON for the render
timings, and the passage descriptions it derives itself from the event file.
"""
import json, re, subprocess, sys
from pathlib import Path
import numpy as np

FS = 48000
PIECES = {'chopin-prelude': 'Chopin Prelude Op.28 No.4', 'chopin-etude-op10-12': 'Chopin Etude Op.10 No.12'}
MATRIX = [('live', False, [64, 128, 256]), ('live', True, [64, 128, 256]),
          ('balanced', False, [64, 128, 256]), ('balanced', True, [64, 128, 256]), ('full', False, [128, 1024])]
NOTE = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B']
name_of = lambda k: f'{NOTE[k % 12]}{k//12-1}'

def parse(text):
    w = re.search(r'WINDOW worst1s_mean ([\d.]+) at ([\d.]+) s; shed_max (\d+) shed_seconds ([\d.]+) shed_blocks (\d+) max_active_voices (\d+)', text)
    m = re.search(r'mean ([\d.]+) p99 ([\d.]+) p99\.9 ([\d.]+) max ([\d.]+) misses (\d+)/(\d+) peak ([\d.]+) over1 (\d+) stalls (\d+) shed (\d+) \(events (\d+)\) steals (\d+)', text)
    g = [float(x) for x in m.groups()]
    return dict(mean=g[0], x_realtime=1/g[0], p99=g[1], p999=g[2], max=g[3], late_blocks=int(g[4]), blocks=int(g[5]), peak=g[6], samples_over_1=int(g[7]),
                radiation_stalls=int(g[8]), shed_level_final=int(g[9]), shed_events=int(g[10]), steals=int(g[11]),
                worst_1s_mean=float(w.group(1)), worst_1s_at_s=float(w.group(2)), shed_level_max=int(w.group(3)),
                shed_seconds=float(w.group(4)), shed_blocks=int(w.group(5)), max_active_voices=int(w.group(6)), raw_line=text.strip().splitlines()[-1])

def passages(events, series, top=6, window=3):
    """Most expensive 3 s windows of the Full series, with the notes struck in them."""
    ev = [list(map(int, l.split())) for l in events.splitlines()]
    mean = np.array([s[1] for s in series]); t = np.array([s[0] for s in series])
    k = int(window); cs = np.convolve(mean, np.ones(k)/k, mode='valid'); order = np.argsort(-cs); picks = []
    for i in order:
        if all(abs(i-j) >= k for j in picks): picks.append(int(i))
        if len(picks) == top: break
    out = []
    for i in sorted(picks, key=lambda i: -cs[i]):
        a, b = t[i]*FS, (t[i]+k)*FS
        on = [e for e in ev if e[1] == 144 and e[3] > 0 and a <= e[0] < b]
        keys = [e[2] for e in on]; ped = [e for e in ev if e[1] == 176 and a <= e[0] < b]
        # keys still sounding from the pedal/held before the window are not counted: report strikes and pedal only
        out.append(dict(start_s=float(t[i]), end_s=float(t[i]+k), mean_full_ratio=float(cs[i]), strikes=len(on), strikes_per_s=len(on)/k,
                        lowest=name_of(min(keys)) if keys else None, highest=name_of(max(keys)) if keys else None,
                        bass_strikes_below_C3=sum(1 for x in keys if x < 48), mean_velocity=float(np.mean([e[3] for e in on])) if on else None,
                        pedal_changes=len(ped), pedal_down_at_start=None))
    return out

def main():
    d = Path(sys.argv[1]).resolve(); raw = d/'perf-raw-0.6'; raw.mkdir(exist_ok=True)
    report = dict(version='0.6', machine='Intel Core i3-8100 (4 cores, no SMT), 48 kHz, 1 helper thread, steady_clock per block',
                  radiation='Auto (Baked for Live/Balanced, Modal for Full)', imperfection=1.0, voices='Auto (preset)', pieces={})
    for piece, title in PIECES.items():
        evf = d/f'{piece}-0.6.events'; events = evf.read_text(); last = int(events.splitlines()[-1].split()[0])
        dur = (last + 4*FS)/FS; runs = []
        for quality, adaptive, blocks in MATRIX:
            for b in blocks:
                tag = f'{piece}-{quality}-{"adaptive" if adaptive else "static"}-{b}'
                cmd = [str(d/'piano-block-bench'), '--events', str(evf), '--whole', '--block', str(b), '--fs', '48000', '--quality', quality,
                       '--threads', '1', '--series', str(raw/f'{tag}.series')] + (['--adaptive'] if adaptive else [])
                r = subprocess.run(cmd, check=True, capture_output=True, text=True)
                (raw/f'{tag}.txt').write_text(r.stdout); row = parse(r.stdout)
                row.update(quality=quality, adaptive=adaptive, block=b, piece_seconds=dur, wall_seconds=dur*row['mean']); runs.append(row)
                print(tag, f"x{row['x_realtime']:.2f} mean {row['mean']:.3f} p99 {row['p99']:.3f} p99.9 {row['p999']:.3f} max {row['max']:.3f} late {row['late_blocks']}/{row['blocks']} shed {row['shed_level_max']} {row['shed_seconds']:.1f}s steals {row['steals']}", flush=True)
        full = [r for r in runs if r['quality'] == 'full']
        series = [list(map(float, l.split())) for l in (raw/f'{piece}-full-static-128.series').read_text().splitlines()]
        full_sum = dict(piece_seconds=dur, mean_ratio_block128=full[0]['mean'], x_realtime_block128=full[0]['x_realtime'], worst_1s_mean=full[0]['worst_1s_mean'],
                        worst_1s_at_s=full[0]['worst_1s_at_s'], mean_ratio_block1024=full[1]['mean'], worst_1s_mean_block1024=full[1]['worst_1s_mean'],
                        peak_per_second_mean=max(s[1] for s in series), cores_needed_mean=full[0]['mean'], cores_needed_worst_1s=full[0]['worst_1s_mean'],
                        cores_needed_p99_block=full[0]['p99'], cores_needed_max_block=full[0]['max'])
        report['pieces'][piece] = dict(title=title, runs=runs, full=full_sum, expensive_passages=passages(events, series))
    (d/'performance-report-0.6.json').write_text(json.dumps(report, indent=2)+'\n')
    L = [f"Piano 0.6 whole-piece performance ({report['machine']})", '']
    for piece, p in report['pieces'].items():
        L += [f"== {p['title']} ({p['full']['piece_seconds']:.1f} s incl. 4 s tail) ==",
              f"{'quality':9s}{'adaptive':9s}{'block':>6s}{'xRT':>8s}{'mean':>7s}{'p99':>7s}{'p99.9':>7s}{'max':>7s}{'late/total':>14s}{'shed max':>9s}{'shed s':>8s}{'steals':>7s}{'worst1s':>8s}"]
        for r in p['runs']:
            L.append(f"{r['quality']:9s}{'on' if r['adaptive'] else 'off':9s}{r['block']:6d}{r['x_realtime']:8.2f}{r['mean']:7.3f}{r['p99']:7.3f}{r['p999']:7.3f}{r['max']:7.3f}{str(r['late_blocks'])+'/'+str(r['blocks']):>14s}{r['shed_level_max']:9d}{r['shed_seconds']:8.1f}{r['steals']:7d}{r['worst_1s_mean']:8.3f}")
        f = p['full']
        L += ['', f"Full: whole-piece mean deadline ratio {f['mean_ratio_block128']:.2f} (x{f['x_realtime_block128']:.3f} real time); worst sliding 1 s window {f['worst_1s_mean']:.2f} at {f['worst_1s_at_s']:.1f} s;",
              f"  block 1024: mean {f['mean_ratio_block1024']:.2f}, worst 1 s {f['worst_1s_mean_block1024']:.2f}. Equivalent cores at perfect scaling (mean/worst 1 s/p99 block/max block): {f['cores_needed_mean']:.1f} / {f['cores_needed_worst_1s']:.1f} / {f['cores_needed_p99_block']:.1f} / {f['cores_needed_max_block']:.1f}", '',
              'Most expensive 3 s windows of the Full render (mean deadline ratio, block 128):']
        for q in p['expensive_passages']:
            L.append(f"  {q['start_s']:6.1f}-{q['end_s']:6.1f} s  ratio {q['mean_full_ratio']:5.2f}  {q['strikes']} strikes ({q['strikes_per_s']:.1f}/s), {q['lowest']}..{q['highest']}, {q['bass_strikes_below_C3']} below C3, mean vel {q['mean_velocity']}, {q['pedal_changes']} pedal changes")
        L.append('')
    (d/'performance-report-0.6.txt').write_text('\n'.join(L)+'\n'); print('\n'.join(L))

if __name__ == '__main__': main()

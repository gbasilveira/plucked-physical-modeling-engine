#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Validate the pinned expressive MIDI and prepare a sample-timed render fixture.

Requires mido==1.3.3. Run from the repository root. --check verifies the committed
fixture and audit without rewriting either; any source/performance drift fails.
"""
import collections
import hashlib
import importlib.metadata
import json
from pathlib import Path
import sys
import mido

ROOT = Path(__file__).resolve().parent / 'listening'
SOURCE = ROOT / 'chopin-op28-no4.mid'
EXPECTED = '8b8984ae232642ff1979b84af642d5d5db60f7f7f20b3745f2d4b4f17cdaf908'

def main():
    assert importlib.metadata.version('mido') == '1.3.3', 'use pinned mido==1.3.3'
    digest = hashlib.sha256(SOURCE.read_bytes()).hexdigest()
    assert digest == EXPECTED, 'source MIDI changed'
    midi = mido.MidiFile(SOURCE)
    assert midi.type == 1 and midi.ticks_per_beat == 480 and len(midi.tracks) == 9
    t = 0.0
    active = collections.Counter()
    notes = []; tempos = []; controls = []; rows = []; ignored = []
    maximum = 0
    overlaps = []
    for msg in midi:  # mido merges tracks and applies the complete tempo map.
        assert msg.time >= 0
        t += msg.time
        frame = round(t * 48000)
        if msg.type == 'set_tempo':
            assert msg.tempo > 0
            tempos.append([frame, msg.tempo])
        elif msg.type in ('note_on', 'note_off'):
            assert msg.channel == 0 and 21 <= msg.note <= 108
            on = msg.type == 'note_on' and msg.velocity > 0
            key = (msg.channel, msg.note)
            if on:
                active[key] += 1
                if active[key] > 1:
                    overlaps.append([frame, msg.note])
                    assert t >= 64, 'overlapping same-key notes inside rendered passage'
                notes.append([frame, msg.note, msg.velocity])
            else:
                assert active[key] >= 1, 'unmatched note off'
                active[key] -= 1
            maximum = max(maximum, sum(active.values()))
            rows.append(f'{frame} {144 if on else 128} {msg.note} {msg.velocity}\n')
        elif msg.type == 'control_change':
            assert msg.channel == 0
            controls.append([frame, msg.control, msg.value])
            if msg.control == 64:
                rows.append(f'{frame} 176 64 {msg.value}\n')
            else:
                assert msg.control in (7, 10, 91), 'new unsupported controller'
                ignored.append([frame, msg.control, msg.value])
        elif msg.type == 'program_change':
            assert msg.channel == 0 and msg.program == 0, 'non-piano program'
        else:
            assert msg.is_meta, 'unexpected MIDI message'
    assert not sum(active.values())
    assert len(notes) == 604 and len(tempos) == 335
    assert len([c for c in controls if c[1] == 64]) == 133
    passage = [n for n in notes if 40*48000 <= n[0] < 64*48000]
    assert min(n[2] for n in passage) <= 20 and max(n[2] for n in passage) >= 90
    events = ''.join(rows)
    audit = dict(piece='Chopin: Prelude in E minor, Op. 28 No. 4',
                 midi_author='Bernd Krueger', source_sha256=digest,
                 source_url='https://commons.wikimedia.org/wiki/File:Chopin_-_Prelude_in_E_Minor.mid',
                 license='CC-BY-SA-3.0-DE', mido_version='1.3.3',
                 midi_format=midi.type, ticks_per_beat=midi.ticks_per_beat,
                 tracks=len(midi.tracks), total_note_ons=len(notes),
                 tempo_changes=len(tempos), sustain_events=133,
                 max_simultaneously_held_keys=maximum, unmatched_note_offs=0,
                 overlapping_same_key_ons=overlaps,
                 overlap_note='three overlaps occur after 64 s, outside all rendered/preroll events',
                 note_velocity_range=[min(n[2] for n in notes),max(n[2] for n in notes)],
                 excerpt_start_seconds=40, excerpt_end_seconds=64, tail_seconds=2,
                 wav_duration_seconds=26, preroll='simulate source from time zero; export from 40 s',
                 excerpt_note_ons=len(passage),
                 excerpt_velocity_range=[min(n[2] for n in passage),max(n[2] for n in passage)],
                 excerpt_key_range=[min(n[1] for n in passage),max(n[1] for n in passage)],
                 midi_edits='none; preserve notes, velocities, tempo map and sustain; round timestamps to 48 kHz',
                 ignored_mixer_controls=ignored,
                 ignored_control_explanation='CC7 volume, CC10 pan and CC91 external reverb are not synthesizer controls; source retained unchanged',
                 event_schedule_sha256=hashlib.sha256(events.encode()).hexdigest())
    outputs = {'chopin-op28-no4.events':events,
               'chopin-op28-no4.audit.json':json.dumps(audit,indent=2)+'\n'}
    for name, content in outputs.items():
        path = ROOT/name
        if '--check' in sys.argv:
            assert path.read_text() == content, f'{name} changed; inspect the MIDI/performance diff'
        else:
            path.write_text(content)
    print(json.dumps(audit,indent=2))

if __name__ == '__main__':
    main()

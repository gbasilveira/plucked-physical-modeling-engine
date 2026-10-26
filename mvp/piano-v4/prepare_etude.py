#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Fetch/validate the pinned Chopin Etude Op.10 No.12 MIDI and prepare its sample-timed fixture.

Requires mido==1.3.3. The MIDI is never modified; --check verifies the committed fixture/audit.
  prepare_etude.py --fetch DIR   download the pinned file into a NEW directory DIR, verify SHA-256, do nothing else
  prepare_etude.py [--check]     validate listening/chopin-op10-no12.mid, write (or compare) .events and .audit.json
"""
import collections, hashlib, importlib.metadata, json, sys, urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent / 'listening'
SOURCE = ROOT / 'chopin-op10-no12.mid'
EXPECTED = 'a376584d5b8c7fb3983c46e4ddef3e5866c2c7ec11a3dd7b59fdecea9ba3914f'
URL = 'https://upload.wikimedia.org/wikipedia/commons/0/02/Chopin_-_Revolutionary_Etude.mid'
PAGE = 'https://commons.wikimedia.org/wiki/File:Chopin_-_Revolutionary_Etude.mid'

def fetch(directory):
    d = Path(directory); d.mkdir(parents=True, exist_ok=False)
    data = urllib.request.urlopen(urllib.request.Request(URL, headers={'User-Agent': 'pme-fetch/1.0'}), timeout=60).read()
    assert hashlib.sha256(data).hexdigest() == EXPECTED, 'downloaded MIDI differs from the pinned SHA-256'
    assert data[:4] == b'MThd'
    (d/'chopin-op10-no12.mid').write_bytes(data)
    print('verified', d/'chopin-op10-no12.mid')

def main():
    if '--fetch' in sys.argv:
        return fetch(sys.argv[sys.argv.index('--fetch')+1])
    import mido
    assert importlib.metadata.version('mido') == '1.3.3', 'use pinned mido==1.3.3'
    digest = hashlib.sha256(SOURCE.read_bytes()).hexdigest()
    assert digest == EXPECTED, 'source MIDI changed'
    midi = mido.MidiFile(SOURCE)
    assert midi.type == 1 and midi.ticks_per_beat == 480
    t = 0.0; active = collections.Counter()
    notes = []; tempos = []; pedal = []; ignored = []; rows = []; overlaps = []; maximum = 0
    for msg in midi:
        t += msg.time; frame = round(t*48000)
        if msg.type == 'set_tempo': tempos.append([frame, msg.tempo])
        elif msg.type in ('note_on', 'note_off'):
            assert msg.channel == 0 and 21 <= msg.note <= 108
            on = msg.type == 'note_on' and msg.velocity > 0
            key = msg.note
            if on:
                active[key] += 1
                if active[key] > 1: overlaps.append([frame, msg.note])
                notes.append([frame, msg.note, msg.velocity])
            else:
                assert active[key] >= 1, 'unmatched note off'; active[key] -= 1
            maximum = max(maximum, sum(active.values()))
            rows.append(f'{frame} {144 if on else 128} {msg.note} {msg.velocity}\n')
        elif msg.type == 'control_change':
            assert msg.channel == 0
            if msg.control == 64: pedal.append([frame, msg.value]); rows.append(f'{frame} 176 64 {msg.value}\n')
            else:
                assert msg.control in (7, 10, 91), 'new unsupported controller'; ignored.append([frame, msg.control, msg.value])
        elif msg.type == 'program_change': assert msg.channel == 0 and msg.program == 0
        else: assert msg.is_meta
    assert not sum(active.values())
    events = ''.join(rows)
    audit = dict(piece='Chopin: Etude in C minor, Op. 10 No. 12 ("Revolutionary")',
                 midi_author='Bernd Krueger (piano-midi.de)', source_sha256=digest, source_url=URL, source_page=PAGE,
                 license='CC-BY-SA-3.0-DE', license_url='https://creativecommons.org/licenses/by-sa/3.0/de/deed.en',
                 mido_version='1.3.3', midi_format=midi.type, ticks_per_beat=midi.ticks_per_beat, tracks=len(midi.tracks),
                 total_note_ons=len(notes), tempo_changes=len(tempos), sustain_events=len(pedal),
                 pedal_down_events=sum(1 for p in pedal if p[1] >= 64), pedal_up_events=sum(1 for p in pedal if p[1] < 64),
                 sustain_present=True, pedalling_added=False,
                 max_simultaneously_held_keys=maximum, unmatched_note_offs=0, overlapping_same_key_ons=len(overlaps),
                 note_velocity_range=[min(n[2] for n in notes), max(n[2] for n in notes)],
                 key_range=[min(n[1] for n in notes), max(n[1] for n in notes)],
                 duration_seconds=round(midi.length, 3),
                 midi_edits='none; notes, velocities, tempo map and CC64 preserved; timestamps rounded to 48 kHz',
                 ignored_mixer_controls=ignored,
                 ignored_control_explanation='CC7 volume, CC10 pan and CC91 external reverb are not synthesizer controls; source retained unchanged',
                 event_schedule_sha256=hashlib.sha256(events.encode()).hexdigest())
    outputs = {'chopin-op10-no12.events': events, 'chopin-op10-no12.audit.json': json.dumps(audit, indent=2)+'\n'}
    for name, content in outputs.items():
        path = ROOT/name
        if '--check' in sys.argv: assert path.read_text() == content, f'{name} changed'
        else: path.write_text(content)
    print(json.dumps(audit, indent=2))

if __name__ == '__main__': main()

#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Optional real-Carla LV2 smoke test; needs an already running JACK/PipeWire server.

Creates a separate, unconnected diagnostic instrument, not the user's Carla session.
For PipeWire on Fedora, run with LD_LIBRARY_PATH=/usr/lib64/pipewire-0.3/jack.
Usage: python3 carla_check.py /absolute/path/to/piano-mvp-v4.lv2
"""
import argparse
from pathlib import Path
import os
import sys
import tempfile
import time

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("bundle", type=Path)
    parser.add_argument("--backend-dir", default="/usr/share/carla")
    parser.add_argument("--library", default="/usr/lib64/carla/libcarla_standalone2.so")
    args = parser.parse_args()
    bundle = args.bundle.resolve()
    if not (bundle/"manifest.ttl").is_file(): raise ValueError("LV2 bundle not found")
    with tempfile.TemporaryDirectory(prefix="piano-carla-test-") as directory:
        # Avoid scanning unrelated plug-ins or modifying the host's search settings.
        (Path(directory)/bundle.name).symlink_to(bundle,target_is_directory=True)
        os.environ["LV2_PATH"] = directory
        sys.path.insert(0,args.backend_dir)
        from carla_backend import (CarlaHostDLL, ENGINE_OPTION_PROCESS_MODE,
            ENGINE_PROCESS_MODE_MULTIPLE_CLIENTS, ENGINE_OPTION_OSC_ENABLED,
            BINARY_NATIVE, PLUGIN_LV2)
        host = CarlaHostDLL(args.library,False)
        host.set_engine_option(ENGINE_OPTION_PROCESS_MODE,ENGINE_PROCESS_MODE_MULTIPLE_CLIENTS,"")
        host.set_engine_option(ENGINE_OPTION_OSC_ENABLED,0,"")
        if not host.engine_init("JACK","PianoLV2Diagnostic"):
            raise RuntimeError(host.get_last_error())
        try:
            if not host.add_plugin(BINARY_NATIVE,PLUGIN_LV2,str(bundle),"PianoLV2DiagnosticInstrument",
                "https://gbasilveira.github.io/plucked-physical-modeling-engine/mvp/piano-v4",0,None,0):
                raise RuntimeError(host.get_last_error())
            host.set_active(0,True)
            peaks = []
            for velocity in [32,72,112]:
                host.set_active(0,False)
                host.set_active(0,True)
                time.sleep(0.03)
                host.send_midi_note(0,0,60,velocity)
                peak = 0.0
                for _ in range(100):
                    host.engine_idle()
                    peak = max(peak,host.get_output_peak_value(0,True),host.get_output_peak_value(0,False))
                    time.sleep(0.01)
                host.send_midi_note(0,0,60,0)
                peaks.append(peak)
                print(f"Velocity {velocity}: actual Carla peak {peak:.6f}",flush=True)
            if not (peaks[0]>1e-5 and peaks[0]<peaks[1]<peaks[2] and peaks[2]>2*peaks[0]):
                raise RuntimeError("Actual Carla velocity response is flat or silent")
            import math
            print(f"PASS: actual Carla MIDI velocity, hard/soft peak difference {20*math.log10(peaks[2]/peaks[0]):.2f} dB")
        finally:
            host.remove_all_plugins()
            host.engine_close()

if __name__ == "__main__": main()

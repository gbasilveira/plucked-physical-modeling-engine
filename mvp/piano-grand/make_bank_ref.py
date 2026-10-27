# SPDX-License-Identifier: MIT
"""Piano 0.7c: the small reference of test grand-bank-error: 0.7b responses (times the level factor) of four ports for four (lid, mic) sets,
0.5 s, float32, from irb_full_{lid}.npz. Usage: make_bank_ref.py WORK OUT.bin [GAIN]"""
import struct, sys, json
from pathlib import Path
import numpy as np
SETS = [(0, 0), (0, 2), (1, 1), (2, 0)]; PORTS = [4, 30, 55, 80]; FRAMES = 24000; LIDS = ('open', 'half', 'closed')
def main(work, out, gain=0.005290300607788151):
    work = Path(work); data = []
    for lid, mic in SETS:
        E = np.load(work/f'irb_full_{LIDS[lid]}.npz')['E']
        data.append(np.stack([E[2*mic:2*mic + 2, p, :FRAMES].T*gain for p in PORTS]))      # (ports, frames, 2)
    scales = np.array([float(np.abs(d).max()) for d in data], '<f4'); arr = np.stack([d/sc for d, sc in zip(data, scales)]).astype('<f4')   # per-set scale
    hdr = struct.pack('<4sIIII', b'GREF', len(SETS), len(PORTS), FRAMES, 0) + struct.pack('<%dI' % (2*len(SETS)), *[v for s in SETS for v in s]) + struct.pack('<%dI' % len(PORTS), *PORTS) + scales.tobytes()
    Path(out).write_bytes(hdr + arr.tobytes()); print('wrote', out, len(hdr) + arr.nbytes)
if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2], *(float(x) for x in sys.argv[3:4]))

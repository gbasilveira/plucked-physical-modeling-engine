# SPDX-License-Identifier: MIT
"""Write the 512-row board table read by the BoardOverride hook (piano-grand-render, grand-passivity-check).
Usage: make_table.py MODES.npz OUT.bin [WORK LID]   (without WORK/LID: in-vacuo poles)"""
import sys
import numpy as np
import render_grand as R
modes, out = sys.argv[1:3]
if len(sys.argv) > 4:
    om, sg, phi = R.board_table(sys.argv[3], modes, sys.argv[4])
else:
    d = np.load(modes); n = len(d['f']); om = np.full(512, 1.0e6); sg = np.zeros(512); phi = np.zeros((88, 512))
    om[:n] = 2*np.pi*d['f']; sg[:n] = 0.5*0.02*om[:n]; phi[:, :n] = d['phi']
R.write_table(out, om, sg, phi)
print('wrote', out)

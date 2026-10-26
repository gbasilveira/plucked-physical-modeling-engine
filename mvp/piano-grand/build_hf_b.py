# SPDX-License-Identifier: MIT
"""Build and save the production statistical board set (3.6-16 kHz; modes above 10 kHz are decimated by 6, see hfboard.make_statboard). Usage: build_hf_b.py MODES.npz OUT.npz"""
import sys, time
import numpy as np, hfboard as H
t = time.time(); hf = H.build_hf_set(sys.argv[1], 16000.0, seed=1, decimate_above=(10000.0, 6.0))
H.save_hf_set(sys.argv[2], hf)
sb = hf['sb']; print(f'{len(sb.f)} statistical modes {sb.f[0]:.0f}-{sb.f[-1]:.0f} Hz ({(sb.f < 10000).sum()} below 10 kHz); density anchor FEM/Weyl = {hf["density_anchor"]:.3f}; mobility slope {hf["slope"]:.3f}; {time.time()-t:.0f} s')

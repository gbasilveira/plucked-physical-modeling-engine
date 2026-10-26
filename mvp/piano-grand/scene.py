# SPDX-License-Identifier: MIT
"""Scene builder for the near-field FDTD (Piano 0.7a stage 2): rasterises rim, front block, lid and floor
on the staggered grid, samples the board modes at the board columns and writes the files read by grand_fdtd.

World frame (m): X to the treble side (spine/bass edge of the board at X = 0), Y away from the player
(front edge of the case at Y = 0, board front edge at Y = 0.40), Z up from the floor.
Assumptions (all nominal, README 'Sources and assumptions'): board plane 0.625 m above the floor, rim 0.10 m
thick and up to 0.975 m, lid hinged on the spine (bass) side at the top of the rim, full stick 35 deg, half stick
12 deg, closed 0 deg; the cast-iron plate, strings, keybed, legs and the pedal lyre are not modelled."""
import math, sys, json
from pathlib import Path
import numpy as np
from scipy.interpolate import LinearNDInterpolator
from scipy.spatial import cKDTree
import geometry as geo

DX = 0.025
DT = 1/24000.0
X0, Y0 = -1.0, -1.2
NX, NY, NZ = 216, 160, 104
KB = 25                       # board plane at z = KB*DX = 0.625 m
Y_BOARD = 0.40                # board frame y = 0 sits at world Y = 0.40
Z_RIM_TOP = 0.975
RIM_T = 0.10
HINGE_X = -RIM_T
LID_ANGLES = {'open': 35.0, 'half': 12.0, 'closed': 0.0}

def centres():
    X = X0 + (np.arange(NX) + 0.5)*DX; Y = Y0 + (np.arange(NY) + 0.5)*DX; Z = (np.arange(NZ) + 0.5)*DX
    return X, Y, Z

def mic_cell(pos):
    return (int((pos[0]-X0)//DX), int((pos[1]-Y0)//DX), int(pos[2]//DX))

# microphone presets (world metres). Salamander (Yamaha C5) is a close AB pair about 12 cm above the strings.
Z_STRINGS = 0.625 + 0.08
MICS = {
    'closeL': (0.40, 1.00, Z_STRINGS + 0.12), 'closeR': (0.80, 1.00, Z_STRINGS + 0.12),
    'playerL': (0.52, -0.40, 1.15), 'playerR': (0.68, -0.40, 1.15),
    'audL': (3.35, 1.00, 1.25), 'audR': (3.35, 1.40, 1.25),
}
PRESETS = {'close': ('closeL', 'closeR'), 'player': ('playerL', 'playerR'), 'audience': ('audL', 'audR')}

def build(lid, modes_npz, fmax_couple, outdir):
    outdir = Path(outdir); outdir.mkdir(parents=True, exist_ok=True)
    X, Y, Z = centres()
    XX, YY = np.meshgrid(X, Y, indexing='ij')          # (NX, NY)
    bx, by = XX, YY - Y_BOARD
    inside = geo.inside(bx, by, 0.0)
    # distance to the outline for cells outside
    ol = geo.outline(0.004); tree = cKDTree(ol)
    dist, _ = tree.query(np.stack([bx.ravel(), by.ravel()], 1)); dist = dist.reshape(bx.shape)
    near = (~inside) & (dist < RIM_T)
    # front: rim ring around the front edge continues (y < 0); front block between the case front and the board
    front_block = (by < 0) & (by >= -Y_BOARD - 0.0) & (bx >= -RIM_T) & (bx <= geo.RIGHT_X[0] + RIM_T)
    solid = np.zeros((NX, NY, NZ), bool)
    k_lo = KB - 2                                       # rim reaches 0.05 m below the board plane
    k_top = int(round(Z_RIM_TOP/DX))                    # exclusive upper layer index
    solid[:, :, k_lo:k_top] |= (near | (front_block & ~inside))[:, :, None]
    # lid
    ang = math.radians(LID_ANGLES[lid])
    ca, sa = math.cos(ang), math.sin(ang)
    Xc, Yc, Zc = np.meshgrid(X, Y, Z, indexing='ij')
    u = (Xc - HINGE_X)*ca + (Zc - Z_RIM_TOP)*sa           # along the panel
    nd = -(Xc - HINGE_X)*sa + (Zc - Z_RIM_TOP)*ca          # normal distance
    yb = Yc - Y_BOARD
    width = geo.right_edge(np.clip(yb, 0, geo.LEN_Y)) + RIM_T - HINGE_X
    lid_y = (Yc >= 0.35) & (Yc <= Y_BOARD + geo.LEN_Y + RIM_T)
    half = 0.5*DX*(abs(sa) + abs(ca)) if lid != 'closed' else 0.5*DX
    lidmask = (np.abs(nd) <= half) & (u >= -1e-9) & (u <= width) & lid_y
    if lid == 'closed':
        lidmask = (Zc >= Z_RIM_TOP) & (Zc < Z_RIM_TOP + DX) & lid_y & (Xc >= HINGE_X) & (Xc <= HINGE_X + width)
    solid |= lidmask
    # front block (pin block, fall board, music desk): solid up to the rim top
    solid[:, :, k_lo:k_top] |= (front_block & (by < 0))[:, :, None] 
    # board columns
    col = np.argwhere(inside)
    # remove columns that would sit in solid cells (none expected)
    for (i, j) in col:
        assert not solid[i, j, KB-1] and not solid[i, j, KB]
    d = np.load(modes_npz)
    f = d['f']; ncoup = int((f <= fmax_couple).sum())
    interp = LinearNDInterpolator(d['nodes'], d['W'][:, :ncoup].astype(np.float64))
    cx = XX[col[:, 0], col[:, 1]]; cy = YY[col[:, 0], col[:, 1]] - Y_BOARD
    Phi = interp(np.stack([cx, cy], 1)); Phi = np.nan_to_num(Phi, nan=0.0).astype(np.float32)
    omega = 2*np.pi*f[:ncoup]; sigma = 0.5*0.02*omega     # structural loss factor 0.02, as the MVP
    # kept solid in (k, j, i) order for C++: index (k*ny + j)*nx + i
    sol_c = np.ascontiguousarray(np.transpose(solid, (2, 1, 0))).astype(np.uint8)
    (outdir/'solid.u8').write_bytes(sol_c.tobytes())
    (outdir/'columns.i32').write_bytes(col.astype(np.int32).tobytes())
    (outdir/'phi.f32').write_bytes(Phi.tobytes())
    (outdir/'modes.f64').write_bytes(np.concatenate([omega, sigma]).astype(np.float64).tobytes())
    (outdir/'scene.txt').write_text(f"nx={NX}\nny={NY}\nnz={NZ}\nkb={KB}\ndx={DX}\ndt={DT!r}\nncols={len(col)}\nnmodes={ncoup}\nsponge=0.5\nlid={lid}\n")
    info = dict(lid=lid, columns=int(len(col)), board_area=float(len(col)*DX*DX), modes_coupled=ncoup, solid_cells=int(solid.sum()))
    (outdir/'info.json').write_text(json.dumps(info))
    return info

if __name__ == '__main__':
    lid, modes, fmax, out = sys.argv[1], sys.argv[2], float(sys.argv[3]), sys.argv[4]
    print(build(lid, modes, fmax, out))

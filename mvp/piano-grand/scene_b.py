# SPDX-License-Identifier: MIT
"""Piano 0.7b scene builder for the near-field FDTD: the 0.7a scene (rim, front block, lid, floor, board columns) on a choice of
grids, plus the cast-iron plate. World frame as in scene.py (X to the treble, Y away from the player, Z up).

Grids
  coarse: dx 25 mm, dt 1/24000, the whole 5.4 x 4.0 x 2.6 m domain of 0.7a (all microphones; trusted below about 1 kHz).
  fine:   dx 12.5 mm, dt 1/48000, a sub-domain around the piano (2.65 x 3.1 x 2.4 m, absorbing layer 0.5 m inside it); close
          and player microphones only. Grid dispersion is 4 times smaller than the coarse grid at the same frequency.
Iron plate (NOMINAL, not measured): a 25 mm thick rigid sheet whose underside is 0.030 m above the board plane (top 0.680 m; the strings, 0.705 m, are 25 mm above it), covering the board outline
  except the windows. Windows = the area inside the outline shrunk by 0.08 m, minus four cross struts (centre lines y = 0.27, 0.62, 0.97,
  1.32 m in board coordinates, 0.07 m wide). Open area about 62 % of the board area. The strings are 0.08 m above the board (0.705 m).
"""
import math, json
from dataclasses import dataclass
from pathlib import Path
import numpy as np
from scipy.interpolate import LinearNDInterpolator
from scipy.spatial import cKDTree
import geometry as geo

C0 = 343.0

@dataclass(frozen=True)
class Grid:
    name: str
    dx: float
    dt: float
    X0: float
    Y0: float
    NX: int
    NY: int
    NZ: int
    KB: int            # board plane at z = KB*dx = 0.625 m

COARSE = Grid('coarse', 0.025, 1/24000.0, -1.0, -1.2, 216, 160, 104, 25)
FINE = Grid('fine', 0.0125, 1/48000.0, -0.7, -0.6, 212, 248, 192, 50)
GRIDS = {'coarse': COARSE, 'fine': FINE}

Y_BOARD = 0.40
Z_BOARD = 0.625
Z_RIM_TOP = 0.975
RIM_T = 0.10
HINGE_X = -RIM_T
LID_ANGLES = {'open': 35.0, 'half': 12.0, 'closed': 0.0}
PLATE_Z0, PLATE_T = Z_BOARD + 0.030, 0.025          # plate underside and thickness (m); top at 0.680, strings at 0.705
PLATE_STRUTS_Y, PLATE_STRUT_W, PLATE_RIM_BAND = (0.27, 0.62, 0.97, 1.32), 0.07, 0.08
Z_STRINGS = Z_BOARD + 0.08

MICS = {
    'closeL': (0.40, 1.00, Z_STRINGS + 0.12), 'closeR': (0.80, 1.00, Z_STRINGS + 0.12),
    'playerL': (0.52, -0.40, 1.15), 'playerR': (0.68, -0.40, 1.15),
    'audL': (3.35, 1.00, 1.25), 'audR': (3.35, 1.40, 1.25),
}
PRESETS = {'close': ('closeL', 'closeR'), 'player': ('playerL', 'playerR'), 'audience': ('audL', 'audR')}

def plate_open(bx, by):
    """True at board-frame points where the plate plane is open (a window) inside the board outline."""
    ins = geo.inside(bx, by, PLATE_RIM_BAND)
    strut = np.zeros(np.shape(bx), bool)
    for yc in PLATE_STRUTS_Y: strut |= np.abs(by - yc) < PLATE_STRUT_W/2
    return ins & ~strut

def plate_solid(bx, by):
    return geo.inside(bx, by, 0.0) & ~plate_open(bx, by)

def mic_cell(g, pos):
    return (int((pos[0]-g.X0)//g.dx), int((pos[1]-g.Y0)//g.dx), int(pos[2]//g.dx))

def in_grid(g, pos):
    c = mic_cell(g, pos)
    return 0 <= c[0] < g.NX and 0 <= c[1] < g.NY and 0 <= c[2] < g.NZ

def solid_mask(g, lid, plate=True):
    """Solid cells as labels (uint8, 0 = air; 1 rim / front block, 2 lid, 3 iron plate), array (NX, NY, NZ)."""
    DX = g.dx
    X = g.X0 + (np.arange(g.NX) + 0.5)*DX; Y = g.Y0 + (np.arange(g.NY) + 0.5)*DX; Z = (np.arange(g.NZ) + 0.5)*DX
    XX, YY = np.meshgrid(X, Y, indexing='ij'); bx, by = XX, YY - Y_BOARD
    inside = geo.inside(bx, by, 0.0)
    ol = geo.outline(0.004); tree = cKDTree(ol)
    dist, _ = tree.query(np.stack([bx.ravel(), by.ravel()], 1)); dist = dist.reshape(bx.shape)
    near = (~inside) & (dist < RIM_T)
    front_block = (by < 0) & (by >= -Y_BOARD) & (bx >= -RIM_T) & (bx <= geo.RIGHT_X[0] + RIM_T)
    solid = np.zeros((g.NX, g.NY, g.NZ), np.uint8)       # labels: 1 rim / front block, 2 lid, 3 iron plate
    k_lo = g.KB - int(round(0.05/DX)); k_top = int(round(Z_RIM_TOP/DX))
    solid[:, :, k_lo:k_top][(near | (front_block & ~inside))] = 1
    ang = math.radians(LID_ANGLES[lid]); ca, sa = math.cos(ang), math.sin(ang)
    Xc, Yc, Zc = np.meshgrid(X, Y, Z, indexing='ij')
    u = (Xc - HINGE_X)*ca + (Zc - Z_RIM_TOP)*sa
    nd = -(Xc - HINGE_X)*sa + (Zc - Z_RIM_TOP)*ca
    width = geo.right_edge(np.clip(Yc - Y_BOARD, 0, geo.LEN_Y)) + RIM_T - HINGE_X
    lid_y = (Yc >= 0.35) & (Yc <= Y_BOARD + geo.LEN_Y + RIM_T)
    half = 0.5*DX*(abs(sa) + abs(ca)) if lid != 'closed' else 0.5*DX
    lidmask = (np.abs(nd) <= half) & (u >= -1e-9) & (u <= width) & lid_y
    if lid == 'closed':
        lidmask = (Zc >= Z_RIM_TOP) & (Zc < Z_RIM_TOP + DX) & lid_y & (Xc >= HINGE_X) & (Xc <= HINGE_X + width)
    solid[lidmask] = 2
    solid[:, :, k_lo:k_top][front_block & (by < 0)] = 1
    del Xc, Yc, Zc, u, nd, width, lid_y, lidmask
    if plate:
        kp0 = int(round(PLATE_Z0/DX)); kp1 = kp0 + int(round(PLATE_T/DX))
        solid[:, :, kp0:kp1][plate_solid(bx, by)] = 3
    return solid, inside, XX, YY

def build(lid, modes_npz, fmax_couple, outdir, grid='coarse', plate=True):
    g = GRIDS[grid]; outdir = Path(outdir); outdir.mkdir(parents=True, exist_ok=True)
    solid, inside, XX, YY = solid_mask(g, lid, plate); solid = solid > 0
    col = np.argwhere(inside)
    for (i, j) in col:
        assert not solid[i, j, g.KB-1] and not solid[i, j, g.KB]
    d = np.load(modes_npz); f = d['f']; ncoup = int((f <= fmax_couple).sum())
    interp = LinearNDInterpolator(d['nodes'], d['W'][:, :ncoup].astype(np.float64))
    cx = XX[col[:, 0], col[:, 1]]; cy = YY[col[:, 0], col[:, 1]] - Y_BOARD
    Phi = np.nan_to_num(interp(np.stack([cx, cy], 1)), nan=0.0).astype(np.float32)
    omega = 2*np.pi*f[:ncoup]; sigma = 0.5*0.02*omega
    sol_c = np.ascontiguousarray(np.transpose(solid, (2, 1, 0))).astype(np.uint8)
    (outdir/'solid.u8').write_bytes(sol_c.tobytes())
    (outdir/'columns.i32').write_bytes(col.astype(np.int32).tobytes())
    (outdir/'phi.f32').write_bytes(Phi.tobytes())
    (outdir/'modes.f64').write_bytes(np.concatenate([omega, sigma]).astype(np.float64).tobytes())
    (outdir/'scene.txt').write_text(f"nx={g.NX}\nny={g.NY}\nnz={g.NZ}\nkb={g.KB}\ndx={g.dx}\ndt={g.dt!r}\nncols={len(col)}\nnmodes={ncoup}\nsponge=0.5\nlid={lid}\nplate={int(plate)}\ngrid={grid}\n")
    info = dict(lid=lid, grid=grid, plate=plate, columns=int(len(col)), board_area=float(len(col)*g.dx**2), modes_coupled=ncoup, solid_cells=int(solid.sum()),
                plate_open_fraction=float(plate_open(XX[inside], YY[inside] - Y_BOARD).mean()))
    (outdir/'info.json').write_text(json.dumps(info))
    return info

if __name__ == '__main__':
    import sys
    lid, modes, fmax, out, grid, plate = sys.argv[1], sys.argv[2], float(sys.argv[3]), sys.argv[4], sys.argv[5], bool(int(sys.argv[6]))
    print(build(lid, modes, fmax, out, grid, plate))

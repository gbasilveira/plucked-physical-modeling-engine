# SPDX-License-Identifier: MIT
"""Grand-piano soundboard geometry for Piano 0.7a (about 200 cm class, Yamaha C5 sized).

Board frame (metres): x runs bass (spine, x = 0) to treble, y runs from the front edge of the
board (pin-block side, y = 0) towards the tail. The grain of the spruce runs along y (the long
axis), the ribs run across it (along x). Every number here is a nominal assumption taken from
published general dimensions of 2 m grands; none is measured on a Yamaha C5. See README.md
("Sources and assumptions") for what each value is based on and how sure we are of it.
"""
import math
import numpy as np

# ---------------------------------------------------------------- outline ------------------
# Bent (treble) side as x_r(y): widest near the front, closing towards the tail. Control points
# are interpolated with a monotone cubic (no overshoot). Board length 1.50 m, area about 1.49 m^2
# (a 2 m grand has a soundboard of roughly 1.4 to 1.6 m^2; the board starts about 0.4 m behind the
# front edge of the case because of the pin block).
LEN_Y = 1.50
RIGHT_Y = np.array([0.0, 0.15, 0.45, 0.80, 1.15, 1.40, 1.50])
RIGHT_X = np.array([1.22, 1.28, 1.22, 1.02, 0.78, 0.55, 0.45])
FRONT_Y_IN_CASE = 0.40     # board frame y = 0 sits this far behind the front edge of the case

def _pchip_coeffs(x, y):
    h = np.diff(x); d = np.diff(y) / h
    m = np.zeros_like(y)
    for i in range(1, len(y) - 1):
        if d[i-1] * d[i] > 0:
            w1, w2 = 2*h[i] + h[i-1], h[i] + 2*h[i-1]
            m[i] = (w1 + w2) / (w1/d[i-1] + w2/d[i])
    m[0] = ((2*h[0] + h[1])*d[0] - h[0]*d[1]) / (h[0] + h[1])
    m[-1] = ((2*h[-1] + h[-2])*d[-1] - h[-1]*d[-2]) / (h[-1] + h[-2])
    return m

_M = _pchip_coeffs(RIGHT_Y, RIGHT_X)

def right_edge(y):
    """x of the bent side at height y (vectorised, monotone cubic Hermite)."""
    y = np.asarray(y, float)
    i = np.clip(np.searchsorted(RIGHT_Y, y) - 1, 0, len(RIGHT_Y) - 2)
    h = RIGHT_Y[i+1] - RIGHT_Y[i]; t = (y - RIGHT_Y[i]) / h
    h00 = 2*t**3 - 3*t**2 + 1; h10 = t**3 - 2*t**2 + t; h01 = -2*t**3 + 3*t**2; h11 = t**3 - t**2
    return h00*RIGHT_X[i] + h10*h*_M[i] + h01*RIGHT_X[i+1] + h11*h*_M[i+1]

def outline(spacing):
    """Closed polygon (counter-clockwise), boundary points about `spacing` apart. Returns (N,2)."""
    pts = []
    def seg(p, q):
        n = max(1, int(round(math.dist(p, q) / spacing)))
        for k in range(n):
            pts.append((p[0] + (q[0]-p[0])*k/n, p[1] + (q[1]-p[1])*k/n))
    yy = np.linspace(0, LEN_Y, 4001)
    xx = right_edge(yy)
    arc = np.concatenate([[0], np.cumsum(np.hypot(np.diff(xx), np.diff(yy)))])
    n = max(1, int(round(arc[-1] / spacing)))
    s = np.linspace(0, arc[-1], n + 1)
    cy = np.interp(s, arc, yy); cx = right_edge(cy)
    seg((0.0, 0.0), (RIGHT_X[0], 0.0))                       # front edge
    pts.extend(zip(cx[:-1], cy[:-1]))                         # bent side, front to tail
    seg((RIGHT_X[-1], LEN_Y), (0.0, LEN_Y))                   # tail edge
    seg((0.0, LEN_Y), (0.0, 0.0))                             # spine (straight side)
    return np.array(pts)

def inside(px, py, margin=0.0):
    """True inside the board (margin > 0 shrinks it)."""
    px = np.asarray(px, float); py = np.asarray(py, float)
    ok = (py >= margin) & (py <= LEN_Y - margin) & (px >= margin)
    r = right_edge(np.clip(py, 0, LEN_Y))
    # distance to the bent side is approximated by the horizontal gap times cos of the local slope
    dy = 1e-4
    slope = (right_edge(np.clip(py + dy, 0, LEN_Y)) - right_edge(np.clip(py - dy, 0, LEN_Y))) / (2*dy)
    gap = (r - px) / np.sqrt(1 + slope**2)
    return ok & (gap >= margin)

def area():
    yy = np.linspace(0, LEN_Y, 20001)
    return float(np.trapezoid(right_edge(yy), yy))

# ---------------------------------------------------------------- thickness ----------------
H_CENTRE, H_EDGE, EDGE_BAND = 0.0090, 0.0065, 0.25   # m: raised-cosine taper over the outer 0.25 m

def edge_distance(x, y):
    x = np.asarray(x, float); y = np.asarray(y, float)
    dy = 1e-4
    r = right_edge(np.clip(y, 0, LEN_Y))
    slope = (right_edge(np.clip(y + dy, 0, LEN_Y)) - right_edge(np.clip(y - dy, 0, LEN_Y))) / (2*dy)
    d_bent = (r - x) / np.sqrt(1 + slope**2)
    return np.minimum.reduce([x, y, LEN_Y - y, d_bent])

def thickness(x, y):
    d = np.clip(edge_distance(x, y) / EDGE_BAND, 0, 1)
    s = 0.5 - 0.5*np.cos(np.pi*d)
    return H_EDGE + (H_CENTRE - H_EDGE) * s

# ---------------------------------------------------------------- bridges ------------------
def bezier(p0, p1, p2, n=400):
    t = np.linspace(0, 1, n)[:, None]
    p0, p1, p2 = map(np.asarray, (p0, p1, p2))
    return (1-t)**2*p0 + 2*(1-t)*t*p1 + t**2*p2

# Long (treble) bridge: from the bass end of the treble section (key 45) to the highest treble (key 108),
# slightly concave towards the tail. Bass bridge: near the spine, keys 21..44. Nominal end points: the
# high-treble strings are the shortest and sit nearest the front, the bass strings are the longest.
TREBLE_BRIDGE = bezier((0.50, 1.10), (0.70, 0.52), (1.10, 0.14))
BASS_BRIDGE = bezier((0.13, 1.33), (0.18, 1.05), (0.40, 0.95))
FIRST_BASS_KEY, LAST_BASS_KEY, LAST_KEY = 21, 44, 108

def _arclength(path):
    return np.concatenate([[0], np.cumsum(np.hypot(*np.diff(path, axis=0).T))])

def key_points():
    """Per-key string attachment point on its bridge. Returns dict arrays: key, x, y, bridge, s (arc, m).

    Keys are spaced evenly in arc length along their bridge with half a pitch of margin at each end
    (3-string unisons in the treble are about 17 mm apart on this bridge, 20 mm in the bass).
    """
    keys, xs, ys, br, ss = [], [], [], [], []
    for (path, k0, k1, b) in ((BASS_BRIDGE, FIRST_BASS_KEY, LAST_BASS_KEY, 0),
                              (TREBLE_BRIDGE, LAST_BASS_KEY + 1, LAST_KEY, 1)):
        arc = _arclength(path); n = k1 - k0 + 1
        pitch = arc[-1] / n
        for j, key in enumerate(range(k0, k1 + 1)):
            s = pitch * (j + 0.5) if b == 0 else arc[-1] - pitch * (j + 0.5)   # treble: key 45 at the bass end (arc 0 end is key 45)
            if b == 1: s = pitch * (j + 0.5)
            keys.append(key); xs.append(np.interp(s, arc, path[:, 0])); ys.append(np.interp(s, arc, path[:, 1]))
            br.append(b); ss.append(s)
    return dict(key=np.array(keys), x=np.array(xs), y=np.array(ys), bridge=np.array(br), s=np.array(ss))

# ---------------------------------------------------------------- ribs ---------------------
RIB_Y = 0.12 + 0.115 * np.arange(12)       # 12 ribs across the grain, 115 mm apart
RIB_WIDTH, RIB_HEIGHT = 0.022, 0.030        # m, mean section (spruce, grain along the rib)
RIB_TAPER = 0.10                            # m at each end over which the rib height falls to 40 %
RIB_END_MARGIN = 0.02                       # ribs stop 20 mm short of the rim

def rib_paths(n_per_m=400):
    paths = []
    for y in RIB_Y:
        x1 = float(right_edge(y)) - RIB_END_MARGIN
        n = max(8, int(x1 * n_per_m))
        paths.append(np.stack([np.linspace(RIB_END_MARGIN, x1, n), np.full(n, y)], axis=1))
    return paths

def rib_height_factor(s, length):
    d = np.minimum(s, length - s)
    return np.where(d >= RIB_TAPER, 1.0, 0.4 + 0.6 * np.clip(d / RIB_TAPER, 0, 1))

# Bridge sections (hard maple, cap included): width 28 mm, height 45 mm.
BRIDGE_WIDTH, BRIDGE_HEIGHT = 0.028, 0.045

if __name__ == '__main__':
    print('area m^2', area())
    kp = key_points()
    print('treble length', _arclength(TREBLE_BRIDGE)[-1], 'bass length', _arclength(BASS_BRIDGE)[-1])
    print('min margin of bridge points', float(edge_distance(kp['x'], kp['y']).min()))
    print('h range', float(thickness(0.6, 0.75)), float(thickness(0.02, 0.02)))

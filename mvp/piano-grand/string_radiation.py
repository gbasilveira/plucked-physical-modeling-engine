# SPDX-License-Identifier: MIT
"""Piano 0.7b stage 2a: direct sound radiation of the vibrating strings (line dipoles) to the microphones.

Physics. A thin cylinder (radius a) moving transversally with velocity v(x, t) in air exerts on the fluid, per unit length, the added-mass
force  f' = rho0 pi a^2 dv/dt  (2-D potential flow around a cylinder, ka << 1; the string's own inertia is in the string model). A force on the
fluid radiates as a dipole:  p(r) = F (e . rhat) (1 + j k r) exp(-j k r) / (4 pi r^2)  (exp(+j w t) convention). A mode n of a string of length L
with unit modal velocity has v(x) = phi_n(x) = sqrt(2/L) sin(n pi x / L) (x = L at the bridge; engine/src/string/string_model.cpp). The
gain per unit analytic modal velocity,  G_n,m = sum_x' f'_n(x') dipole(x', mic m),  is evaluated at the partial's own frequency (the partial is
narrow-band): p(t) = Re[G (u/dt + j omega (q - u/2))] is evaluated by grand_render for every struck string, both polarisations
(vertical e = z, horizontal e = in the horizontal plane across the string).
Image. The reflecting surface under a string element is the iron plate (top at Z_PLATE_TOP) where the plate is solid and the board plane
otherwise; a rigid plane mirrors the tangential force component and reverses the normal one. (The board is taken as rigid for this term: its own
vibration is radiated by the board model.) The lid, rim and diffraction are not applied to this path; the three strings of a unison are
offset across the string by 9.5 mm (2 strings: +-4.75 mm).
Geometry (nominal assumption): each string runs from its bridge point towards the front, straight, unless it is longer than the case
allows (front line at board y = -0.35 m), in which case it is inclined towards the treble side just enough to end on that line (overstrung bass).
Delay. G is stored relative to a reference distance (the mic's distance to the centroid of the strings, rounded to a whole number of samples
at 48 kHz); the renderer's output is delayed by that many samples in Python.
Usage: string_radiation.py STRINGS.bin OUT.bin [--no-plate]
"""
import math, struct, sys
import numpy as np
import geometry as geo, scene_b as S

RHO0, C = 1.2, 343.0
UNISON_PITCH = 0.0095
FRONT_LINE = -0.35
FS = 48000.0
Z_PLATE_TOP = S.PLATE_Z0 + S.PLATE_T

def read_strings(path):
    b = open(path, 'rb').read(); p = 0; keys = {}
    for key in range(21, 109):
        cnt = struct.unpack_from('<i', b, p)[0]; p += 4; ss = []
        for j in range(cnt):
            L, d, rhoA, T = struct.unpack_from('<4d', b, p); p += 32; n = struct.unpack_from('<i', b, p)[0]; p += 4
            om = np.frombuffer(b, '<f8', n, p).copy(); p += 8*n
            ss.append(dict(L=L, d=d, rhoA=rhoA, T=T, omega=om))
        keys[key] = ss
    return keys

def string_axis(key, L, kp):
    """Bridge point (board frame), unit direction (board frame) towards the string's far end."""
    i = key - 21; xb, yb = float(kp['x'][i]), float(kp['y'][i])
    c = min(1.0, (yb - FRONT_LINE)/L); beta = math.acos(c) if c < 1.0 else 0.0
    return np.array([xb, yb]), np.array([math.sin(beta), -math.cos(beta)])

def dipole_pressure(P, e, k, mic, zref=None):
    """Pressure at `mic` (3,) per unit force e (3,) on the fluid at points P (n,3), exp(+jwt), optional rigid-plane image at z = zref (array (n,) or float)."""
    out = np.zeros(len(P), complex)
    def one(Q, ee):
        d = mic[None, :] - Q; r = np.linalg.norm(d, axis=1)
        return (d @ ee)/r*(1 + 1j*k*r)*np.exp(-1j*k*r)/(4*np.pi*r**2)
    out += one(P, e)
    if zref is not None:
        Q = P.copy(); Q[:, 2] = 2*zref - P[:, 2]
        ei = np.array(e, float); ei[2] = -ei[2]
        out += one(Q, ei)
    return out

def line_gain(P, ds, xi_phi, f, e, mic, zref, a):
    """G for one mode: sum over line elements of  rho0 pi a^2 (j w) phi(x') dipole(x') ds ."""
    w = 2*math.pi*f; k = w/C
    return RHO0*math.pi*a*a*1j*w*np.sum(xi_phi*dipole_pressure(P, e, k, mic, zref))*ds

def ref_samples(mic, centre):
    return int(round(np.linalg.norm(np.asarray(mic) - centre)/C*FS))

def build(strings_bin, out_bin, mics, plate=True, verbose=True):
    keys = read_strings(strings_bin); kp = geo.key_points()
    mic_pos = [np.asarray(S.MICS[m], float) for m in mics]
    centre = np.array([0.6, 1.1 + S.Y_BOARD - 0.4, S.Z_STRINGS])      # middle of the string field (world)
    refs = [ref_samples(m, centre) for m in mic_pos]
    with open(out_bin, 'wb') as f:
        f.write(b'SRAD'); f.write(struct.pack('<i', len(mics)))
        for key in range(21, 109):
            ss = keys[key]; f.write(struct.pack('<i', len(ss)))
            for s in ss: f.write(struct.pack('<i', len(s['omega'])))
            for js, s in enumerate(ss):
                L = s['L']; a = 0.5*s['d']; om = s['omega']; n = len(om)
                b0, dirv = string_axis(key, L, kp)
                lat = (js - (len(ss) - 1)/2)*UNISON_PITCH
                perp = np.array([-dirv[1], dirv[0]])                    # across the string in the horizontal plane (board frame)
                G = np.zeros((2, n, len(mics)), complex)
                for i, wn in enumerate(om):
                    fn = wn/(2*math.pi); nq = int(12*(i + 1) + 64); ds = L/nq
                    xi = (np.arange(nq) + 0.5)*ds                          # distance from the bridge end
                    x_str = L - xi                                          # position along the string (x = L at the bridge)
                    phi = math.sqrt(2/L)*np.sin((i + 1)*math.pi*x_str/L)
                    bx = b0[0] + dirv[0]*xi + perp[0]*lat; by = b0[1] + dirv[1]*xi + perp[1]*lat
                    P = np.stack([bx, by + S.Y_BOARD, np.full(nq, S.Z_STRINGS)], 1)
                    zref = np.where(S.plate_solid(bx, by), Z_PLATE_TOP, S.Z_BOARD) if plate else S.Z_BOARD
                    ez = np.array([0, 0, 1.0]); eh = np.array([perp[0], perp[1], 0.0])
                    for mi, m in enumerate(mic_pos):
                        rr = refs[mi]/FS*C
                        for pol, e in enumerate((ez, eh)):
                            G[pol, i, mi] = line_gain(P, ds, phi, fn, e, m, zref, a)*np.exp(1j*2*math.pi*fn*rr/C)
                f.write(np.stack([G.real, G.imag], axis=-1).astype('<f4').tobytes())
            if verbose and key % 12 == 0: print('string table key', key, flush=True)
    return refs

if __name__ == '__main__':
    mics = list(S.MICS)
    refs = build(sys.argv[1], sys.argv[2], mics, plate='--no-plate' not in sys.argv)
    print('reference delays (samples)', dict(zip(mics, refs)))

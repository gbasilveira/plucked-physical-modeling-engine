# SPDX-License-Identifier: MIT
"""Reissner-Mindlin orthotropic plate FEM with embedded Timoshenko beams (ribs, bridges).

Element: 3-node triangle, unknowns (w, phi_x, phi_y) per node (phi = slope-like rotation, shear
strain gamma = grad w - phi). Bending: constant curvature of the linear phi. Shear: constant shear
strain from the three edge shear gaps (discrete-shear-gap idea, DSG3; least squares over the three
edges so the element does not depend on node numbering), which avoids shear locking.
Ribs and bridges are beams that need not follow mesh edges: the beam is cut into short pieces and
each piece uses the strain field of the element that contains it (T-section bending stiffness about
the composite neutral axis, Timoshenko shear, torsion, line mass, eccentric rotary inertia).
// SPEC-N/A: this module is MVP-only (offline tool); formulas are documented in README.md.
"""
import math, time
import numpy as np
import scipy.sparse as sp
import scipy.sparse.linalg as sla
from scipy.spatial import Delaunay, cKDTree
import geometry as geo

# ---- material: Chabassier & Durufle RT-0425 spruce (as the MVP) + wood-handbook shear ratios --
EL, ER, GLR, NU_LR = 11.6e9, 0.9e9, 0.75e9, 0.37     # grain direction L = y, across grain R = x
RHO = 390.0
G_LT, G_RT = 0.71e9, 0.035e9   # through-thickness shear (Wood Handbook ratios 0.061, 0.003 of E_L): L-z (y-z), R-z (x-z)
KS = 5.0/6.0
SHEAR_SCALE = 1.0     # >> 1 gives the Kirchhoff (thin-plate) limit with the same element
LOSS_FACTOR = 0.02            # as MVP 3: sigma = loss_factor * omega / 2

def plane_stress_Q():
    ex, ey = ER, EL                         # x across the grain, y along it
    nuyx = NU_LR; nuxy = nuyx*ex/ey
    den = 1 - nuxy*nuyx
    return np.array([[ex/den, nuyx*ex/den, 0], [nuyx*ex/den, ey/den, 0], [0, 0, GLR]])

# ------------------------------------------------------------------ mesh --------------------
def resample(path, h):
    seg = np.hypot(*np.diff(path, axis=0).T); arc = np.concatenate([[0], np.cumsum(seg)])
    n = max(1, int(round(arc[-1]/h)))
    sa = np.linspace(0, arc[-1], n+1)
    return np.stack([np.interp(sa, arc, path[:, 0]), np.interp(sa, arc, path[:, 1])], 1)

def make_mesh(h, beam_paths=()):
    """Delaunay mesh of the board. Points of the beam lines (ribs, bridges) are placed explicitly with
    spacing h and the lattice points closer than 0.6 h to a line are dropped, so that the line is a chain
    of mesh edges (Gabriel edges are always Delaunay edges). Returns p, simplices, boundary flag,
    chains (list of node-index lists, one per beam path)."""
    bnd = geo.outline(h)
    line_pts, line_id = [], []
    taken = list(bnd)
    tree_taken = None
    for bi, path in enumerate(beam_paths):
        q = resample(path, h)
        for pt in q:
            if geo.inside(pt[0], pt[1], 0.45*h):
                line_pts.append(pt); line_id.append(bi)
    lp = np.array(line_pts) if line_pts else np.zeros((0, 2))
    # drop line points that crowd earlier ones (crossings) or the boundary
    keep = np.ones(len(lp), bool)
    if len(lp):
        tb = cKDTree(bnd)
        for i in range(len(lp)):
            if tb.query(lp[i])[0] < 0.7*h: keep[i] = False; continue
            for j in range(i):
                if keep[j] and line_id[j] != line_id[i] and np.hypot(*(lp[i]-lp[j])) < 0.7*h: keep[i] = False; break
        lp = lp[keep]; line_id = [l for l, k in zip(line_id, keep) if k]
    dy = h*math.sqrt(3)/2
    pts = []
    j = 0; y = 0.0
    while y <= geo.LEN_Y + 1e-9:
        x0 = 0.5*h if (j % 2) else 0.0
        xs = np.arange(x0, 1.5, h)
        pts.append(np.stack([xs, np.full_like(xs, y)], axis=1))
        y += dy; j += 1
    p = np.concatenate(pts)
    ok = geo.inside(p[:, 0], p[:, 1], 0.6*h)
    p = p[ok]
    if len(lp):
        dist = cKDTree(lp).query(p)[0]
        p = p[dist > 0.6*h]
    nb = len(bnd); nl = len(lp)
    p = np.concatenate([bnd, lp, p])
    tri = Delaunay(p)
    s = tri.simplices
    c = p[s].mean(axis=1)
    s = s[geo.inside(c[:, 0], c[:, 1], -1e-9)]
    a = 0.5*np.abs((p[s[:,1],0]-p[s[:,0],0])*(p[s[:,2],1]-p[s[:,0],1])-(p[s[:,2],0]-p[s[:,0],0])*(p[s[:,1],1]-p[s[:,0],1]))
    s = s[a > 1e-4*h*h]
    boundary = np.zeros(len(p), bool); boundary[:nb] = True
    # beam chains: consecutive line points of one beam, joined by shortest mesh paths when not adjacent
    edges = set()
    for tri_ in s:
        for u, v in ((0, 1), (1, 2), (2, 0)):
            a_, b_ = int(tri_[u]), int(tri_[v]); edges.add((min(a_, b_), max(a_, b_)))
    from scipy.sparse.csgraph import dijkstra
    ea = np.array(sorted(edges)); w = np.hypot(*(p[ea[:, 0]] - p[ea[:, 1]]).T)
    G = sp.coo_matrix((np.concatenate([w, w]), (np.concatenate([ea[:, 0], ea[:, 1]]), np.concatenate([ea[:, 1], ea[:, 0]]))), shape=(len(p), len(p))).tocsr()
    chains = []
    for bi in range(len(beam_paths)):
        idx = [nb + i for i in range(nl) if line_id[i] == bi]
        chain = idx[:1]
        for a_, b_ in zip(idx[:-1], idx[1:]):
            if (min(a_, b_), max(a_, b_)) in edges:
                chain.append(b_)
            else:   # missing edge (near a crossing): follow the shortest mesh path
                dist, pred = dijkstra(G, indices=a_, return_predecessors=True)
                path_ = [b_]
                while path_[-1] != a_ and pred[path_[-1]] >= 0: path_.append(int(pred[path_[-1]]))
                if path_[-1] == a_ and len(path_) <= 6: chain += path_[-2::-1]
                else: chains.append((bi, chain)); chain = [b_]
        if len(chain) > 1: chains.append((bi, chain))
    return p, s.astype(np.int64), boundary, chains

def tri_geometry(p, s):
    x = p[s, 0]; y = p[s, 1]
    A = 0.5*((x[:,1]-x[:,0])*(y[:,2]-y[:,0]) - (x[:,2]-x[:,0])*(y[:,1]-y[:,0]))
    sgn = np.sign(A)
    # make counter-clockwise
    b = np.stack([y[:,1]-y[:,2], y[:,2]-y[:,0], y[:,0]-y[:,1]], axis=1)/(2*A[:,None])
    c = np.stack([x[:,2]-x[:,1], x[:,0]-x[:,2], x[:,1]-x[:,0]], axis=1)/(2*A[:,None])
    return np.abs(A), b, c   # gradients of N_a: (b_a, c_a) (sign-correct for either orientation)

# ------------------------------------------------------------------ element matrices ---------
def shear_B(p, s, gb, gc):
    """B_s (ne,2,9): constant shear strain from edge shear gaps. dof order per node (w, px, py)."""
    ne = len(s)
    x = p[s, 0]; y = p[s, 1]
    edges = ((0, 1), (1, 2), (2, 0))
    Gm = np.zeros((ne, 3, 9)); D = np.zeros((ne, 3, 2))
    for e, (i, j) in enumerate(edges):
        dx = x[:, j]-x[:, i]; dy = y[:, j]-y[:, i]
        D[:, e, 0] = dx; D[:, e, 1] = dy
        Gm[:, e, 3*j] += 1; Gm[:, e, 3*i] -= 1
        Gm[:, e, 3*i+1] -= 0.5*dx; Gm[:, e, 3*j+1] -= 0.5*dx
        Gm[:, e, 3*i+2] -= 0.5*dy; Gm[:, e, 3*j+2] -= 0.5*dy
    DtD = np.einsum('eki,ekj->eij', D, D)
    P = np.linalg.solve(DtD, np.transpose(D, (0, 2, 1)))     # (ne,2,3)
    return np.einsum('eik,ekj->eij', P, Gm)

def bending_B(gb, gc):
    ne = len(gb)
    B = np.zeros((ne, 3, 9))
    for a in range(3):
        B[:, 0, 3*a+1] = gb[:, a]
        B[:, 1, 3*a+2] = gc[:, a]
        B[:, 2, 3*a+1] = gc[:, a]; B[:, 2, 3*a+2] = gb[:, a]
    return B

def plate_matrices(p, s, hth, kind='mindlin', Q=None, rho=RHO, gshear=(G_RT, G_LT), iso=None, lump=True):
    """K, M triplets for all elements. hth: thickness per element. iso=(E,nu) forces isotropic (tests)."""
    ne = len(s)
    A, gb, gc = tri_geometry(p, s)
    if iso is not None:
        E, nu = iso
        Q = np.array([[E/(1-nu**2), nu*E/(1-nu**2), 0], [nu*E/(1-nu**2), E/(1-nu**2), 0], [0, 0, E/(2*(1+nu))]])
        gs = (E/(2*(1+nu)),)*2
    else:
        Q = plane_stress_Q() if Q is None else Q
        gs = gshear
    Bb = bending_B(gb, gc); Bs = shear_B(p, s, gb, gc)
    Db = (hth**3/12)[:, None, None]*Q[None]
    Kb = A[:, None, None]*np.einsum('eki,ekl,elj->eij', Bb, Db, Bb)
    Ds = np.zeros((ne, 2, 2)); Ds[:, 0, 0] = KS*gs[0]*hth; Ds[:, 1, 1] = KS*gs[1]*hth
    Ks = A[:, None, None]*np.einsum('eki,ekl,elj->eij', Bs, Ds, Bs)
    Me = np.zeros((ne, 9, 9))
    # Lumped mass (row sums of the consistent matrix): measured 2.7x smaller dispersion error than the
    # consistent mass for this element (fem_verify.py), so the default is lumped.
    pat = np.eye(3)/3.0 if lump else (np.ones((3, 3)) + np.eye(3))/12.0
    for a in range(3):
        for b in range(3):
            Me[:, 3*a, 3*b] = rho*hth*A*pat[a, b]
            Me[:, 3*a+1, 3*b+1] = rho*hth**3/12*A*pat[a, b]
            Me[:, 3*a+2, 3*b+2] = rho*hth**3/12*A*pat[a, b]
    return Kb + Ks, Me, Bs

def assemble(s, Ke, Me, nn):
    dof = (3*s[:, :, None] + np.arange(3)[None, None, :]).reshape(len(s), 9)
    I = np.repeat(dof[:, :, None], 9, axis=2).ravel(); J = np.repeat(dof[:, None, :], 9, axis=1).ravel()
    K = sp.coo_matrix((Ke.ravel(), (I, J)), shape=(3*nn, 3*nn)).tocsr()
    M = sp.coo_matrix((Me.ravel(), (I, J)), shape=(3*nn, 3*nn)).tocsr()
    return K, M

# ------------------------------------------------------------------ beams -------------------
def directional_E(theta):
    """In-plane Young modulus of the plate along a direction at angle theta from x (across grain)."""
    ex, ey = ER, EL; nuxy = NU_LR*ex/ey
    c, s_ = np.cos(theta), np.sin(theta)
    inv = c**4/ex + s_**4/ey + s_**2*c**2*(1/GLR - 2*nuxy/ex)
    return 1/inv

def beam_section(kind, hplate, theta, factor):
    """Per-piece section properties (arrays). Returns dEI, GJ, GAs, mline, Irot, ecc."""
    if kind == 'rib':
        b, hr, Er, rho_r, Gr = geo.RIB_WIDTH, geo.RIB_HEIGHT*factor, EL, RHO, GLR
    else:
        b, hr, Er, rho_r, Gr = geo.BRIDGE_WIDTH, geo.BRIDGE_HEIGHT, 12.0e9, 650.0, 1.0e9
    bf = b + 2*hplate                                 # effective flange of the plate
    Ef = directional_E(theta)
    EAf, EAr = Ef*bf*hplate, Er*b*hr
    zc = hplate/2 + hr/2
    zna = EAr*zc/(EAf + EAr)
    EIT = Ef*bf*hplate**3/12 + EAf*zna**2 + Er*b*hr**3/12 + EAr*(zc - zna)**2
    dEI = EIT - Ef*bf*hplate**3/12
    J = b*hr*(b*b + hr*hr)/12*0.5                     # rough rectangular torsion constant (conservative)
    GJ = Gr*J
    GAs = KS*Gr*b*hr*SHEAR_SCALE
    mline = rho_r*b*hr
    Irot = rho_r*(b*hr**3/12)
    return dEI, GJ, GAs, mline, Irot, zc

def add_beams(p, K, M, chains, kinds):
    """Edge Timoshenko beams on mesh-edge chains. chains: list of (beam_id, [nodes]); kinds[beam_id] in
    ('rib', 'bridge'). Returns K, M, total beam mass, number of edges."""
    rows, cols, kv, mv = [], [], [], []
    mass_total = 0.0; nedge = 0
    for kind in ('rib', 'bridge'):
        A_, B_ = [], []
        for bid, ch in chains:
            if kinds[bid] != kind: continue
            A_ += ch[:-1]; B_ += ch[1:]
        if not A_: continue
        a = np.array(A_); b = np.array(B_)
        d = p[b] - p[a]; ln = np.hypot(d[:, 0], d[:, 1]); t = d/ln[:, None]; nrm = np.stack([-t[:, 1], t[:, 0]], 1)
        mid = 0.5*(p[a] + p[b])
        hpl = geo.thickness(mid[:, 0], mid[:, 1]); theta = np.arctan2(t[:, 1], t[:, 0])
        # rib taper: distance of the edge from the rib ends along each chain
        fac = np.ones(len(a))
        if kind == 'rib':
            k0 = 0
            for bid, ch in chains:
                if kinds[bid] != 'rib': continue
                ne = len(ch) - 1
                seg = ln[k0:k0+ne]; cum = np.concatenate([[0], np.cumsum(seg)])
                sm = 0.5*(cum[:-1] + cum[1:])
                fac[k0:k0+ne] = geo.rib_height_factor(sm, cum[-1]); k0 += ne
        sec_ = beam_section(kind, hpl, theta, fac)
        dEI, GJ, GAs, mline, Irot, ecc = [np.broadcast_to(np.asarray(x, float), (len(a),)).copy() for x in sec_]
        m = len(a); Z = np.zeros(m)
        kap = np.stack([Z, -t[:, 0], -t[:, 1], Z, t[:, 0], t[:, 1]], 1)/ln[:, None]
        tor = np.stack([Z, -nrm[:, 0], -nrm[:, 1], Z, nrm[:, 0], nrm[:, 1]], 1)/ln[:, None]
        gam = np.stack([-1/ln, -0.5*t[:, 0], -0.5*t[:, 1], 1/ln, -0.5*t[:, 0], -0.5*t[:, 1]], 1)
        Ke = ln[:, None, None]*(dEI[:, None, None]*kap[:, :, None]*kap[:, None, :] + GJ[:, None, None]*tor[:, :, None]*tor[:, None, :]
                                + GAs[:, None, None]*gam[:, :, None]*gam[:, None, :])
        Me = np.zeros((m, 6, 6))
        for side in (0, 1):
            o = 3*side
            Me[:, o, o] = 0.5*mline*ln
            Me[:, o+1, o+1] = 0.5*ln*(mline*ecc**2 + Irot*t[:, 0]**2)
            Me[:, o+2, o+2] = 0.5*ln*(mline*ecc**2 + Irot*t[:, 1]**2)
            Me[:, o+1, o+2] = Me[:, o+2, o+1] = 0.5*ln*Irot*t[:, 0]*t[:, 1]
        mass_total += float((mline*ln).sum()); nedge += m
        dof = np.concatenate([3*a[:, None] + np.arange(3), 3*b[:, None] + np.arange(3)], axis=1)
        rows.append(np.repeat(dof[:, :, None], 6, 2).ravel()); cols.append(np.repeat(dof[:, None, :], 6, 1).ravel())
        kv.append(Ke.ravel()); mv.append(Me.ravel())
    n3 = K.shape[0]
    I = np.concatenate(rows); J = np.concatenate(cols)
    Kb = sp.coo_matrix((np.concatenate(kv), (I, J)), shape=(n3, n3)).tocsr()
    Mb = sp.coo_matrix((np.concatenate(mv), (I, J)), shape=(n3, n3)).tocsr()
    return K + Kb, M + Mb, mass_total, nedge

# ------------------------------------------------------------------ model --------------------
def build_board(h, with_ribs=True, with_bridges=True, theta_k=None, thickness='variable', shear_scale=1.0):
    global SHEAR_SCALE
    SHEAR_SCALE = shear_scale
    paths, kinds = [], []
    if with_ribs:
        for pth in geo.rib_paths(): paths.append(pth); kinds.append('rib')
    if with_bridges:
        paths += [geo.TREBLE_BRIDGE, geo.BASS_BRIDGE]; kinds += ['bridge', 'bridge']
    p, s, bnd, chains = make_mesh(h, paths)
    nn = len(p)
    c = p[s].mean(axis=1)
    hth = geo.thickness(c[:, 0], c[:, 1]) if thickness == 'variable' else np.full(len(s), 0.0082)
    Ke, Me, _ = plate_matrices(p, s, hth, gshear=(G_RT*shear_scale, G_LT*shear_scale))
    K, M = assemble(s, Ke, Me, nn)
    info = dict(plate_mass=float((RHO*hth*tri_geometry(p, s)[0]).sum()), beam_mass=0.0, beam_edges=0)
    if chains:
        K, M, bm, ne = add_beams(p, K, M, chains, kinds); info['beam_mass'] = bm; info['beam_edges'] = ne
    # beam length actually modelled vs nominal
    nominal = sum(float(np.hypot(*np.diff(pp, axis=0).T).sum()) for pp in paths)
    info['beam_length_modelled'] = float(sum(np.hypot(*(p[np.array(ch[1:])]-p[np.array(ch[:-1])]).T).sum() for _, ch in chains))
    info['beam_length_nominal'] = nominal
    # boundary: w = 0 on the rim; rotation spring k_theta (N m/rad per m of edge); None = clamped
    bi = np.where(bnd)[0]
    if theta_k is None or theta_k >= 1e11:
        fixed = np.concatenate([3*bi, 3*bi+1, 3*bi+2])
    else:
        fixed = 3*bi
        pb = p[bi]
        nxt = np.roll(pb, -1, axis=0); prv = np.roll(pb, 1, axis=0)
        lum = 0.5*(np.hypot(*(nxt-pb).T) + np.hypot(*(pb-prv).T))
        Ks = sp.coo_matrix((np.concatenate([theta_k*lum, theta_k*lum]), (np.concatenate([3*bi+1, 3*bi+2]),)*2), shape=K.shape)
        K = K + Ks
    free = np.setdiff1d(np.arange(3*nn), fixed)
    info.update(nodes=nn, elements=len(s), dofs=int(len(free)))
    return dict(p=p, s=s, bnd=bnd, K=K, M=M, free=free, info=info, hth=hth, chains=chains, kinds=kinds)

def solve_modes(model, nmodes=None, fmax=None, tol=1e-9, ncv_factor=2.0, nmax=1500):
    """Lowest modes by shift-invert Lanczos. Either nmodes, or all modes up to fmax (Hz) in chunks."""
    free = model['free']
    K = model['K'][free][:, free].tocsc(); M = model['M'][free][:, free].tocsc()
    t0 = time.time()
    def run(k, sigma):
        return sla.eigsh(K, k=k, M=M, sigma=sigma, which='LM', tol=tol, ncv=min(int(ncv_factor*k)+20, K.shape[0]-1))
    if nmodes is not None:
        w2, V = run(nmodes, 0.0)
    else:
        # sequential shifts: take 200 modes around each shift, advance past the highest trusted one
        wmax2 = (2*math.pi*fmax)**2
        W, Vs = [], []
        sigma = -1.0
        while True:
            w2, V = run(160, sigma)
            order = np.argsort(w2); w2 = w2[order]; V = V[:, order]
            if W:
                # keep only modes above what we already have (shift-invert returns those nearest to sigma)
                prev = max(W[-1].max(), 0)
            W.append(w2); Vs.append(V)
            # modes returned are the 160 nearest to sigma, spanning [lo, hi]; next shift above hi
            hi = w2.max()
            if hi >= wmax2: break
            sigma = hi*1.0001
            if sum(len(x) for x in W) > nmax: break
        w2 = np.concatenate(W); V = np.concatenate(Vs, axis=1)
        order = np.argsort(w2); w2 = w2[order]; V = V[:, order]
        # drop duplicates from overlapping windows
        keep = np.concatenate([[True], np.diff(w2) > 1e-9*w2[1:]])
        w2 = w2[keep]; V = V[:, keep]
        sel = w2 <= wmax2*1.0; w2 = w2[sel]; V = V[:, sel]
    omega = np.sqrt(np.maximum(w2, 0)); order = np.argsort(omega)
    omega = omega[order]; V = V[:, order]
    full = np.zeros((model['K'].shape[0], len(omega)))
    full[free] = V
    # mass normalise (x^T M x = 1)
    Mm = model['M']
    nrm = np.sqrt(np.einsum('ij,ij->j', full, Mm @ full))
    full /= nrm
    return omega/(2*math.pi), full, time.time() - t0

def deflection(model, full):
    """w at the nodes: (nn, nmodes)."""
    return full[0::3, :]

if __name__ == '__main__':
    import sys
    h = float(sys.argv[1]) if len(sys.argv) > 1 else 0.03
    m = build_board(h)
    print(m['info'])
    f, V, t = solve_modes(m, nmodes=20)
    print('time', t); print(np.round(f, 1))

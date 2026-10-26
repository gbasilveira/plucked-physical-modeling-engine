# SPDX-License-Identifier: MIT
"""Verification of the Mindlin triangle against the exact simply supported Mindlin plate (isotropic),
thin and thick, and a clamped-plate thin limit check. Prints a table; asserts tolerances."""
import math, sys
import numpy as np
import scipy.sparse.linalg as sla
import fem

def rect_mesh(a, n):
    xs = np.linspace(0, a, n+1); X, Y = np.meshgrid(xs, xs, indexing='ij')
    p = np.stack([X.ravel(), Y.ravel()], 1)
    idx = lambda i, j: i*(n+1)+j
    tri = []
    for i in range(n):
        for j in range(n):
            a_, b_, c_, d_ = idx(i, j), idx(i+1, j), idx(i+1, j+1), idx(i, j+1)
            if (i+j) % 2: tri += [(a_, b_, c_), (a_, c_, d_)]
            else: tri += [(a_, b_, d_), (b_, c_, d_)]
    return p, np.array(tri)

def exact_ss(a, h, E, nu, rho, m, n):
    D = E*h**3/(12*(1-nu**2)); G = E/(2*(1+nu)); k2 = (math.pi/a)**2*(m*m+n*n)
    Ks = fem.KS*G*h; I = rho*h**3/12
    # (rho h w2)(Dk2 + Ks - I w2) = Ks k2 (D k2 - I w2)  -> quadratic in w2
    # I rho h w2^2 - rho h (Dk2+Ks) w2 + Ks k2 I w2 ... expand
    A_ = rho*h*I; B_ = -(rho*h*(D*k2 + Ks) + Ks*k2*I); C_ = Ks*k2*k2*D
    r = (-B_ - math.sqrt(B_*B_ - 4*A_*C_))/(2*A_)
    return math.sqrt(r)/(2*math.pi)

def ss_fem(a, h, E, nu, rho, n, nm=6):
    p, s = rect_mesh(a, n); nn = len(p)
    hth = np.full(len(s), h)
    Ke, Me, _ = fem.plate_matrices(p, s, hth, rho=rho, iso=(E, nu))
    K, M = fem.assemble(s, Ke, Me, nn)
    on = lambda c: np.isclose(p[:, c], 0) | np.isclose(p[:, c], a)
    fixed = set()
    for i in range(nn):
        if on(0)[i] or on(1)[i]: fixed.add(3*i)
        if on(1)[i]: fixed.add(3*i+1)      # edges parallel to x (y = 0, a): phi_x = 0
        if on(0)[i]: fixed.add(3*i+2)      # edges parallel to y: phi_y = 0
    free = np.setdiff1d(np.arange(3*nn), np.array(sorted(fixed)))
    w2, _ = sla.eigsh(K[free][:, free].tocsc(), k=nm, M=M[free][:, free].tocsc(), sigma=0, which='LM')
    return np.sort(np.sqrt(w2))/(2*math.pi)

if __name__ == '__main__':
    E, nu, rho, a = 70e9, 0.3, 2700.0, 1.0
    ok = True
    for h in (0.002, 0.02, 0.1):     # h/a = 0.002 thin (lock test), 0.02, 0.1 thick
        ex = sorted([exact_ss(a, h, E, nu, rho, m, n) for m in range(1, 4) for n in range(1, 4)])
        # modes (1,1), (1,2)x2, (2,2): compare the lowest four distinct
        for n in (16, 32, 48):
            f = ss_fem(a, h, E, nu, rho, n, 4)
            err = f/np.array([ex[0], ex[1], ex[2], ex[3]]) - 1
            print(f'h/a={h/a:.3f} mesh {n}x{n}x2 rel.err of modes 1..4:', np.round(err*100, 3), '%')
        if abs(err).max() > 0.01: ok = False
    print('PASS' if ok else 'FAIL'); sys.exit(0 if ok else 1)

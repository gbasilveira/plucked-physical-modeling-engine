# SPDX-License-Identifier: MIT
# Planning-time numerical check for docs/plan/engine-specification.md (see README.md in this folder).
import numpy as np, math
fs=48000.0; dt=1/fs
eps0=1e-12
def tc_exact(m,K,p,v):
    a=p+1; dm=(a*m*v*v/(2*K))**(1/a)
    return 2*dm/v*math.sqrt(math.pi)*math.gamma(1+1/a)/math.gamma(0.5+1/a)
# 1) hammer vs rigid wall: coordinate x (penetration positive), mass m, no linear terms
def wall(m,K,p,v,os=1):
    h=dt/os
    x=-1e-5; xo=x - v*h   # moving toward wall at v
    psi=math.sqrt(2*eps0)
    t=0; tin=None; tout=None; vout=None
    for it in range(int(0.05/h)):
        V=K/(p+1)*max(x,0)**(p+1)
        dV=K*max(x,0)**p
        g=dV/math.sqrt(2*(V+eps0))
        # x^{n+1} = 2x - xo + h^2/m * ( -g*(psi + g*(x1 - xo)/2 /2) )
        # => x1 = a - hh*(g*x1)/4 with hh = h^2 g/m
        hh=h*h*g/m
        a=2*x-xo - hh*psi + hh*(g*xo)/4
        x1 = a - hh*(g*a)/(4+g*hh)
        psi = psi + g*(x1-xo)/2
        if x1>0 and tin is None: tin=t
        if tin is not None and tout is None and x1<=0 and x>0: tout=t; vout=(x1-x)/h
        xo,x=x,x1; t+=h
        if V==0 and x<0 and tout is not None: break
    return tout-tin, vout
m=0.008; K=4e9; p=2.5; v=3.0
tc,vo=wall(m,K,p,v)
print(f"wall: contact {tc*1e3:.4f} ms exact {tc_exact(m,K,p,v)*1e3:.4f} ms; rebound {vo:.4f} m/s (in {v})")
tc,vo=wall(m,K,p,v,os=8)
print(f"wall os8: contact {tc*1e3:.4f} ms; rebound {vo:.4f}")
# 2) hammer + stiff string (modal), lossless: energy conservation
import osc; osc.dt=dt
L=0.62; rhoA=0.0055; T=680.0; EI=1.2e-4
N=80; n=np.arange(1,N+1); k=n*np.pi/L
w=np.sqrt((T*k**2+EI*k**4)/rhoA)
keep=w*dt<0.95*np.pi; w=w[keep]; k=k[keep]; N=len(w)
Kc=np.zeros(N);B=np.zeros(N);G=np.zeros(N);W2=np.zeros(N)
for i in range(N): Kc[i],B[i],G[i],W2[i]=osc.coeffs(w[i],0.0)
wh2=W2/dt**2
x0=L/8; b=0.01
Wk=(np.sin(k*b/2)/(k*b/2))*(np.pi**2/(np.pi**2-(k*b/2)**2))
phib=np.sqrt(2/L)*np.sin(k*x0)*Wk
mH=0.008; KH=4e9; pH=2.5; vH=3.0
# state: modal q,u ; hammer position xh (positive toward string), string disp at contact y = phib.q ; penetration d = xh - y
q=np.zeros(N); u=np.zeros(N)
xh=-1e-5; uh=vH*dt
psi=math.sqrt(2*eps0)
Es=[]
for it in range(int(0.02*fs)):
    y=phib@q; d=xh-y
    V=KH/(pH+1)*max(d,0)**(pH+1); dV=KH*max(d,0)**pH
    gs=dV/math.sqrt(2*(V+eps0))
    # gradient of psi wrt coordinates: d/dxh = gs, d/dq = -gs*phib
    # free updates (difference form): q_free = q + (B*u - Kc*q), xh_free = xh + uh
    qf=q + (B*u - Kc*q); xf=xh+uh
    qo=q-u; xo=xh-uh
    # vectors over coordinates [q..., xh]: hvec = gamma*grad/m
    gvec=np.concatenate([-gs*phib,[gs]])
    gam=np.concatenate([G,[dt*dt]]); mass=np.concatenate([np.full(N,rhoA),[mH]])
    hvec=gam*gvec/mass
    zf=np.concatenate([qf,[xf]]); zo=np.concatenate([qo,[xo]])
    a=zf - hvec*psi + hvec*(gvec@zo)/4
    z1=a - hvec*(gvec@a)/(4+gvec@hvec)
    psi=psi+gvec@(z1-zo)/2
    q1=z1[:N]; x1=z1[N]
    # energy at n+1/2
    un=q1-q; uhn=x1-xh
    E=rhoA*np.sum(0.5*(un/dt)**2+0.5*wh2*q*q1) + 0.5*mH*(uhn/dt)**2 + 0.5*psi**2 - eps0
    Es.append(E)
    u=un; q=q1; uh=uhn; xh=x1
Es=np.array(Es)
E0=0.5*mH*vH**2
print("hammer-string energy: initial", E0, "max rel dev", np.max(np.abs(Es-Es[0]))/Es[0])
print("final hammer velocity (rebound)", uh/dt)

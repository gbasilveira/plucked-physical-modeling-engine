# SPDX-License-Identifier: MIT
# Planning-time numerical check for docs/plan/engine-specification.md (see README.md in this folder).
import numpy as np, math
from osc import coeffs
fs=48000.0*4; dt=1/fs
L=1.0; T=1000.0; rhoA=0.01; EI=0.0
c=math.sqrt(T/rhoA); f0=c/(2*L); Z0=math.sqrt(T*rhoA)
N=60
n=np.arange(1,N+1); k=n*math.pi/L
w=np.sqrt((T*k**2+EI*k**4)/rhoA)
g=math.sqrt(2/L)*(-1.0)**(n+1)/k        # <psi, phi_n>, psi=x/L
import osc
osc.dt=dt
K=np.zeros(N);NB=np.zeros(N);C=np.zeros(N);W2=np.zeros(N)
for i in range(N):
    K[i],NB[i],C[i],W2[i]=osc.coeffs(w[i],0.0)
what2=W2/dt**2
def sim(G,secs,mode=1):
    q=np.zeros(N); u=np.zeros(N); q[mode-1]=1e-3
    wb=0.0; wb_old=0.0
    amps=[]
    nst=int(secs*fs)
    for it in range(nst):
        Fb = rhoA*np.dot(what2*g, q) - (T/L)*wb
        wb_new = wb + dt*G*Fb
        acc=(wb_new-2*wb+wb_old)/dt**2
        u = NB*u - K*q + C*(-g*acc)
        q = q + u
        wb_old, wb = wb, wb_new
        if it % 200==0: amps.append(math.hypot(q[mode-1], (u[mode-1]/dt)/w[mode-1]))
    return np.array(amps)
for G in (1e-3, 3e-3):
    a=sim(G,1.0)
    t=np.arange(len(a))*200/fs
    sl=np.polyfit(t[len(t)//4:], np.log(a[len(t)//4:]),1)[0]
    print(f"G={G}: measured sigma {-sl:.4f}, predicted 2 f0 Z0 G = {2*f0*Z0*G:.4f}, exact -f0 ln(1-2Z0G/(1+Z0G))... {-f0*math.log((1-Z0*G)/(1+Z0*G)):.4f}")
a=sim(1e-3,1.0,mode=3)
t=np.arange(len(a))*200/fs
sl=np.polyfit(t[len(t)//4:], np.log(a[len(t)//4:]),1)[0]
print("mode 3 sigma", -sl)

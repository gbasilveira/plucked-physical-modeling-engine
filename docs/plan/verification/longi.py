# SPDX-License-Identifier: MIT
# Planning-time numerical check for docs/plan/engine-specification.md (see README.md in this folder).
import numpy as np, math
rng=np.random.default_rng(1)
L=0.9; N=30; EA=2e5; T0=700.0; rhoA=0.006
q=rng.normal(size=N)*1e-4
n=np.arange(1,N+1); k=n*np.pi/L
x=np.linspace(0,L,400001)
yx=(np.sqrt(2/L)*k[:,None]*np.cos(k[:,None]*x[None,:])*q[:,None]).sum(0)
for m in (1,2,5,9,17):
    Km=m*np.pi/L
    chip=np.sqrt(2/L)*Km*np.cos(Km*x)
    quad=-((EA-T0)/(2*rhoA))*np.trapezoid(yx**2*chip,x)
    s=0.0
    for i in range(1,N+1):
        for j in range(1,N+1):
            if i+j==m or abs(i-j)==m: s+=k[i-1]*k[j-1]*q[i-1]*q[j-1]
    form=-((EA-T0)*Km/(2*rhoA*math.sqrt(2*L)))*s
    print(m, quad, form)
# KC: integral of yx^2 equals sum k^2 q^2
print("int yx^2", np.trapezoid(yx**2,x), "sum k^2 q^2", np.sum(k**2*q**2))

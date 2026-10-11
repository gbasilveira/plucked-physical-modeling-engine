# SPDX-License-Identifier: MIT
# Planning-time numerical check for docs/plan/engine-specification.md (see README.md in this folder).
import numpy as np, math
import osc
# 1) Hann window FT
b=0.02; x0=0.13; L=1.0
xs=np.linspace(x0-b/2,x0+b/2,200001)
g=(1+np.cos(2*np.pi*(xs-x0)/b))/b   # unit area Hann of full width b
for n in (1,5,20,60):
    k=n*np.pi/L
    num=np.trapezoid(g*np.sqrt(2/L)*np.sin(k*xs),xs)
    W=(math.sin(k*b/2)/(k*b/2))*(math.pi**2/(math.pi**2-(k*b/2)**2))
    print("hann n",n, num, math.sqrt(2/L)*math.sin(k*x0)*W)
# 2) tension closed form
L=1.2; rhoA=0.006; EI=2e-4; f1=110.0
T=4*L*L*rhoA*f1*f1 - math.pi**2*EI/L**2
w1=math.sqrt((T*(math.pi/L)**2+EI*(math.pi/L)**4)/rhoA)
print("tension check f1:", w1/(2*math.pi))
# 3) two strings sharing resistive bridge: in-phase vs anti-phase decay; plus rear segment
fs=48000.0*4; dt=1/fs; osc.dt=dt
L=1.0; T=1000.0; rhoA=0.01
c=math.sqrt(T/rhoA); f0=c/(2*L); Z0=math.sqrt(T*rhoA); G=1e-3
N=40; n=np.arange(1,N+1); k=n*np.pi/L; w=c*k
gA=np.sqrt(2/L)*(-1.0)**(n+1)/k
K=np.zeros(N);NB=np.zeros(N);C=np.zeros(N);W2=np.zeros(N)
for i in range(N): K[i],NB[i],C[i],W2[i]=osc.coeffs(w[i],0.0)
wh2=W2/dt**2
def run(sign,secs=1.0,rear=False):
    q=[np.zeros(N),np.zeros(N)]; u=[np.zeros(N),np.zeros(N)]
    q[0][0]=1e-3; q[1][0]=sign*1e-3
    # rear segment: length 0.15, same T,rhoA
    Lr=0.15; Nr=12; nr=np.arange(1,Nr+1); kr=nr*np.pi/Lr; wr=c*kr; gr=np.sqrt(2/Lr)/kr
    Kr=np.zeros(Nr);NBr=np.zeros(Nr);Cr=np.zeros(Nr);W2r=np.zeros(Nr)
    for i in range(Nr): Kr[i],NBr[i],Cr[i],W2r[i]=osc.coeffs(wr[i],0.0)
    qr=np.zeros(Nr); ur=np.zeros(Nr)
    wb=0.0; wbo=0.0; amps=[]; E=[]
    for it in range(int(secs*fs)):
        Fb=sum(rhoA*np.dot(wh2*gA,q[s]) - (T/L)*wb for s in range(2))
        if rear: Fb += rhoA*np.dot(W2r/dt**2*gr,qr) - (T/Lr)*wb
        wbn=wb+dt*G*Fb; acc=(wbn-2*wb+wbo)/dt**2
        for s in range(2):
            u[s]=NB*u[s]-K*q[s]+C*(-gA*acc); q[s]=q[s]+u[s]
        if rear:
            ur=NBr*ur-Kr*qr+Cr*(-gr*acc); qr=qr+ur
        wbo,wb=wb,wbn
        if it%200==0:
            a=math.hypot(q[0][0]+sign*q[1][0], (u[0][0]+sign*u[1][0])/dt/w[0])/2
            amps.append(a)
    a=np.array(amps); t=np.arange(len(a))*200/fs
    return -np.polyfit(t[len(t)//4:],np.log(a[len(t)//4:]+1e-30),1)[0]
print("in-phase sigma", run(+1), "predicted 4 f0 Z0 G =", 4*f0*Z0*G)
print("anti-phase sigma", run(-1), "predicted ~0")
print("in-phase with rear segment sigma", run(+1,rear=True))

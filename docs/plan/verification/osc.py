# SPDX-License-Identifier: MIT
# Planning-time numerical check for docs/plan/engine-specification.md (see README.md in this folder).
import numpy as np, math
fs=48000.0; dt=1/fs
def coeffs(w,sig):
    wd=math.sqrt(max(w*w-sig*sig,0.0))
    s=math.tanh(sig*dt)
    # what2 dt^2 = 2 - 2(1+s) e^{-sig dt} cos(wd dt), computed without cancellation
    e=math.exp(-sig*dt)
    one_minus_ecos = -math.expm1(-sig*dt) + e*2*math.sin(wd*dt/2)**2
    W2 = 2*one_minus_ecos - 2*s*e*math.cos(wd*dt)
    kappa = W2/(1+s)
    negb = (1-s)/(1+s)
    c = dt*dt/(1+s)
    return kappa,negb,c,W2
def run(f0,T60,dtype,nsamp):
    w=2*math.pi*f0; sig=6.9078/T60
    kappa,negb,c,_=coeffs(w,sig)
    kappa=dtype(kappa); negb=dtype(negb)
    q=dtype(1.0); u=dtype(0.0)  # u = q^n - q^{n-1}
    out=np.empty(nsamp,dtype=np.float64)
    for i in range(nsamp):
        u = negb*u - kappa*q
        q = q + u
        out[i]=q
    return out
def measure(x,f0):
    # estimate frequency via zero crossings over the record
    zc=np.where((x[:-1]<0)&(x[1:]>=0))[0]
    # refine with linear interp
    t=[i + (-x[i])/(x[i+1]-x[i]) for i in zc]
    per=(t[-1]-t[0])/(len(t)-1)
    return fs/per
for f0 in (27.5,261.6,4186.0):
    for dt_ in (np.float64,np.float32):
        x=run(f0,20.0,dt_,int(fs*2))
        fm=measure(x,f0)
        # expected damped frequency
        sig=6.9078/20; wd=math.sqrt((2*math.pi*f0)**2-sig**2)
        fe=wd/(2*math.pi)
        print(f"f0={f0:8.1f} {dt_.__name__}: measured {fm:.6f} expected {fe:.6f} err cents {1200*math.log2(fm/fe):+.4f}")
# naive two-step float32 for comparison
def naive(f0,nsamp):
    w=2*math.pi*f0; sig=6.9078/20
    kappa,negb,c,W2=coeffs(w,sig); s=math.tanh(sig*dt)
    a=np.float32((2-W2)/(1+s)); b=np.float32(-(1-s)/(1+s))
    q1=np.float32(1.0); q0=np.float32(1.0)
    out=np.empty(nsamp)
    for i in range(nsamp):
        q2=a*q1+b*q0; q0=q1; q1=q2; out[i]=q2
    return out
x=naive(27.5,int(fs*2)); fm=measure(x,27.5)
sig=6.9078/20; fe=math.sqrt((2*math.pi*27.5)**2-sig**2)/(2*math.pi)
print("naive two-step float32 A0: err cents", 1200*math.log2(fm/fe))
# decay check float32 diff form at A0: amplitude after 2 s
x=run(27.5,20.0,np.float32,int(fs*4))
env=np.max(np.abs(x[-2000:])); print("A0 float32 decay after 4s:",20*math.log10(env), "dB expected", -60*4/20)

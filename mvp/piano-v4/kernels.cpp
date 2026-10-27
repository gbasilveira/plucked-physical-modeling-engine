// SPDX-License-Identifier: MIT
#include "kernels.hpp"

namespace piano_mvp_v4::kern {
PIANO_CLONES void freeStepV(double* __restrict q, double* __restrict u, double* __restrict qPrev,
                            double* __restrict uPrev, const double* __restrict beta,
                            const double* __restrict kappa, const double* __restrict gamma,
                            const double* __restrict mass, const double* __restrict fc, double acc,
                            std::size_t n) noexcept {
    for (std::size_t i = 0; i < n; ++i) {
        const double f = (-fc[i] * acc) / mass[i];          // SPEC-NL discrete scheme: F^n / m
        const double qn = q[i], un = u[i];
        qPrev[i] = qn; uPrev[i] = un;
        const double uNext = beta[i] * un - kappa[i] * qn + gamma[i] * f;   // SPEC-OSC difference form
        u[i] = uNext; q[i] = qn + uNext;
    }
}
PIANO_CLONES void freeStepVFast(double* __restrict q, double* __restrict u, const double* __restrict beta,
                                const double* __restrict kappa, const double* __restrict gf, double acc,
                                std::size_t n) noexcept {
    for (std::size_t i = 0; i < n; ++i) {
        const double qn = q[i];
        const double uNext = beta[i] * u[i] - kappa[i] * qn - gf[i] * acc;   // SPEC-OSC difference form
        u[i] = uNext; q[i] = qn + uNext;
    }
}
PIANO_CLONES void freeStepH(double* __restrict q, double* __restrict u, const double* __restrict beta,
                            const double* __restrict kappa, const double* __restrict gamma,
                            const double* __restrict g, double acc, std::size_t n) noexcept {
    for (std::size_t i = 0; i < n; ++i) {
        const double f = -g[i] * acc;
        const double qn = q[i], un = u[i];
        const double uNext = beta[i] * un - kappa[i] * qn + gamma[i] * f;   // SPEC-OSC difference form
        u[i] = uNext; q[i] = qn + uNext;
    }
}
PIANO_CLONES void scaleQU(double* __restrict q, double* __restrict u, double c, std::size_t n) noexcept {
    for (std::size_t i = 0; i < n; ++i) { q[i] *= c; u[i] *= c; }
}
PIANO_CLONES void scaleQUPerMode(double* __restrict q, double* __restrict u, const double* __restrict c,
                                 std::size_t n) noexcept {
    for (std::size_t i = 0; i < n; ++i) { q[i] *= c[i]; u[i] *= c[i]; }
}
PIANO_CLONES void lightStep(double* __restrict q, double* __restrict u, const double* __restrict beta,
                            const double* __restrict kappa, const double* __restrict gamma,
                            const double* __restrict g, double* __restrict f, double acc,
                            std::size_t n) noexcept {
    for (std::size_t i = 0; i < n; ++i) {
        f[i] = -g[i] * acc;
        const double qn = q[i], un = u[i];
        const double uNext = beta[i] * un - kappa[i] * qn + gamma[i] * f[i];   // SPEC-OSC difference form
        u[i] = uNext; q[i] = qn + uNext;
    }
}
PIANO_CLONES double dotFast(const double* __restrict a, const double* __restrict q, std::size_t n) noexcept {
    double s[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    std::size_t i = 0;
    for (; i + 8 <= n; i += 8)
        for (int k = 0; k < 8; ++k) s[k] += a[i + k] * q[i + k];
    double t = ((s[0] + s[1]) + (s[2] + s[3])) + ((s[4] + s[5]) + (s[6] + s[7]));
    for (; i < n; ++i) t += a[i] * q[i];
    return t;
}
PIANO_CLONES double sumSqFast(const double* __restrict k2, const double* __restrict q, std::size_t n) noexcept {
    double s[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    std::size_t i = 0;
    for (; i + 8 <= n; i += 8)
        for (int k = 0; k < 8; ++k) s[k] += k2[i + k] * q[i + k] * q[i + k];
    double t = ((s[0] + s[1]) + (s[2] + s[3])) + ((s[4] + s[5]) + (s[6] + s[7]));
    for (; i < n; ++i) t += k2[i] * q[i] * q[i];
    return t;
}
PIANO_CLONES void axpy(double* __restrict y, const double* __restrict phi, double g, std::size_t n) noexcept {
    for (std::size_t i = 0; i < n; ++i) y[i] += phi[i] * g;
}
PIANO_CLONES void modeStepClearForce(double* __restrict q, double* __restrict u, const double* __restrict beta,
                                     const double* __restrict kappa, const double* __restrict gamma,
                                     double* __restrict force, std::size_t n) noexcept {
    for (std::size_t i = 0; i < n; ++i) {
        const double qn = q[i], un = u[i];
        const double uNext = beta[i] * un - kappa[i] * qn + gamma[i] * force[i];   // SPEC-OSC difference form
        u[i] = uNext; q[i] = qn + uNext; force[i] = 0.0;
    }
}
PIANO_CLONES double dot4(const double* __restrict a, const double* __restrict b, std::size_t n) noexcept {
    double s[4] = {0, 0, 0, 0};
    std::size_t i = 0;
    for (; i + 4 <= n; i += 4)
        for (int k = 0; k < 4; ++k) s[k] += a[i + k] * b[i + k];
    double t = (s[0] + s[1]) + (s[2] + s[3]);
    for (; i < n; ++i) t += a[i] * b[i];
    return t;
}
static inline __attribute__((always_inline)) void radiationLoop(
    const double* __restrict omega, const double* __restrict sigma, const double* __restrict c00,
    const double* __restrict c01, const double* __restrict c10, const double* __restrict c11,
    const double* __restrict reL, const double* __restrict imL, const double* __restrict reR,
    const double* __restrict imR, double* __restrict q, double* __restrict vv, double* __restrict drive,
    double* __restrict termL, double* __restrict termR, std::size_t n) noexcept {
    for (std::size_t i = 0; i < n; ++i) {
        const double forcing = drive[i];
        const double equilibrium = forcing / (omega[i] * omega[i]);
        const double shifted = q[i] - equilibrium;
        const double qn = c00[i] * shifted + c01[i] * vv[i] + equilibrium;
        const double v = c10[i] * shifted + c11[i] * vv[i];
        q[i] = qn; vv[i] = v;
        const double acceleration = forcing - 2 * sigma[i] * v - omega[i] * omega[i] * qn;
        // Re(A*q'' phasor) as piano_mvp_v3::modalPressure.
        termL[i] = reL[i] * acceleration - imL[i] * omega[i] * v;
        termR[i] = reR[i] * acceleration - imR[i] * omega[i] * v;
        drive[i] = 0;
    }
}
PIANO_CLONES void radiationModes(const RadiationArrays& a, std::size_t lo, std::size_t hi) noexcept {
    radiationLoop(a.omega + lo, a.sigma + lo, a.c00 + lo, a.c01 + lo, a.c10 + lo, a.c11 + lo, a.reL + lo,
                  a.imL + lo, a.reR + lo, a.imR + lo, a.q + lo, a.v + lo, a.drive + lo, a.termL + lo,
                  a.termR + lo, hi - lo);
}
PIANO_CLONES void bankDrive(float* __restrict drive, const float* __restrict phiT, const std::uint8_t* port, const float* g,
                            const std::uint8_t* light, int count, std::size_t chunks, std::size_t lightChunks) noexcept {
    // Four chunks (32 modes) at a time: four independent accumulator chains hide the add latency; each lane still
    // adds the ports in list order, so the result does not depend on the grouping or the vector width.
    for (std::size_t c = 0; c < chunks; c += 4) {
        float acc[4][8];
        for (int u = 0; u < 4; ++u) for (int l = 0; l < 8; ++l) acc[u][l] = drive[(c + u) * 8 + l];
        const float* base = phiT + c * 88 * 8;
        const bool lightSkip = c >= lightChunks;
        for (int k = 0; k < count; ++k) {
            if (lightSkip && light[k]) continue;
            const float* p = base + static_cast<std::size_t>(port[k]) * 8;
            const float gk = g[k];
            for (int u = 0; u < 4; ++u)
                for (int l = 0; l < 8; ++l) acc[u][l] += p[u * 88 * 8 + l] * gk;
        }
        for (int u = 0; u < 4; ++u) for (int l = 0; l < 8; ++l) drive[(c + u) * 8 + l] = acc[u][l];
    }
}
static inline __attribute__((always_inline)) void radiationLoopF(
    const double* __restrict omega, const double* __restrict sigma, const double* __restrict c00,
    const double* __restrict c01, const double* __restrict c10, const double* __restrict c11,
    const double* __restrict reL, const double* __restrict imL, const double* __restrict reR,
    const double* __restrict imR, double* __restrict q, double* __restrict vv, float* __restrict drive,
    double* __restrict termL, double* __restrict termR, std::size_t n) noexcept {
    for (std::size_t i = 0; i < n; ++i) {
        const double forcing = static_cast<double>(drive[i]);
        const double equilibrium = forcing / (omega[i] * omega[i]);
        const double shifted = q[i] - equilibrium;
        const double qn = c00[i] * shifted + c01[i] * vv[i] + equilibrium;
        const double v = c10[i] * shifted + c11[i] * vv[i];
        q[i] = qn; vv[i] = v;
        const double acceleration = forcing - 2 * sigma[i] * v - omega[i] * omega[i] * qn;
        termL[i] = reL[i] * acceleration - imL[i] * omega[i] * v;
        termR[i] = reR[i] * acceleration - imR[i] * omega[i] * v;
        drive[i] = 0.f;
    }
}
PIANO_CLONES void radiationModesF(const RadiationArraysF& a, std::size_t n) noexcept {
    radiationLoopF(a.omega, a.sigma, a.c00, a.c01, a.c10, a.c11, a.reL, a.imL, a.reR, a.imR, a.q, a.v, a.drive, a.termL, a.termR, n);
}
PIANO_CLONES void axpyC(float* __restrict dre, float* __restrict dim, const float* __restrict rre, const float* __restrict rim,
                        float g, std::size_t n) noexcept {
    for (std::size_t i = 0; i < n; ++i) { dre[i] += rre[i] * g; dim[i] += rim[i] * g; }
}
PIANO_CLONES void lfStep(const double* __restrict lre, const double* __restrict lim, double* __restrict xre, double* __restrict xim,
                         float* __restrict dre, float* __restrict dim, std::size_t n) noexcept {
    for (std::size_t i = 0; i < n; ++i) {
        const double a = xre[i], b = xim[i];
        xre[i] = lre[i] * a - lim[i] * b + static_cast<double>(dre[i]);
        xim[i] = lre[i] * b + lim[i] * a + static_cast<double>(dim[i]);
        dre[i] = 0.f; dim[i] = 0.f;
    }
}
PIANO_CLONES double sumLanes(const double* __restrict x, std::size_t n) noexcept {
    double s[8] = {};
    for (std::size_t i = 0; i < n; i += 8) for (int l = 0; l < 8; ++l) s[l] += x[i + l];
    return ((s[0] + s[1]) + (s[2] + s[3])) + ((s[4] + s[5]) + (s[6] + s[7]));
}
PIANO_CLONES void hfStep(const float* __restrict lre, const float* __restrict lim, float* __restrict xre, float* __restrict xim,
                         float* __restrict drive, std::size_t n) noexcept {
    for (std::size_t i = 0; i < n; ++i) {
        const float a = xre[i], b = xim[i];
        xre[i] = lre[i] * a - lim[i] * b + drive[i];
        xim[i] = lre[i] * b + lim[i] * a;
        drive[i] = 0.f;
    }
}
PIANO_CLONES double hfDot(const float* __restrict cre, const float* __restrict cim, const float* __restrict xre,
                          const float* __restrict xim, std::size_t n) noexcept {
    float s[8] = {};
    for (std::size_t i = 0; i < n; i += 8) for (int l = 0; l < 8; ++l) s[l] += cre[i + l] * xre[i + l] - cim[i + l] * xim[i + l];
    return static_cast<double>(((s[0] + s[1]) + (s[2] + s[3])) + ((s[4] + s[5]) + (s[6] + s[7])));
}
PIANO_CLONES double dotF(const float* __restrict a, const float* __restrict b, std::size_t n) noexcept {
    float s[8] = {};
    for (std::size_t i = 0; i < n; i += 8) for (int l = 0; l < 8; ++l) s[l] += a[i + l] * b[i + l];
    return static_cast<double>(((s[0] + s[1]) + (s[2] + s[3])) + ((s[4] + s[5]) + (s[6] + s[7])));
}
namespace {
template <int R>
inline __attribute__((always_inline)) void passA(const double* const* W, const double* __restrict q,
                                                 const double* __restrict u, double* __restrict zPrev,
                                                 double* __restrict qPrev, double* __restrict uPrev, std::size_t n,
                                                 double* known) noexcept {
    double s[R][4] = {};
    std::size_t i = 0;
    for (; i + 4 <= n; i += 4)
        for (int l = 0; l < 4; ++l) {
            const std::size_t x = i + l;
            const double z = q[x] - u[x];
            zPrev[x] = z; qPrev[x] = q[x]; uPrev[x] = u[x];
            for (int j = 0; j < R; ++j) s[j][l] += W[j][x] * z;
        }
    for (int j = 0; j < R; ++j) known[j] = (s[j][0] + s[j][1]) + (s[j][2] + s[j][3]);
    for (; i < n; ++i) {
        const double z = q[i] - u[i];
        zPrev[i] = z; qPrev[i] = q[i]; uPrev[i] = u[i];
        for (int j = 0; j < R; ++j) known[j] += W[j][i] * z;
    }
}
template <int R>
inline __attribute__((always_inline)) void passB(double* __restrict a, const double* const* W, const double* const* H,
                                                 const double* __restrict q, const double* __restrict u,
                                                 const double* __restrict beta, const double* __restrict kappa,
                                                 const double* __restrict gamma, const double* __restrict invMass,
                                                 const double* __restrict force, const double* known, bool sav,
                                                 double psi, std::size_t n, double* wa, double* M) noexcept {
    double sa[R][4] = {}, sm[R * R][4] = {};
    std::size_t i = 0;
    const auto one = [&](std::size_t x) {
        const double f = force[x] * invMass[x];
        const double uFree = beta[x] * u[x] - kappa[x] * q[x] + gamma[x] * f;   // SPEC-OSC difference form
        double ai = q[x] + uFree;
        for (int j = 0; j < R; ++j) ai += H[j][x] * known[j];
        if (sav) ai -= H[0][x] * psi;
        return ai;
    };
    for (; i + 4 <= n; i += 4)
        for (int l = 0; l < 4; ++l) {
            const std::size_t x = i + l;
            const double ai = one(x);
            a[x] = ai;
            for (int j = 0; j < R; ++j) {
                sa[j][l] += W[j][x] * ai;
                for (int m = 0; m < R; ++m) sm[j * R + m][l] += W[j][x] * H[m][x];
            }
        }
    for (int j = 0; j < R; ++j) {
        wa[j] = (sa[j][0] + sa[j][1]) + (sa[j][2] + sa[j][3]);
        for (int m = 0; m < R; ++m)
            M[j * R + m] = (sm[j * R + m][0] + sm[j * R + m][1]) + (sm[j * R + m][2] + sm[j * R + m][3]);
    }
    for (; i < n; ++i) {
        const double ai = one(i);
        a[i] = ai;
        for (int j = 0; j < R; ++j) {
            wa[j] += W[j][i] * ai;
            for (int m = 0; m < R; ++m) M[j * R + m] += W[j][i] * H[m][i];
        }
    }
}
template <int R>
inline __attribute__((always_inline)) void passC(double* __restrict q, double* __restrict u,
                                                 const double* __restrict uPrev, const double* __restrict a,
                                                 const double* const* H, const double* y, const double* __restrict g,
                                                 std::size_t n, double* dPsi) noexcept {
    double s[4] = {0, 0, 0, 0};
    std::size_t i = 0;
    const auto one = [&](std::size_t x, double& acc) {
        double z = a[x];
        for (int k = 0; k < R; ++k) z -= H[k][x] * y[k];
        const double uNew = z - q[x];
        if (g) acc += g[x] * (uNew + uPrev[x]);
        u[x] = uNew; q[x] = q[x] + uNew;
    };
    for (; i + 4 <= n; i += 4)
        for (int l = 0; l < 4; ++l) one(i + l, s[l]);
    double t = (s[0] + s[1]) + (s[2] + s[3]);
    for (; i < n; ++i) one(i, t);
    *dPsi = t;
}
}  // namespace
#define PIANO_FLIGHT_DISPATCH(NAME, ...) \
    switch (r) { case 1: NAME<1>(__VA_ARGS__); break; case 2: NAME<2>(__VA_ARGS__); break; \
                 case 3: NAME<3>(__VA_ARGS__); break; default: NAME<4>(__VA_ARGS__); break; }
PIANO_CLONES void flightPassA(int r, const double* const* W, const double* q, const double* u, double* zPrev,
                              double* qPrev, double* uPrev, std::size_t n, double* known) noexcept {
    PIANO_FLIGHT_DISPATCH(passA, W, q, u, zPrev, qPrev, uPrev, n, known)
}
PIANO_CLONES void flightPassB(int r, double* a, const double* const* W, const double* const* H, const double* q,
                              const double* u, const double* beta, const double* kappa, const double* gamma,
                              const double* invMass, const double* force, const double* known, bool sav, double psi,
                              std::size_t n, double* wa, double* M) noexcept {
    PIANO_FLIGHT_DISPATCH(passB, a, W, H, q, u, beta, kappa, gamma, invMass, force, known, sav, psi, n, wa, M)
}
PIANO_CLONES void flightPassC(int r, double* q, double* u, const double* uPrev, const double* a,
                              const double* const* H, const double* y, const double* g, std::size_t n,
                              double* dPsi) noexcept {
    PIANO_FLIGHT_DISPATCH(passC, q, u, uPrev, a, H, y, g, n, dPsi)
}
}

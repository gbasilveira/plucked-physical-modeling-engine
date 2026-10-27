// SPDX-License-Identifier: MIT
// 0.5.4 hot-loop kernels of the piano MVP.
//
// Exact kernels keep the operation order of the 0.5.3 code (SPEC-OSC difference form
// u' = (beta u - kappa q) + gamma f, q' = q + u'); they are elementwise, so a vectorised build gives
// the same bits as the scalar one (no FMA contraction: the AVX2 clone does not enable FMA).
// "Fast" kernels reassociate dot products into eight fixed accumulators (fixed combine order):
// deterministic, but not bit-identical to the sequential sum (difference ~1e-16 relative).
// On x86-64 Linux (GCC/Clang) the elementwise kernels are compiled twice and dispatched at load
// time (target_clones "avx2" / "default"); other targets use the default code.
#pragma once
#include <cstddef>
#include <cstdint>

#if defined(__x86_64__) && defined(__linux__) && defined(__has_attribute)
#  if __has_attribute(target_clones)
#    define PIANO_CLONES __attribute__((target_clones("avx2", "default")))
#  endif
#endif
#ifndef PIANO_CLONES
#  define PIANO_CLONES
#endif

namespace piano_mvp_v4::kern {
// Vertical string modes, no hammer contact and no tension (SAV skip path of pme::nl::savStep):
// f = (-fc acc) / mass, then the difference form; qPrev/uPrev receive q^n/u^n.
void freeStepV(double* q, double* u, double* qPrev, double* uPrev, const double* beta,
               const double* kappa, const double* gamma, const double* mass, const double* fc,
               double acc, std::size_t n) noexcept;
// Fast variant (reassociated, not bit-identical): the division is folded into gf = gamma fc / mass,
// u' = beta u - kappa q - gf acc; no qPrev/uPrev.
void freeStepVFast(double* q, double* u, const double* beta, const double* kappa, const double* gf,
                   double acc, std::size_t n) noexcept;
// Horizontal string modes: f = -g acc, then the difference form.
void freeStepH(double* q, double* u, const double* beta, const double* kappa, const double* gamma,
               const double* g, double acc, std::size_t n) noexcept;
// Damper: q *= c, u *= c (uniform), or per mode.
void scaleQU(double* q, double* u, double c, std::size_t n) noexcept;
void scaleQUPerMode(double* q, double* u, const double* c, std::size_t n) noexcept;
// Unstruck-string step: f = -g acc, then the difference form (SympatheticString::advance).
void lightStep(double* q, double* u, const double* beta, const double* kappa, const double* gamma,
               const double* g, double* f, double acc, std::size_t n) noexcept;
// Fast reassociated dot product sum a_i q_i (eight accumulators, fixed combine order).
double dotFast(const double* a, const double* q, std::size_t n) noexcept;
// Fast sum k2_i q_i^2.
double sumSqFast(const double* k2, const double* q, std::size_t n) noexcept;
// Hammer-flight SAV step (pme::nl::savStep with rank r = 1..4: the SAV column and the dashpot columns),
// fused into three passes with reassociated sums (README 0.5.4). `W` and `H` hold the r column pointers.
//  passA: z^{n-1} = q - u, qPrev, uPrev, known_j = w_j^T z^{n-1}.
//  passB: a = free update + sum_j h_j known_j - h_0 psi (psi only with the SAV column), wa_j = w_j^T a and
//         M_jk = w_j^T h_k (row-major r x r).
//  passC: z = a - sum_k h_k y_k, u' = z - q, q' = q + u', dPsi = sum g (u' + u) (g null: no SAV column).
void flightPassA(int r, const double* const* W, const double* q, const double* u, double* zPrev, double* qPrev,
                 double* uPrev, std::size_t n, double* known) noexcept;
void flightPassB(int r, double* a, const double* const* W, const double* const* H, const double* q, const double* u,
                 const double* beta, const double* kappa, const double* gamma, const double* invMass,
                 const double* force, const double* known, bool sav, double psi, std::size_t n, double* wa,
                 double* M) noexcept;
void flightPassC(int r, double* q, double* u, const double* uPrev, const double* a, const double* const* H,
                 const double* y, const double* g, std::size_t n, double* dPsi) noexcept;
// Board kernels. drive[i] += phi[i] g for i in [0,n).
void axpy(double* y, const double* phi, double g, std::size_t n) noexcept;
// SPEC-OSC difference form for the coupled board modes, force consumed (zeroed).
void modeStepClearForce(double* q, double* u, const double* beta, const double* kappa,
                        const double* gamma, double* force, std::size_t n) noexcept;
// Dot product with four fixed accumulators ((a0+a1)+(a2+a3), then the remainder in index order):
// the 0.5.3 GrandBoard::dot4 arithmetic, bit-identical to it.
double dot4(const double* a, const double* b, std::size_t n) noexcept;
// Zero-order-hold radiating modes [lo,hi) of the board (MVP 3 formulation); writes the per-mode
// pressure terms (summed in index order by the caller) and clears the drive.
struct RadiationArrays {
    const double *omega, *sigma, *c00, *c01, *c10, *c11, *reL, *imL, *reR, *imR;
    double *q, *v, *drive, *termL, *termR;
};
void radiationModes(const RadiationArrays& a, std::size_t lo, std::size_t hi) noexcept;

// 0.6 baked radiation bank. Drive accumulation in float, in port order, no contraction (so a vector build gives the same
// bits as the scalar one): for every 8-mode chunk c, drive[c*8+l] += phiT[(c*88 + port)*8 + l] * g for each port k in
// list order; ports flagged `light` skip the chunks >= lightChunks. phiT is chunk-major so a chunk streams across ports.
void bankDrive(float* drive, const float* phiT, const std::uint8_t* port, const float* g, const std::uint8_t* light,
               int count, std::size_t chunks, std::size_t lightChunks) noexcept;
// The zero-order-hold radiating-mode update of radiationModes() with a float drive (cleared); state and terms in double.
struct RadiationArraysF {
    const double *omega, *sigma, *c00, *c01, *c10, *c11, *reL, *imL, *reR, *imR;
    double *q, *v, *termL, *termR;
    float* drive;
};
void radiationModesF(const RadiationArraysF& a, std::size_t n) noexcept;

// ---- 0.7c grand radiation bank (grand_live.hpp). A complex one-pole resonator bank: x <- lam x + d, output Re sum x (low range, double
// state, drive d = sum over ports of complex residue * g), or the same with a real shared drive and complex output weights (high range,
// float state). Elementwise except the lane-wise sums (eight fixed lanes, fixed combine order), so any vector width gives the same bits.
void axpyC(float* dre, float* dim, const float* rre, const float* rim, float g, std::size_t n) noexcept;
void lfStep(const double* lre, const double* lim, double* xre, double* xim, float* dre, float* dim, std::size_t n) noexcept;
double sumLanes(const double* x, std::size_t n) noexcept;          // n a multiple of 8
void hfStep(const float* lre, const float* lim, float* xre, float* xim, float* drive, std::size_t n) noexcept;
double hfDot(const float* cre, const float* cim, const float* xre, const float* xim, std::size_t n) noexcept;   // sum cre xre - cim xim
double dotF(const float* a, const float* b, std::size_t n) noexcept;      // sum a b, eight float lanes (n a multiple of 8)

// Exact sequential dot products of up to three independent chains (interleaved for ILP; each
// chain's own sum is formed in index order, as the scalar reference).
inline void dotExact3(int chains, const double* const* a, const double* const* q,
                      const std::size_t* n, double* out) noexcept {
    double s0 = 0, s1 = 0, s2 = 0;
    std::size_t m = n[0];
    for (int c = 1; c < chains; ++c) if (n[c] < m) m = n[c];
    if (chains == 1) {
        for (std::size_t i = 0; i < m; ++i) s0 += a[0][i] * q[0][i];
    } else if (chains == 2) {
        for (std::size_t i = 0; i < m; ++i) { s0 += a[0][i] * q[0][i]; s1 += a[1][i] * q[1][i]; }
    } else {
        for (std::size_t i = 0; i < m; ++i) {
            s0 += a[0][i] * q[0][i]; s1 += a[1][i] * q[1][i]; s2 += a[2][i] * q[2][i];
        }
    }
    for (std::size_t i = m; i < n[0]; ++i) s0 += a[0][i] * q[0][i];
    if (chains > 1) for (std::size_t i = m; i < n[1]; ++i) s1 += a[1][i] * q[1][i];
    if (chains > 2) for (std::size_t i = m; i < n[2]; ++i) s2 += a[2][i] * q[2][i];
    out[0] = s0; if (chains > 1) out[1] = s1; if (chains > 2) out[2] = s2;
}
}

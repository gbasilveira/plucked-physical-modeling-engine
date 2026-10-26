// SPDX-License-Identifier: MIT
// Piano 0.7a stage 2: 3-D staggered-grid acoustic FDTD with the soundboard as a two-way coupled modal
// moving boundary, rigid walls (rim, lid, floor) and an absorbing sponge. Offline tool, not used by the plug-ins.
//
// Scheme (SPEC-N/A, MVP only): rho dv/dt = -grad p, dp/dt = -rho c^2 div v on a Yee-type grid
// (p at cell centres, v on faces, leap-frog; the 7-point scalar scheme in first-order form). The board is a
// layer of z-faces whose velocity is prescribed by the modal board  v_b(c) = sum_i Phi[c,i] qdot_i ; the
// generalised force on mode i is F_i = sum_c Phi[c,i] dA (p_below - p_above).  Modal oscillators use the
// leap-frog update with the stiffness pre-warped so the numerical frequency equals the FE frequency.
//
// Modes: recip  = unit volume-velocity source at a cell (a microphone position); records the modal velocities
//                 of all board modes (reciprocity: v_i/Q = -p_m/F_i).
//        fwd    = Gaussian force on one board mode; records pressure at given cells (reciprocity check).
//        wave   = no board modes, source + receivers (grid dispersion / sponge tests).
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef _OPENMP
#include <omp.h>
#endif

namespace {
constexpr double kRho = 1.2, kC = 343.0;
struct Scene {
    int nx{}, ny{}, nz{}, kb{}; double dx{}, dt{}; double sponge{0.5}, sigmaScale{15.0};
    std::vector<std::uint8_t> solid;               // nx*ny*nz, 1 = rigid cell
    std::vector<int> ci, cj;                       // board columns
    std::vector<float> phi;                        // [ncols][nmodes]
    std::vector<double> omega, sigma;              // modes
    int ncols{}, nmodes{};
};
std::map<std::string, std::string> readKV(const std::string& path) {
    std::ifstream f(path); if (!f) throw std::runtime_error("cannot read " + path);
    std::map<std::string, std::string> m; std::string line;
    while (std::getline(f, line)) { auto e = line.find('='); if (e != std::string::npos) m[line.substr(0, e)] = line.substr(e + 1); }
    return m;
}
template <class T> std::vector<T> readBin(const std::string& path, size_t count) {
    std::ifstream f(path, std::ios::binary); if (!f) throw std::runtime_error("cannot read " + path);
    std::vector<T> v(count); f.read(reinterpret_cast<char*>(v.data()), count * sizeof(T));
    if (!f) throw std::runtime_error("short file " + path);
    return v;
}
std::vector<int> parseCell(const std::string& s) { std::vector<int> r; std::stringstream ss(s); std::string t; while (std::getline(ss, t, ',')) r.push_back(std::stoi(t)); return r; }
}

int main(int argc, char** argv) {
    try {
        bool energy = false; std::string dir, mode = "recip", out = "out", srcS, recS, fwdModeS; int steps = 24000, nUse = -1; double tau = 7e-5; bool noBoard = false;
        for (int i = 1; i < argc; ++i) {
            std::string a = argv[i];
            auto nxt = [&]() { if (i + 1 >= argc) throw std::runtime_error("missing value for " + a); return std::string(argv[++i]); };
            if (a == "--scene") dir = nxt(); else if (a == "--mode") mode = nxt(); else if (a == "--out") out = nxt();
            else if (a == "--src") srcS = nxt(); else if (a == "--rec") recS = nxt(); else if (a == "--steps") steps = std::stoi(nxt());
            else if (a == "--modes") nUse = std::stoi(nxt()); else if (a == "--mode-index") fwdModeS = nxt();
            else if (a == "--tau") tau = std::stod(nxt()); else if (a == "--no-board") noBoard = true; else if (a == "--energy") energy = true;
            else throw std::runtime_error("unknown option " + a);
        }
        auto kv = readKV(dir + "/scene.txt");
        Scene S;
        S.nx = std::stoi(kv["nx"]); S.ny = std::stoi(kv["ny"]); S.nz = std::stoi(kv["nz"]); S.kb = std::stoi(kv["kb"]);
        S.dx = std::stod(kv["dx"]); S.dt = std::stod(kv["dt"]); S.ncols = std::stoi(kv["ncols"]); S.nmodes = std::stoi(kv["nmodes"]);
        if (kv.count("sponge")) S.sponge = std::stod(kv["sponge"]);
        const int nx = S.nx, ny = S.ny, nz = S.nz;
        const size_t ncell = size_t(nx) * ny * nz;
        S.solid = readBin<std::uint8_t>(dir + "/solid.u8", ncell);
        if (S.ncols > 0) {
            auto cij = readBin<std::int32_t>(dir + "/columns.i32", size_t(S.ncols) * 2);
            S.ci.resize(S.ncols); S.cj.resize(S.ncols);
            for (int c = 0; c < S.ncols; ++c) { S.ci[c] = cij[2 * c]; S.cj[c] = cij[2 * c + 1]; }
            S.phi = readBin<float>(dir + "/phi.f32", size_t(S.ncols) * S.nmodes);
            auto om = readBin<double>(dir + "/modes.f64", size_t(S.nmodes) * 2);
            S.omega.assign(om.begin(), om.begin() + S.nmodes); S.sigma.assign(om.begin() + S.nmodes, om.end());
        }
        int N = (nUse < 0 || nUse > S.nmodes) ? S.nmodes : nUse;
        if (noBoard || mode == "wave") N = 0;
        const double dx = S.dx, dt = S.dt;
        const double cfl = kC * dt / dx;
        if (cfl * std::sqrt(3.0) > 1.0) throw std::runtime_error("CFL violated");
        // ---- grid arrays (padded: index (k*(ny+1)+j)*(nx+1)+i, cells i<nx etc., faces 0..n)
        const int sx = nx + 1, sy = ny + 1, sz = nz + 1; const size_t tot = size_t(sx) * sy * sz;
        auto id = [&](int i, int j, int k) { return (size_t(k) * sy + j) * sx + i; };
        std::vector<float> p(tot, 0.f), vx(tot, 0.f), vy(tot, 0.f), vz(tot, 0.f), sa(tot, 1.f), sb(tot, 1.f);
        std::vector<std::uint8_t> ox(tot, 0), oy(tot, 0), oz(tot, 0), sol(tot, 1);
        for (int k = 0; k < nz; ++k) for (int j = 0; j < ny; ++j) for (int i = 0; i < nx; ++i) sol[id(i, j, k)] = S.solid[(size_t(k) * ny + j) * nx + i];
        // sponge on all lateral sides and the top, not on the floor
        const double L = S.sponge; const double sigMax = S.sigmaScale * kC / L;
        for (int k = 0; k < nz; ++k) for (int j = 0; j < ny; ++j) for (int i = 0; i < nx; ++i) {
            double d = std::min({ (i + 0.5) * dx, (nx - i - 0.5) * dx, (j + 0.5) * dx, (ny - j - 0.5) * dx, (nz - k - 0.5) * dx });
            double sg = d < L ? sigMax * std::pow((L - d) / L, 2.0) : 0.0;
            sa[id(i, j, k)] = float((1 - 0.5 * sg * dt) / (1 + 0.5 * sg * dt)); sb[id(i, j, k)] = float(1 / (1 + 0.5 * sg * dt));
        }
        for (int k = 0; k < nz; ++k) for (int j = 0; j < ny; ++j) for (int i = 0; i < nx; ++i) {
            const bool s0 = sol[id(i, j, k)];
            if (i > 0) ox[id(i, j, k)] = !s0 && !sol[id(i - 1, j, k)];
            if (j > 0) oy[id(i, j, k)] = !s0 && !sol[id(i, j - 1, k)];
            if (k > 0) oz[id(i, j, k)] = !s0 && !sol[id(i, j, k - 1)];
        }
        // board faces: not updated by the air, set by the modes
        std::vector<size_t> bface(S.ncols), bBelow(S.ncols), bAbove(S.ncols);
        for (int c = 0; c < S.ncols; ++c) {
            bface[c] = id(S.ci[c], S.cj[c], S.kb); oz[bface[c]] = 0;
            bBelow[c] = id(S.ci[c], S.cj[c], S.kb - 1); bAbove[c] = bface[c];
            if (sol[bBelow[c]] || sol[bAbove[c]]) throw std::runtime_error("board column inside a solid cell");
        }
        const float Bv = float(dt / (kRho * dx)), Bp = float(kRho * kC * kC * dt / dx);
        // ---- modes
        std::vector<double> q(N, 0.0), qd(N, 0.0), w2(N), damp(N), Fm(N, 0.0);
        for (int m = 0; m < N; ++m) { double w = 2 / dt * std::sin(0.5 * S.omega[m] * dt); w2[m] = w * w; damp[m] = S.sigma[m]; }
        const double dA = dx * dx;
        std::vector<double> dp(S.ncols, 0.0);
        // ---- source / receivers
        std::vector<int> src = srcS.empty() ? std::vector<int>{} : parseCell(srcS);
        std::vector<std::vector<int>> rec;
        { std::stringstream ss(recS); std::string t; while (std::getline(ss, t, ';')) if (!t.empty()) rec.push_back(parseCell(t)); }
        const int fwdMode = fwdModeS.empty() ? -1 : std::stoi(fwdModeS);
        std::vector<float> vrec; if (mode == "recip") vrec.reserve(size_t(steps) * N);
        std::vector<float> prec, erec; std::vector<float> qsrc(steps, 0.f); std::vector<float> pprev; std::vector<double> qprev(N, 0.0); if (energy) pprev.assign(tot, 0.f);
        const double t0 = 4 * tau;
        auto t_start = std::chrono::steady_clock::now();
        for (int n = 0; n < steps; ++n) {
            const double tn = (n + 0.5) * dt;
            const double g = std::exp(-0.5 * ((tn - t0) / tau) * ((tn - t0) / tau));
            // ---- board modes (force from p^n)
            if (N > 0) {
                for (int c = 0; c < S.ncols; ++c) dp[c] = double(p[bBelow[c]]) - double(p[bAbove[c]]);
                // F_m = dA sum_c Phi[c][m] dp[c]; blocks of 8 contiguous modes per task
                #pragma omp parallel for schedule(static)
                for (int m0 = 0; m0 < N; m0 += 8) {
                    const int m1 = std::min(N, m0 + 8); double f[8] = {0, 0, 0, 0, 0, 0, 0, 0};
                    for (int c = 0; c < S.ncols; ++c) { const float* ph = S.phi.data() + size_t(c) * S.nmodes + m0; const double d = dp[c];
                        for (int m = 0; m < m1 - m0; ++m) f[m] += double(ph[m]) * d; }
                    for (int m = m0; m < m1; ++m) Fm[m] = f[m - m0] * dA;
                }
                if (mode == "fwd" && fwdMode >= 0 && fwdMode < N) { const double gf = std::exp(-0.5 * ((n * dt - t0) / tau) * ((n * dt - t0) / tau)); Fm[fwdMode] += gf; qsrc[n] = float(gf); }   // 1 N peak Gaussian force on one mode, at the integer time
                #pragma omp parallel for schedule(static)
                for (int m = 0; m < N; ++m) {
                    qd[m] = ((1 - damp[m] * dt) * qd[m] + dt * (Fm[m] - w2[m] * q[m])) / (1 + damp[m] * dt);
                    q[m] += dt * qd[m];
                }
                // wall velocity of the board
                #pragma omp parallel for schedule(static)
                for (int c = 0; c < S.ncols; ++c) {
                    double v = 0; const float* ph = S.phi.data() + size_t(c) * S.nmodes;
                    for (int m = 0; m < N; ++m) v += double(ph[m]) * qd[m];
                    vz[bface[c]] = float(v);
                }
            }
            // ---- velocities from p^n
            #pragma omp parallel for collapse(2) schedule(static)
            for (int k = 0; k < nz; ++k) for (int j = 0; j < ny; ++j) {
                size_t base = id(0, j, k);
                for (int i = 1; i < nx; ++i) {
                    size_t a = base + i;
                    vx[a] = ox[a] ? sa[a] * vx[a] - sb[a] * Bv * (p[a] - p[a - 1]) : 0.f;
                }
                if (j > 0) for (int i = 0; i < nx; ++i) {
                    size_t a = base + i;
                    vy[a] = oy[a] ? sa[a] * vy[a] - sb[a] * Bv * (p[a] - p[a - sx]) : 0.f;
                }
                if (k > 0) for (int i = 0; i < nx; ++i) {
                    size_t a = base + i;
                    if (oz[a]) vz[a] = sa[a] * vz[a] - sb[a] * Bv * (p[a] - p[a - size_t(sx) * sy]);
                    // walls and board faces keep their value (0 or the board velocity, set above)
                }
            }
            // faces that are neither open nor board: zero (set once at start by initialisation; vz of walls is never written)
            // ---- pressure from v^{n+1/2}
            #pragma omp parallel for collapse(2) schedule(static)
            for (int k = 0; k < nz; ++k) for (int j = 0; j < ny; ++j) {
                size_t base = id(0, j, k);
                for (int i = 0; i < nx; ++i) {
                    size_t a = base + i;
                    float div = (vx[a + 1] - vx[a]) + (vy[a + sx] - vy[a]) + (vz[a + size_t(sx) * sy] - vz[a]);
                    p[a] = sa[a] * p[a] - sb[a] * Bp * div;
                }
            }
            if (!src.empty()) {
                size_t a = id(src[0], src[1], src[2]);
                // unit-amplitude volume velocity Q (m^3/s) injected into the cell: dp = rho c^2 dt Q / dx^3
                p[a] += float(kRho * kC * kC * dt * g / (dx * dx * dx));
                qsrc[n] = float(g);
            }
            if (energy && n % 10 == 0) {
                // discrete energy (staggered leap-frog invariant): air  sum 0.5 rho |v^{n+1/2}|^2 + p^n p^{n+1}/(2 rho c^2), modes 0.5 qd^2 + 0.5 w2 q^{n} q^{n+1}
                double ea = 0; const double dV = dx * dx * dx;
                for (int k = 0; k < nz; ++k) for (int j = 0; j < ny; ++j) for (int i = 0; i < nx; ++i) {
                    size_t a = id(i, j, k);
                    ea += 0.5 * kRho * (double(vx[a]) * vx[a] + double(vy[a]) * vy[a] + double(vz[a]) * vz[a]) * dV;   // every face once (faces 0 are walls: v = 0)
                    ea += 0.5 * double(p[a]) * pprev[a] / (kRho * kC * kC) * dV;
                }
                double em = 0; for (int m = 0; m < N; ++m) em += 0.5 * qd[m] * qd[m] + 0.5 * w2[m] * q[m] * qprev[m];
                erec.push_back(float(ea)); erec.push_back(float(em));
            }
            if (energy) { pprev = p; qprev = q; }
            if (mode == "recip" && N > 0) for (int m = 0; m < N; ++m) vrec.push_back(float(qd[m]));
            for (auto& r : rec) prec.push_back(p[id(r[0], r[1], r[2])]);
            if (n % 2000 == 0) {
                double pm = 0; for (size_t a = 0; a < tot; a += 7) pm = std::max(pm, double(std::fabs(p[a])));
                double el = std::chrono::duration<double>(std::chrono::steady_clock::now() - t_start).count();
                std::fprintf(stderr, "step %d/%d  max|p| %.3e  elapsed %.0fs\n", n, steps, pm, el);
                if (!std::isfinite(pm) || pm > 1e8) throw std::runtime_error("unstable");
            }
        }
        std::ofstream(out + ".qsrc.f32", std::ios::binary).write(reinterpret_cast<char*>(qsrc.data()), qsrc.size() * 4);
        if (!vrec.empty()) std::ofstream(out + ".vmodes.f32", std::ios::binary).write(reinterpret_cast<char*>(vrec.data()), vrec.size() * 4);
        if (!prec.empty()) std::ofstream(out + ".prec.f32", std::ios::binary).write(reinterpret_cast<char*>(prec.data()), prec.size() * 4);
        if (!erec.empty()) std::ofstream(out + ".energy.f32", std::ios::binary).write(reinterpret_cast<char*>(erec.data()), erec.size() * 4);
        std::ofstream(out + ".meta.txt") << "steps=" << steps << "\nnmodes=" << N << "\nnrec=" << rec.size() << "\ndt=" << dt << "\nmode=" << mode << "\n";
        double el = std::chrono::duration<double>(std::chrono::steady_clock::now() - t_start).count();
        std::fprintf(stderr, "done %d steps in %.1f s (%.2f ms/step)\n", steps, el, 1000 * el / steps);
    } catch (const std::exception& e) { std::fprintf(stderr, "error: %s\n", e.what()); return 1; }
    return 0;
}

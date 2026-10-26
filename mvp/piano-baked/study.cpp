// SPDX-License-Identifier: MIT
// 0.6 radiation study: records the port drives of a Full render, then replays them through (a) the modal
// radiation bank, (b) the baked (reduced) bank, (c) FFT-convolution with per-port impulse responses of length T,
// reporting error against (a) and the cost of each stage in isolation (single thread).
//   piano-baked-study [--events FILE] [--from S] [--seconds S] [--ir-seconds T,T,..] [--partition P]
#include "common.hpp"
#include "part_conv.hpp"
#include "grand_board.hpp"
#include "soundboard_table.hpp"
#include <map>
#include <set>
using pb::Piano;
using namespace piano_mvp_v4;

struct Log { std::vector<std::uint32_t> off; std::vector<std::uint16_t> id; std::vector<double> g; std::vector<float> refL, refR; };

static void errors(const std::vector<float>& a, const std::vector<float>& b, double& relRms, double& peak, int skip = 0) {
    double e = 0, s = 0; peak = 0;
    for (std::size_t i = skip; i < a.size(); ++i) { const double d = double(a[i]) - double(b[i]); e += d * d; s += double(a[i]) * a[i]; peak = std::max(peak, std::abs(d)); }
    relRms = s > 0 ? std::sqrt(e / s) : 0;
}
int main(int argc, char** argv) {
    try {
        std::string events = "mvp/piano-v4/listening/chopin-op28-no4.events"; double from = 40, seconds = 8; std::vector<double> irs{0.25, 0.5}; int P = 64;
        for (int i = 1; i < argc; ++i) {
            const std::string a = argv[i];
            if (a == "--events") events = argv[++i]; else if (a == "--from") from = std::stod(argv[++i]); else if (a == "--seconds") seconds = std::stod(argv[++i]);
            else if (a == "--partition") P = std::stoi(argv[++i]);
            else if (a == "--ir-seconds") { irs.clear(); std::string s = argv[++i]; std::size_t p = 0; while (p < s.size()) { auto q = s.find(',', p); irs.push_back(std::stod(s.substr(p, q == std::string::npos ? q : q - p))); if (q == std::string::npos) break; p = q + 1; } }
        }
        const int frames = int(seconds * pb::kFs);
        auto ev = pb::excerpt(pb::readEvents(events), from, from + seconds);
        Piano piano; pb::configure(piano, Piano::Quality::Full, Piano::Radiation::Modal, 0);
        Log log; log.off.push_back(0);
        std::printf("recording %d frames of Full ...\n", frames); std::fflush(stdout);
        auto st = pb::render(piano, ev, frames, [&](int) {
            for (int k = 0; k < piano.radiationPortCount(); ++k) {
                const double g = piano.radiationDrive(k);
                if (g == 0.0) continue;
                log.id.push_back(std::uint16_t(piano.radiationPort(k) * 2 + (piano.radiationLight(k) ? 1 : 0))); log.g.push_back(g);
            }
            log.off.push_back(std::uint32_t(log.id.size()));
        });
        log.refL = st.l; log.refR = st.r;
        std::set<int> used(log.id.begin(), log.id.end());
        std::printf("drive records %zu (mean %.1f ports per sample), distinct port/light sets %zu\n", log.id.size(), double(log.id.size()) / frames, used.size());

        // ---- (a),(b) GrandBoard banks, isolated ----
        std::vector<double> phi; buildParticipation(true, phi);
        bool profile = false; for (int i = 1; i < argc; ++i) if (std::string(argv[i]) == "--profile") profile = true;
        auto runBoard = [&](bool baked, double tol, std::vector<float>& L, std::vector<float>& R, double& ns) {
            GrandBoard b; b.configure(pb::kFs, phi, 128, false, true, tol); b.setBaked(baked);
            std::vector<float> g32; L.assign(frames, 0.f); R.assign(frames, 0.f);
            // the bridge-mass low pass is applied by Piano before the log (g is post-filter), so drive directly
            pme::simd::ScopedFlushDenormals ftz;
            const double t0 = pb::now(); double tD = 0, tM = 0, tF = 0;
            for (int i = 0; i < frames; ++i) {
                std::array<std::uint8_t, 88> port, light; std::array<double, 88> gg; int n = 0;
                for (std::uint32_t k = log.off[i]; k < log.off[i + 1]; ++k) { port[n] = std::uint8_t(log.id[k] >> 1); light[n] = log.id[k] & 1; gg[n++] = log.g[k]; }
                const double a0 = profile ? pb::now() : 0;
                b.accumulateDrives(port.data(), gg.data(), light.data(), n);
                const double a1 = profile ? pb::now() : 0;
                b.radiationModes(0, kBoardModeCount);
                const double a2 = profile ? pb::now() : 0;
                double l, r; b.radiationFinish(l, r);
                if (profile) { const double a3 = pb::now(); tD += a1 - a0; tM += a2 - a1; tF += a3 - a2; }
                L[i] = float(0.05 * l); R[i] = float(0.05 * r);
            }
            ns = (pb::now() - t0) / frames * 1e9;
            if (profile) std::printf("   [profile %s] drive %.0f ns, modes %.0f ns, finish %.0f ns per sample (timer overhead ~25 ns each)\n", baked ? "baked" : "modal", tD / frames * 1e9, tM / frames * 1e9, tF / frames * 1e9);
            return b.bakedModes();
        };
        std::vector<float> mL, mR, bL, bR; double nsM, nsB, rel, pk;
        runBoard(false, 0, mL, mR, nsM);
        errors(st.l, mL, rel, pk); std::printf("modal replay vs Piano output (sanity, should be 0): rel rms %.3g peak %.3g\n", rel, pk);
        double sigRms = 0; for (float x : mL) sigRms += double(x) * x; sigRms = std::sqrt(sigRms / frames);
        std::printf("signal rms %.5f\n", sigRms);
        std::printf("modal 512-mode bank: %.0f ns/sample (%.3f of a 48 kHz sample)\n", nsM, nsM * 48000e-9);
        for (double tol : {0.0, 1e-10, 1e-9, 3e-9, 1e-8, 1e-7, 1e-6}) {
            const std::size_t n = runBoard(true, tol, bL, bR, nsB);
            double r2, p2; errors(mL, bL, rel, pk); errors(mR, bR, r2, p2);
            std::printf("baked tol %.0e: %zu modes, error L rel rms %.2f dB (peak %.2e) R %.2f dB, %.0f ns/sample (x%.2f)\n", tol, n, 20 * std::log10(rel + 1e-30), pk, 20 * std::log10(r2 + 1e-30), nsB, nsM / nsB);
        }

        // ---- (c) FFT convolution ----
        const auto delay = pb::kFs * piano_mvp_v3::kRadiationDelaySeconds; const auto whole = std::size_t(delay); const double fr = delay - double(whole);
        std::map<int, int> idx; std::vector<int> vps(used.begin(), used.end()); for (std::size_t i = 0; i < vps.size(); ++i) idx[vps[i]] = int(i);
        for (double T : irs) {
            const std::size_t N = std::size_t(T * pb::kFs) + whole + 2;
            std::vector<std::vector<float>> irL(vps.size(), std::vector<float>(N, 0.f)), irR = irL;
            std::vector<double> hL(N), hR(N);
            const double t0 = pb::now();
            for (std::size_t m = 0; m < kBoardModeCount; ++m) {
                const auto& row = piano_mvp_v3::kSoundboardModes[m];
                if (std::abs(row[4]) + std::abs(row[5]) + std::abs(row[6]) + std::abs(row[7]) < 1e-9) continue;
                const double om = row[0], sg = row[1], wd = std::sqrt(om * om - sg * sg), dec = std::exp(-sg / pb::kFs);
                const double s = std::sin(wd / pb::kFs) / wd, c = std::cos(wd / pb::kFs);
                const double c00 = dec * (c + sg * s), c01 = dec * s, c10 = -dec * om * om * s, c11 = dec * (c - sg * s);
                double q = 0, v = 0;
                for (std::size_t n = 0; n < N; ++n) {   // impulse: drive 1 at n = 0
                    const double f = n == 0 ? 1.0 : 0.0, eq = f / (om * om), sh = q - eq;
                    const double qn = c00 * sh + c01 * v + eq, vn = c10 * sh + c11 * v;
                    q = qn; v = vn;
                    const double acc = f - 2 * sg * v - om * om * q;
                    hL[n] = row[4] * acc - row[5] * om * v; hR[n] = row[6] * acc - row[7] * om * v;
                }
                for (int vp : vps) {
                    if ((vp & 1) && m >= kLightRadiatingModes) continue;
                    const double ph = phi[std::size_t(vp >> 1) * kBoardModeCount + m];
                    float* dl = irL[idx[vp]].data(); float* dr = irR[idx[vp]].data();
                    for (std::size_t n = 0; n + whole + 1 < N; ++n) {
                        dl[n + whole] += float(0.05 * ph * (1 - fr) * hL[n]); dl[n + whole + 1] += float(0.05 * ph * fr * hL[n]);
                        dr[n + whole] += float(0.05 * ph * (1 - fr) * hR[n]); dr[n + whole + 1] += float(0.05 * ph * fr * hR[n]);
                    }
                }
            }
            const double tBuild = pb::now() - t0;
            std::vector<const float*> pl, pr; for (auto& v : irL) pl.push_back(v.data()); for (auto& v : irR) pr.push_back(v.data());
            for (int part : {P}) {
                pb::PartConv conv; if (!conv.prepare(part, int(vps.size()), pl, pr, N)) throw std::runtime_error("conv prepare");
                std::vector<float> cL(frames), cR(frames); std::vector<int> ids; std::vector<float> gg;
                const double t1 = pb::now(); double tc = 0;
                for (int i = 0; i < frames; ++i) {
                    ids.clear(); gg.clear();
                    for (std::uint32_t k = log.off[i]; k < log.off[i + 1]; ++k) { ids.push_back(idx[log.id[k]]); gg.push_back(float(log.g[k])); }
                    // a port may appear as light and full in different samples; both ids are separate inputs
                    double l, r; const double a = pb::now();
                    conv.step(ids.data(), gg.data(), int(ids.size()), l, r);
                    tc += pb::now() - a; cL[i] = float(l); cR[i] = float(r);
                }
                (void)t1;
                double r2, p2; errors(mL, cL, rel, pk); errors(mR, cR, r2, p2);
                std::printf("conv IR %.2f s (%zu taps, %zu inputs, P %d, %d partitions, %.0f MB): error L %.2f dB R %.2f dB (peak %.2e), %.0f ns/sample (x%.2f of modal, timer overhead included), IR build %.1f s\n",
                            T, N, vps.size(), part, conv.partitions(), double(conv.bytes()) / 1e6, 20 * std::log10(rel + 1e-30), 20 * std::log10(r2 + 1e-30), pk, tc / frames * 1e9, nsM / (tc / frames * 1e9), tBuild);
            }
        }
    } catch (const std::exception& ex) { std::fprintf(stderr, "%s\n", ex.what()); return 1; }
}

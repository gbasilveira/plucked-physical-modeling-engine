// SPDX-License-Identifier: MIT
// Piano 0.7c test G7-1 "baked grand radiation error bound": the embedded bank against the 0.7b responses of tests/grand_bank_ref.bin
// (four ports, four lid/microphone sets, 0.5 s). The error per octave band (62 Hz to 16 kHz) of the left and right channels together is
// the energy of the difference over the energy of the reference; the level difference per band is the energy ratio. Prints a table.
#include "grand_live.hpp"
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

static void fft(std::vector<std::complex<double>>& a) {
    const std::size_t n = a.size();
    for (std::size_t i = 1, j = 0; i < n; ++i) { std::size_t bit = n >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit; if (i < j) std::swap(a[i], a[j]); }
    for (std::size_t len = 2; len <= n; len <<= 1) {
        const double ang = -2 * 3.14159265358979323846 / double(len); const std::complex<double> w(std::cos(ang), std::sin(ang));
        for (std::size_t i = 0; i < n; i += len) { std::complex<double> wn(1); for (std::size_t j = 0; j < len / 2; ++j) {
            const auto u = a[i + j], v = a[i + j + len / 2] * wn; a[i + j] = u + v; a[i + j + len / 2] = u - v; wn *= w; } }
    }
}
static float half(std::uint16_t h) {
    const int s = h >> 15, e = (h >> 10) & 31, m = h & 1023; float v;
    if (e == 0) v = std::ldexp(float(m), -24); else if (e == 31) v = m ? NAN : INFINITY; else v = std::ldexp(float(m + 1024), e - 25);
    return s ? -v : v;
}
int main(int argc, char** argv) {
    using namespace piano_mvp_v4;
    if (argc < 2) { std::fprintf(stderr, "usage: piano-grand-bank-check grand_bank_ref.bin [maxErrDb] [maxLevelDb]\n"); return 2; }
    const double maxErr = argc > 2 ? std::atof(argv[2]) : -15.0, maxLevel = argc > 3 ? std::atof(argv[3]) : 0.5;
    std::size_t size = 0; const std::uint8_t* blob = embeddedGrandTable(size);
    GrandTable table; if (!blob || !table.parse(blob, size)) { std::fprintf(stderr, "no/invalid embedded grand table\n"); return 1; }
    std::ifstream in(argv[1], std::ios::binary); std::vector<char> raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (raw.size() < 20 || std::memcmp(raw.data(), "GREF", 4)) { std::fprintf(stderr, "bad reference\n"); return 1; }
    std::uint32_t nS, nP, nF; std::memcpy(&nS, &raw[4], 4); std::memcpy(&nP, &raw[8], 4); std::memcpy(&nF, &raw[12], 4);
    std::vector<std::uint32_t> sets(2 * nS), ports(nP); std::memcpy(sets.data(), &raw[20], 8 * nS); std::memcpy(ports.data(), &raw[20 + 8 * nS], 4 * nP);
    std::vector<float> scale(nS); std::memcpy(scale.data(), &raw[20 + 8 * nS + 4 * nP], 4 * nS);
    const char* base = raw.data() + 20 + 8 * nS + 4 * nP + 4 * nS;
    const double lo[] = {62.5, 125, 250, 500, 1000, 2000, 4000, 8000}, hi[] = {125, 250, 500, 1000, 2000, 4000, 8000, 16000};
    const std::size_t NF = 32768; std::vector<double> eRef(8, 0.0), eErr(8, 0.0), eTest(8, 0.0); double worstErr = -200, worstLevel = 0;
    double totRef = 0, totErr = 0;
    for (std::uint32_t s = 0; s < nS; ++s) {
        std::vector<double> sRef(8, 0.0), sErr(8, 0.0);
        GrandRadiation bank; if (!bank.configure(&table, 48000.0)) return 1;
        bank.setDirectEnabled(false); bank.request(int(sets[2 * s]), int(sets[2 * s + 1]));
        for (std::uint32_t p = 0; p < nP; ++p) {
            bank.reset(); for (int i = 0; i < 4000; ++i) { bank.step(); double l, r; bank.finish(l, r, nullptr); } bank.reset();
            std::vector<std::complex<double>> ref[2], tst[2]; for (int c = 0; c < 2; ++c) { ref[c].assign(NF, 0); tst[c].assign(NF, 0); }
            const std::uint8_t port = std::uint8_t(ports[p]), light = 0;
            for (std::uint32_t n = 0; n < nF; ++n) {
                if (n == 0) { const float g = 1.f; bank.accumulate(&port, &g, &light, 1); }
                bank.step(); double l, r; bank.finish(l, r, nullptr);
                tst[0][n] = 0.05 * l; tst[1][n] = 0.05 * r;
                const char* q = base + ((std::size_t(s) * nP + p) * nF + n) * 8;
                float h0, h1; std::memcpy(&h0, q, 4); std::memcpy(&h1, q + 4, 4); ref[0][n] = double(h0) * scale[s]; ref[1][n] = double(h1) * scale[s];
            }
            for (int c = 0; c < 2; ++c) {
                fft(ref[c]); fft(tst[c]);
                for (std::size_t k = 1; k < NF / 2; ++k) {
                    const double f = double(k) * 48000.0 / NF;
                    for (int b = 0; b < 8; ++b) if (f >= lo[b] && f < hi[b]) {
                        eRef[b] += std::norm(ref[c][k]); eTest[b] += std::norm(tst[c][k]); eErr[b] += std::norm(ref[c][k] - tst[c][k]);
                        sRef[b] += std::norm(ref[c][k]); sErr[b] += std::norm(ref[c][k] - tst[c][k]);
                    }
                }
            }
        }
        std::printf("set lid %u mic %u:", sets[2 * s], sets[2 * s + 1]);
        for (int b = 0; b < 8; ++b) std::printf(" %.1f", 10 * std::log10(sErr[b] / sRef[b] + 1e-30));
        std::printf("\n");
    }
    std::printf("band          err dB  level dB\n");
    for (int b = 0; b < 8; ++b) {
        const double e = 10 * std::log10(eErr[b] / eRef[b] + 1e-30), l = 10 * std::log10(eTest[b] / eRef[b] + 1e-30);
        std::printf("%6.0f-%-6.0f %7.1f %8.2f\n", lo[b], hi[b], e, l);
        worstErr = std::max(worstErr, e); worstLevel = std::max(worstLevel, std::fabs(l)); totRef += eRef[b]; totErr += eErr[b];
    }
    std::printf("total error %.1f dB re signal; worst band error %.1f dB, worst level difference %.2f dB\n", 10 * std::log10(totErr / totRef), worstErr, worstLevel);
    if (worstErr > maxErr || worstLevel > maxLevel) { std::printf("FAIL (limits %.1f dB, %.2f dB)\n", maxErr, maxLevel); return 1; }
    std::printf("PASS\n"); return 0;
}

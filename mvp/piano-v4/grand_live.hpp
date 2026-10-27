// SPDX-License-Identifier: MIT
// Piano 0.7c: the 0.7b grand body in real time. A table ("PGL2", made by mvp/piano-grand/build_live_table.py) holds
//  (a) the baked board radiation of each of the three lid states: a low range (to about 4 kHz) of complex one-pole resonators whose
//      residues are fitted per port and per microphone (the poles come from a subspace estimate on the 0.7b port-to-microphone impulse
//      responses), and a high range that is the exact statistical-mode set of the 0.7b board (real port participations, complex output
//      weights per microphone), shared by every lid and microphone;
//  (b) the direct radiation of the struck strings (complex gains of the 0.7b line-dipole model, pruned);
//  (c) the strings' board for Body Grand (the 0.7b air-loaded 144-mode table, installed through board_override.hpp).
// Nothing here allocates after configure(). setSet()/request() (lid / microphone) is real-time safe: it starts a short crossfade between
// two complete outputs; the low-range resonators of the new set start from rest (the shared high range keeps its state).
#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>
#include "board_override.hpp"
#include "kernels.hpp"

namespace piano_mvp_v4 {
inline constexpr int kGrandLids = 3, kGrandMics = 3, kGrandSets = 9, kGrandMicChannels = 6;   // lid: open half closed; preset: close player audience

// One string's pruned direct-radiation gains (vertical and horizontal polarisation): gains[entry][6 microphones][re, im] per unit
// analytic modal velocity (the 0.7b convention: p = Re[G (u/dt + j omega (q - u/2))]).
struct GrandDirectString {
    std::uint32_t nV{}, nH{};
    const std::uint16_t* idxV{}; const std::uint16_t* idxH{};
    const float* gV{}; const float* gH{};
};

class GrandTable {
public:
    // Parses a blob that stays alive for the table's lifetime (embedded in the binary, or owned by the caller). Non-audio.
    bool parse(const std::uint8_t* blob, std::size_t size);
    bool valid() const noexcept { return valid_; }
    std::array<std::uint32_t, kGrandLids> lfCount{};   // poles in use per lid (multiple of 8, at most lowPoles)
    std::uint32_t lowPoles{}, lowLight{}, hfModes{};  // P (multiple of 8), poles driven by unstruck strings (prefix), Nh (multiple of 32)
    float outScale{};                                  // pressure scale: out = 0.05 * outScale * (board + direct)
    const double *lfOmega{}, *lfSigma{};               // [lid][P] continuous poles a = -sigma + j omega (rad/s, 48 kHz fit)
    const float *lfRe{}, *lfIm{};                      // residues [set][ch][port][P]
    const double *hfOmega{}, *hfSigma{};               // [Nh]
    const float* hfPhi{};                              // port-major [port][Nh]
    const float *hfWre{}, *hfWim{}, *hfWe{};           // [set][ch][Nh]: complex output weight, and the DC-zero feedthrough on the drive
    const std::uint16_t* hfDelay{};                    // [set][ch] samples (48 kHz) applied to the high range
    std::array<std::int32_t, kGrandMicChannels> refDelay{};   // direct-radiation arrival offset per microphone, samples at 48 kHz
    std::array<std::array<GrandDirectString, 3>, 88> direct{};
    // The strings' board (Body Grand): installed through the BoardOverride hook at prepare().
    BoardOverride board;
    const float* re(int set, int ch, int port) const noexcept { return lfRe + ((static_cast<std::size_t>(set) * 2 + ch) * 88 + port) * lowPoles; }
    const float* im(int set, int ch, int port) const noexcept { return lfIm + ((static_cast<std::size_t>(set) * 2 + ch) * 88 + port) * lowPoles; }
private:
    bool valid_{};
};

// The bundled table (embedded in the binary); null size when the build has none.
const std::uint8_t* embeddedGrandTable(std::size_t& size) noexcept;

class GrandRadiation {
public:
    // Non-audio. Allocates.
    bool configure(const GrandTable* table, double fs);
    void reset() noexcept;
    const GrandTable* table() const noexcept { return table_; }
    // ---- per sample (helper thread or audio thread; one writer at a time)
    void accumulate(const std::uint8_t* port, const float* g, const std::uint8_t* light, int count) noexcept;
    void step() noexcept;
    // Combines the board and the direct terms of the sample; direct[4] = values for the slot microphones (see slots()).
    void finish(double& left, double& right, const float* direct) noexcept;
    // Batched path of the radiation helper: up to kBatch consecutive records (active ports and their drives, `port | 0x80` marks an unstruck
    // string) are accumulated together (each port's residue rows are read once per batch), then stepped one after the other. The arithmetic
    // per record is that of accumulate() + step() (ports in ascending order), so the output is bit-identical.
    static constexpr int kBatch = 16;
    struct Record { const std::uint8_t* keys; const double* g; unsigned count; };
    void loadBatch(const Record* recs, int n) noexcept;
    void stepRecord(int m) noexcept;
    // ---- set selection (audio thread, at a block boundary with the radiation drained)
    void request(int lid, int mic) noexcept;
    int lid() const noexcept { return lid_; }
    int mic() const noexcept { return mic_; }
    // Microphones (0..5) whose direct radiation the voices must compute now: slot 0, 1 = the current preset, 2, 3 = the target
    // preset while fading (-1 when unused).
    const std::array<int, 4>& slots() const noexcept { return slot_; }
    bool fading() const noexcept { return fading_; }
    static int presetMic(int preset, int ch) noexcept { return preset * 2 + ch; }
    // Upper bound of the output amplitude (idle detection), as GrandBoard::outputBound.
    double outputBound() const noexcept;
    void setDirectEnabled(bool on) noexcept { directOn_ = on; }
    bool directEnabled() const noexcept { return directOn_; }
    std::size_t lowPoleCount() const noexcept { return P_; }
    std::size_t highModeCount() const noexcept { return hf_->n; }
    // High-range density: 1 = all of the statistical modes (exact against the 0.7b responses), 3 = every third mode with the output weights
    // scaled by sqrt(3) (the same band energies, a different noise realisation; about a third of the cost of that part). Applied by request()
    // at a block boundary; the high range restarts from rest when it changes.
    void setHighStride(int stride) noexcept { strideReq_.store(stride == 3 ? 3 : 1, std::memory_order_relaxed); }
    int highStride() const noexcept { return hf_ == &dec_ ? 3 : 1; }
private:
    static constexpr std::size_t ringN = 1024;                 // power of two > longest delay
    struct Stream {
        int set{}, lid{}, mic{};
        std::vector<double> xre, xim;                          // [2 channels * P] low-range states
        std::vector<float> dre, dim;                           // [2 * P] drive accumulators
        std::array<double, 2> ff{};                            // high-range feedthrough of the current sample
        std::array<std::array<float, ringN>, 2> hf{};          // high-range sums per channel
        std::array<std::array<float, ringN>, kGrandMicChannels> dir{};   // direct per microphone
        std::size_t cursor{};
    };
    const GrandTable* table_{};
    std::size_t P_{}, Nh_{};
    double fs_{48000}, ratio_{1};
    std::vector<double> lamRe_, lamIm_;                        // [lid][P]
    struct HfBank {
        std::size_t n{};
        const float *phi{}, *wre{}, *wim{}, *we{};            // phi [port][n]; weights [set][ch][n]
        std::vector<float> ownPhi, ownWre, ownWim, ownWe;      // storage of the decimated bank
        std::vector<float> lamRe, lamIm, xre, xim, drive;
    } full_, dec_;
    HfBank* hf_{};
    std::atomic<int> strideReq_{1};
    std::unique_ptr<Stream> s0_, s1_;
    int lid_{0}, mic_{0};
    bool fading_{}; std::size_t fadePos_{}, fadeLen_{960};
    std::array<int, 4> slot_{-1, -1, -1, -1};
    bool directOn_{true};
    std::vector<float> batchLf_, batchHf_;                     // [stream][ch][plane][kBatch][P] and [kBatch][maxHf]
    std::size_t maxHf_{};
    std::array<Stream*, 2> bStream_{}; int bStreams_{1};
    float* lfBuf(int stream, int ch, int plane, int m) noexcept { return batchLf_.data() + ((((static_cast<std::size_t>(stream) * 2 + ch) * 2 + plane) * kBatch) + m) * P_; }
    void promote() noexcept;
    void accumulateStream(Stream& s, const std::uint8_t* port, const float* g, const std::uint8_t* light, int count) noexcept;
    void outputOf(Stream& s, const float* direct, double& left, double& right) noexcept;
};
}

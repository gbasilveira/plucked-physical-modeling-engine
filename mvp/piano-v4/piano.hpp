// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>
#include "pme/exciters/hammer.hpp"
#include "pme/bridge/terms.hpp"
#include "soundboard.hpp"
#include "grand_board.hpp"
#include "sympathetic_string.hpp"
#include "bridge_mass.hpp"
#include "tension_note.hpp"
#include "pool.hpp"
#include "kernels.hpp"
#include <atomic>
#include <cstdint>

namespace piano_mvp_v4 {
// Experimental fixed preset. All allocations happen in prepare(), never during playback.
class Piano {
public:
    static constexpr int maxVoices = 12;        // real-time default (CPU)
    static constexpr int maxPolyphony = 88;     // one voice per key: no stealing
    // 0.5.0 assumption: dampers cover keys below F#6 (MIDI 90); F#6 and above are undamped,
    // as on most grands (stated range varies, roughly the top 1.5 octaves). Not measured.
    static constexpr int firstUndampedKey = 90;
    // Quality presets (0.5.4). Live: the real-time caps and quiet-string savings tuned for >= 2x
    // real time on the worst-case clusters at 128-sample blocks. Balanced: in between. Full: the
    // complete offline model (88 voices, no caps, tension on all keys, no truncation, exact sums).
    enum class Quality : int { Live = 0, Balanced = 1, Full = 2 };
    // Real-time safe at any time on the audio thread (no allocation): see README 0.5.4 for what
    // happens to sounding notes. Needs prepare() with tension configured for every key to get the
    // Full tension model (setTensionMaxKey(108), the default); otherwise tension keys stay as prepared.
    void setQuality(Quality q) noexcept;
    Quality quality() const noexcept { return quality_; }
    // Preset values (public for tests and documentation).
    struct Preset { int voices, sympathetic, leaky, tensionMaxKey; double truncation; bool fast; double sleepForce, sleepSeconds, tensionFloor; bool idleSkip; };
    static const Preset& preset(Quality q) noexcept;
    // setQuality with explicit values (measurements; `q` is the level reported by quality()).
    void applyPreset(Quality q, const Preset& p) noexcept;
    // Worker threads (0.5.4), before prepare(): number of helper threads created in prepare()
    // (0 = single thread, the default). The audio thread always takes part. Results are bit-identical
    // for any thread count.
    bool setWorkerThreads(int n) noexcept {
        if (ready_ || n < 0 || n > WorkerPool::kMaxWorkers) return false;
        workerThreads_ = n; activeWorkers_ = n; return true;
    }
    // Runtime (audio thread): how many of the created workers take part, 0..workerThreads().
    void setActiveWorkers(int n) noexcept;
    // ---- Block interface (0.5.4), used by the plug-ins ----
    // The radiation of the board never feeds back into the strings, so with a worker thread the
    // audio thread steps the strings and hands per-sample port drives to a radiation thread through a
    // ring; the output of the block is complete at blockEnd(). Results are bit-identical to calling
    // sample() for every frame. frames <= maxBlockFrames per begin/end pair.
    static constexpr std::uint32_t maxBlockFrames = 256;
    void blockBegin(std::uint32_t frames) noexcept;
    void blockStep() noexcept;                       // one frame: events were dispatched before it
    void blockEnd(float* left, float* right) noexcept;      // fills `frames` outputs of the block
    void blockEnd(double* left, double* right) noexcept;
    bool radiationThreadActive() const noexcept { return radThread_.running(); }
    // Non-audio. Joins the helper threads (deactivate); startThreads() re-creates them (activate).
    // While they are stopped everything runs on the calling thread with identical output.
    void stopThreads() noexcept;
    bool startThreads();
    // Non-audio: renders a few hundred milliseconds of notes in the current quality and resets, so that
    // code pages, kernel dispatch and helper threads are warm before the first real note (a cold first
    // note after idle cost up to 20 ms in measurements). Call after prepare()/setQuality().
    void warmUp() noexcept;
    int activeWorkers() const noexcept { return pool_.active(); }
    int workerThreads() const noexcept { return pool_.workers(); }
    static int defaultWorkerThreads() noexcept;   // min(3, hardware threads - 1)
    std::uint64_t stolenTasks() const noexcept { return pool_.stolenTasks(); }
    std::uint64_t tasksRunBy(int slot) const noexcept { return pool_.tasksRunBy(slot); }
    std::uint64_t poolStalls() const noexcept { return pool_.stalls(); }
    // Idle skip (0.5.4, Live and Balanced): once no string is simulated and the board and radiation state
    // are below idleThreshold (an amplitude bound of the output, about -186 dBFS), frames are zeros at
    // a negligible cost until the next note-on. Full never skips (bit-exact tails).
    static constexpr double idleThreshold = 1e-9;
    static constexpr int idleHoldSamples = 4800;       // no simulated string for this long first
    bool idle() const noexcept { return idle_; }
    // 0.5.4.1: number of times the audio thread had to wait more than 0.4 ms for the radiation helper and
    // switched the radiation to the audio thread for a while (output is bit-identical either way).
    std::uint64_t radiationStalls() const noexcept { return radStalls_; }
    bool radiationOnHelper() const noexcept { return radOn_; }
    // 0.5.4.1 adaptive load shedding (Live and Balanced only, off by default; the plug-ins switch it on).
    // The audio thread times every block against its deadline. When the smoothed cost is above 0.7 of the
    // deadline (or a block exceeds it) the model sheds work in steps (level 1..3, table in piano.cpp: fewer
    // struck voices, sympathetic and leaky strings, then no tension modulation) instead of missing deadlines;
    // it restores the preset after silence or after 4 s below 0.2. The output then depends on timing, so
    // renders, tests and Full leave it off.
    void setAdaptive(bool on) noexcept { adaptive_ = on; if (!on && shed_ != 0) { shed_ = 0; applyShed(); } }
    int shedLevel() const noexcept { return shed_; }
    std::uint64_t shedEvents() const noexcept { return shedEvents_; }
    void setStallFallback(bool on) noexcept { stallFallback_ = on; }
    // Tests only: the radiation helper sleeps this long (microseconds) while holding the lock after each
    // batch, as if it were descheduled there.
    void testHelperHold(unsigned microseconds) noexcept { testHelperHoldUs_.store(microseconds, std::memory_order_relaxed); }
    // Per-key held-note count (0.5.4): a note-off releases the key only when every overlapping note-on
    // of the key has been released (default on; off is the 0.5.3 single flag).
    bool setHeldNoteCount(bool enabled) noexcept {
        if (ready_) return false;
        heldCount_on_ = enabled; return true;
    }
    int sleepSamples() const noexcept { return static_cast<int>(sleepSamples_); }
    bool prepare(double sampleRate);
    // Set before prepare(), while playback is stopped.
    bool setBridgeFeedback(bool enabled) noexcept {
        if (ready_) return false;
        feedback_ = enabled; return true;
    }
    bool setUnisonStrings(bool enabled) noexcept {
        if (ready_) return false;
        unisons_ = enabled; return true;
    }
    bool setResidualBridgeLoss(bool enabled) noexcept {
        if (ready_) return false;
        residualLoss_ = enabled; return true;
    }
    bool setSoftFelt(bool enabled) noexcept {
        if (ready_) return false;
        softFelt_ = enabled; return true;
    }
    // 0.4.9: both default on (see README). Bridge-top mass: low-pass force transmission.
    bool setBridgeMassLoading(bool enabled) noexcept {
        if (ready_) return false;
        bridgeMass_ = enabled; return true;
    }
    // String viscosity saturates above kStringLossCornerHz.
    bool setSaturatingStringLoss(bool enabled) noexcept {
        if (ready_) return false;
        saturatingLoss_ = enabled; return true;
    }
    // 0.5.0, before prepare(). Voice limit 1..88 (default 12); 88 never steals.
    bool setVoiceLimit(int n) noexcept {
        if (ready_ || n < 1 || n > maxPolyphony) return false;
        voiceLimit_ = n; return true;
    }
    // Una corda (CC67 >= 64): applied to notes struck while down; default on (see README).
    bool setUnaCorda(bool enabled) noexcept {
        if (ready_) return false;
        unaCordaEnabled_ = enabled; return true;
    }
    // Keys >= firstUndampedKey ignore note-off damping (they decay naturally).
    bool setUndampedTreble(bool enabled) noexcept {
        if (ready_) return false;
        undampedTreble_ = enabled; return true;
    }
    // 0.5.1, all before prepare(); each default on and independent (see README 0.5.1).
    // Shared board: one bidirectionally coupled soundboard for all strings (needs bridge
    // feedback; off reproduces the 0.5.0 private per-key bridges bit-for-bit).
    bool setSharedBoard(bool enabled) noexcept {
        if (ready_) return false;
        sharedBoard_ = enabled; return true;
    }
    // Key-dependent bridge points on the bass/treble bridge curve (off: 0.5.0 pan blend).
    // Only affects the shared board.
    bool setGrandBridgeGeometry(bool enabled) noexcept {
        if (ready_) return false;
        grandGeometry_ = enabled; return true;
    }
    // Simulate unstruck strings whose dampers are lifted (pedal, held key, undamped treble) when
    // the board moves their bridge point. Only affects the shared board.
    bool setSympatheticStrings(bool enabled) noexcept {
        if (ready_) return false;
        sympathetic_ = enabled; return true;
    }
    // Largest number of simultaneously simulated unstruck strings, 0..88 (default 88).
    bool setSympatheticLimit(int n) noexcept {
        if (ready_ || n < 0 || n > maxPolyphony) return false;
        sympLimit_ = n; return true;
    }
    // 0.5.2, before prepare(). Kirchhoff-Carrier tension modulation of the struck strings (pitch
    // glide, phantom partials; default on). Off reproduces 0.5.1 bit-for-bit.
    bool setTensionModulation(bool enabled) noexcept {
        if (ready_) return false;
        tension_ = enabled; return true;
    }
    // Tension modulation only for keys 21..n (CPU lever, default 108 = all keys). See README 0.5.2.
    bool setTensionMaxKey(int n) noexcept {
        if (ready_ || n < 20 || n > 108) return false;
        tensionMaxKey_ = n; return true;
    }
    // 0.5.3, all before prepare(). Leaky dampers: strings under a felt damper are not removed from the
    // system; damped, unstruck strings are simulated like sympathetic ones but with the damper's
    // added modal damping (default on; off reproduces 0.5.2 bit-for-bit). Needs the shared board.
    bool setLeakyDampers(bool enabled) noexcept {
        if (ready_) return false;
        leaky_ = enabled; return true;
    }
    // Damper law: added amplitude decay rate sigma_d(f) = rate (f/damperRefHz)^(-exponent), 1/s.
    // rate > 0, exponent in [-2, 2]. Only used when leaky dampers are on.
    bool setDamperLaw(double rate, double exponent) noexcept {
        if (ready_ || !(rate > 0) || !(rate < 1e4) || !(exponent >= -2 && exponent <= 2)) return false;
        damperRate_ = rate; damperExponent_ = exponent; return true;
    }
    // Largest number of simultaneously simulated damped (leaking) unstruck strings, 0..88 (default 88).
    bool setLeakyLimit(int n) noexcept {
        if (ready_ || n < 0 || n > maxPolyphony) return false;
        leakyLimit_ = n; return true;
    }
    // Use the same damper law when a struck key is released (note-off without pedal), replacing the
    // uniform exp(-35/fs) step. Default OFF: the fitted law (2/s) is 17x weaker than the release
    // step, so notes would ring on for seconds after note-off (README 0.5.3).
    bool setLeakyRelease(bool enabled) noexcept {
        if (ready_) return false;
        leakyRelease_ = enabled; return true;
    }
    static constexpr double damperRefHz = 200.0;
    static constexpr double defaultDamperRate = 2.0;
    // Plug-in cap on damped (leaking) strings (CPU, README 0.5.3).
    static constexpr int pluginLeakyLimit = 4;
    // Plug-in cap on sympathetic strings (CPU, README 0.5.1).
    static constexpr int pluginSympatheticLimit = 8;
    // Plug-in tension modulation boundary (CPU, README 0.5.2): keys 21..47 only. With all keys the
    // pedalled 12-key cluster runs at 0.50x real time, with this boundary at 0.97x.
    static constexpr int pluginTensionMaxKey = 47;
    // Board bridge-point speed (m/s) above which a lifted, idle string starts to be simulated,
    // and below which (half of it) an idle sympathetic string may go back to sleep. Assumption.
    static constexpr double wakeVelocity = 1e-4;
    // Unstruck strings keep only vertical modes below this (the board is coupled up to 1.87 kHz).
    static constexpr double sympatheticCutHz = 2000.0;
    // Lift the damper of a key without striking it (diagnostic/test; noteOff releases it).
    void liftDamper(int key) noexcept {
        if (ready_ && key >= 21 && key <= 108) voices_[key - 21].held = true;
    }
    static int stringsForKey(int key) noexcept {
        return key <= 32 ? 1 : (key <= 51 ? 2 : 3);
    }
    static double feltStiffnessScale(int key) noexcept {
        if (key<=72) return 0.35;
        if (key>=84) return 1.0;
        const double t=(key-72)/12.0;
        return 0.35+0.65*t*t*(3-2*t);
    }
    // ---- 0.5.5 Voices setting ----
    // Struck-voice limit independent of Quality: 0 = Auto (the quality's own cap: Live 16, Balanced 24), else
    // 1..88. Explicit values apply to Live and Balanced; Full always has 88 (never steals). The sympathetic and
    // leaky caps of the quality scale with sqrt(N / auto voices), rounded up, at most 88 (README 0.5.5). Real-time
    // safe (changes limits only); sounding notes keep ringing, a lower limit takes effect at following note-ons.
    void setVoices(int n) noexcept { voicesSetting_ = std::clamp(n, 0, maxPolyphony); applyLimits(); }
    int voices() const noexcept { return voicesSetting_; }
    int voiceLimit() const noexcept { return voiceLimit_; }
    int sympatheticLimit() const noexcept { return sympLimit_; }
    int leakyLimit() const noexcept { return leakyLimit_; }
    // Steal fade (samples): a stolen voice is faded to -80 dB over this time instead of cut.
    int stealFadeSamples() const noexcept { return fadeSamples_; }
    // Test hook: fade length in samples (1 = effectively a cut). Resets to 8 ms at prepare().
    void setStealFadeSamples(int n) noexcept { fadeSamples_ = std::max(1, n); fadeStep_ = std::exp(std::log(1e-4) / fadeSamples_); }
    // ---- 0.5.5 imperfections (README "Update 0.5.5") ----
    // Amount 0..2 (0 = exactly 0.5.4, 1 = default, 2 = twice the assumed deviations). Real-time safe at any
    // time; a key takes the new amount at its next note-on (its strings are retuned then; no allocation).
    static constexpr double defaultImperfection = 1.0, maxImperfection = 2.0;
    void setImperfection(double amount) noexcept {
        imperfection_.store(std::isfinite(amount) ? std::clamp(amount, 0.0, maxImperfection) : 0.0, std::memory_order_relaxed);
    }
    double imperfection() const noexcept { return imperfection_.load(std::memory_order_relaxed); }
    // Per-key deterministic plan (a hash of the key index; the same on every thread, block size and run).
    // Offsets are those of amount 1; the amount scales the change from the 0.5.4 values.
    struct ImperfectionPlan {
        std::array<double, 3> baseCents{}, cents{};   // unison offsets (cents): 0.5.4 symmetric 0.8 spacing, and amount 1
        std::array<double, 3> strengthDev{};          // contact strength deviation (fraction, +-0.05..0.15)
        std::array<double, 3> lagSeconds{};           // contact timing lag of each string (s), min 0
        int falseCount{};                             // split partials
        std::array<int, 4> falseMode{}, falseString{};// mode index (partial - 1), string
        std::array<double, 4> falseHz{};              // frequency shift of that partial on that string (Hz)
    };
    const ImperfectionPlan& imperfectionPlan(int key) const noexcept { return plans_[key - 21]; }
    static ImperfectionPlan makeImperfectionPlan(int key, int strings, std::size_t modes) noexcept;
    // ---- 0.6 baked radiation (README "Update 0.6") ----
    // Radiation stage: Modal = the 512-mode bank (bit-exact reference), Baked = the reduced bank (zero-weight and
    // negligible-energy modes removed, error about -85 dB rms, ~3x cheaper), Auto = Baked for Live and Balanced,
    // Modal for Full. Real-time safe at any time on the audio thread; it takes effect at the next block (or
    // sample) and clears the state of the bank taking over (a sounding radiation tail of the other bank is lost).
    enum class Radiation : int { Auto = 0, Modal = 1, Baked = 2 };
    void setRadiation(Radiation r) noexcept { radiationSetting_.store(static_cast<int>(r), std::memory_order_relaxed); }
    Radiation radiation() const noexcept { return static_cast<Radiation>(radiationSetting_.load(std::memory_order_relaxed)); }
    bool bakedRadiationActive() const noexcept { return board_.baked(); }
    // Before prepare(): dropped-energy tolerance of the baked bank (default kBakedDefaultTolerance).
    bool setBakedTolerance(double t) noexcept {
        if (ready_ || !(t >= 0 && t < 1)) return false;
        bakedTolerance_ = t; return true;
    }
    // Tools: the radiation records of the sample just stepped (sample() / blockStep(): ports with a drive).
    int radiationPortCount() const noexcept { return nPorts_; }
    int radiationPort(int k) const noexcept { return ports_[k]; }
    double radiationDrive(int k) const noexcept { const auto& v = voices_[ports_[k]]; return v.radiated ? v.g : 0.0; }
    bool radiationLight(int k) const noexcept { return voices_[ports_[k]].radModes == kLightRadiatingModes; }
    void reset() noexcept;
    void noteOn(int key, double velocity) noexcept;
    void noteOff(int key) noexcept;
    // All keys released at once (MIDI CC123): clears the held-note counts.
    void allNotesOff() noexcept { heldCount_.fill(0); for (auto& v : voices_) v.held = false; }
    void sustain(bool down) noexcept;
    void softPedal(bool down) noexcept { unaCordaDown_ = unaCordaEnabled_ && down; }
    bool softPedalDown() const noexcept { return unaCordaDown_; }
    void sample(float& left, float& right) noexcept;
    int activeVoices() const noexcept;             // struck (hammer-driven) voices
    int sympatheticVoices() const noexcept;        // simulated but never struck since waking
    const GrandBoard& board() const noexcept { return board_; }
    // 0.7b offline probe (grand_render, direct string radiation): calls f(key, string, activeModes, omega, qV, uV, qH, uH) for every
    // struck voice. q is the modal displacement and u = q^n - q^{n-1} (SPEC-OSC difference form), so the velocity is u / dt at n - 1/2.
    // Read-only; nothing in the audio path calls it, so the 0.6 arithmetic is unchanged.
    template <class F> void visitStrings(F&& f) const {
        for (int k = 0; k < 88; ++k) {
            const Voice& v = voices_[k];
            if (!v.active || !v.struck) continue;
            for (std::size_t j = 0; j < v.note.stringCount(); ++j) {
                const auto off = v.note.modeOffset(j);
                f(k, static_cast<int>(j), v.note.activeModes(j), v.modes[j].omega.data(), v.note.segment().q() + off, v.note.segment().u() + off,
                  v.note.horizontalQ(j), v.note.horizontalU(j));
            }
        }
    }
    // 0.7b diagnostics: is the key's voice a struck one (false: unstruck, excited only through the board)?
    bool voiceStruck(int key) const noexcept { return key >= 21 && key <= 108 && voices_[key - 21].struck; }
    // Unstruck (sympathetic / leaky-damped-by-the-light-model) strings: f(key, string, modes, omega, q, u), vertical polarisation only.
    template <class F> void visitLightStrings(F&& f) const {
        for (int k = 0; k < 88; ++k) {
            const Voice& v = voices_[k];
            if (!v.active || v.struck) continue;
            for (std::size_t j = 0; j < v.light.strings(); ++j)
                f(k, static_cast<int>(j), v.light.modeCount(j), v.modes[j].omega.data(), v.light.q(j), v.light.u(j));
        }
    }
    // Compiled data of the strings of a key (after prepare): count, speaking length, number of modes, omega.
    int keyStringCount(int key) const noexcept { return stringsForKey(key); }
    const pme::StringModes& compiledString(int key, int j) const noexcept { return voices_[key - 21].modes[j]; }
    bool sharedBoardActive() const noexcept { return shared_; }
    bool voiceActive(int key) const noexcept { return key >= 21 && key <= 108 && voices_[key-21].active; }
    // Last bridge force (N) this key's strings put on the board (shared board only).
    double portForce(int key) const noexcept { return key >= 21 && key <= 108 ? voices_[key-21].force : 0.0; }
    std::uint64_t numericalFaults() const noexcept { return numericalFaults_; }
    std::uint64_t voiceSteals() const noexcept { return voiceSteals_; }
    double peakBridgeMotion() const noexcept { return peakBridgeMotion_; }
    pme::exciters::HammerDiagnostics hammerDiagnostics(int key) const noexcept {
        return key>=21 && key<=108 ? voices_[key-21].note.diagnostics()
                                  : pme::exciters::HammerDiagnostics{};
    }
private:
    struct Voice {
        std::array<pme::StringModes, 3> modes;
        std::array<pme::SegmentBridgeTerms, 3> terms;
        TensionStruckNote note;           // 0.5.2: StruckNote + tension modulation (tension_note.hpp)
        pme::bridge::BridgePoint<double> bridge;
        bool active{}, held{}, undamped{}, struck{}, sympathetic{};
        double force{};     // F^n of this voice's strings on the shared board (0.5.1)
        SympatheticString light;        // unstruck-string model (sympathetic voices)
        std::array<std::vector<double>, 3> leak;   // 0.5.3 per-mode damper step factors, struck vertical modes
        double lightW{}, dwPrev{};      // its bridge displacement since waking; last board increment
        std::uint64_t age{}, quiet{};
        double release{1.0}, pan{};
        // 0.5.4 per-sample hand-over between the pool phases
        double g{}, peakMotion{}, peakForce{}, slotCost{};
        unsigned radModes{};
        int slot{-1}, sinceTruncation{};
        bool radiated{}, faulted{};
        double tuneApplied{};           // 0.5.5: imperfection amount the compiled coefficients carry (0 = as compiled)
        double env{};                   // 0.5.5 decaying peak of |force| (steal policy)
        std::uint64_t sinceOn{};        // samples since the note-on (steal protection)
        int fade{};                     // >0: being stolen, samples left of the fade-out
    };
    void applyImperfection(Voice& v, int key) noexcept;
    std::atomic<int> radiationSetting_{0};
    double bakedTolerance_{kBakedDefaultTolerance};
    void syncRadiation() noexcept {
        const int r = radiationSetting_.load(std::memory_order_relaxed);
        const bool want = r == static_cast<int>(Radiation::Baked) || (r == static_cast<int>(Radiation::Auto) && quality_ != Quality::Full);
        if (want != board_.baked()) board_.setBaked(want);
    }
    std::array<ImperfectionPlan, 88> plans_{};
    std::atomic<double> imperfection_{0.0};
    std::vector<double> ratioScratch_;
    int voicesSetting_{0}, fadeSamples_{384};
    double fadeStep_{1.0}, envDecay_{0.999};
    Preset basePreset_{16, 12, 6, 40, 1e-6, true, 1e-6, 0.5, 1e-10, true};
    void applyLimits() noexcept;
    Voice* pickSteal() noexcept;
    // Voice phase of the pool (see stringsStep).
    static void voiceTask(void* self, int k);
    void voiceStep(int port) noexcept;
    void stringsStep() noexcept;                     // wake, coupled board, all strings of one sample
    void radiateInline(float& left, float& right) noexcept;   // radiation of the sample just stepped
    void resetAfterNonFinite() noexcept;
    bool radOn_{};                                       // radiation on the helper right now
    // 0.5.4.1: stall fallback. If the audio thread has to wait for the helper longer than kStallNs at the end
    // of a block (the helper was descheduled while holding radLock_), radiation runs inline for a while.
    bool radRequested_{}, stallFallback_{true}, adaptive_{};
    int shed_{};
    std::uint64_t shedEvents_{}, shedHold_{}, shedCalm_{};
    double costEma_{};
    std::chrono::steady_clock::time_point blockT0_{};
    void applyShed() noexcept;
    void governBlock() noexcept;
    std::uint64_t fallbackLeft_{}, healthyFrames_{}, backoffFrames_{}, radStalls_{};
    static constexpr std::int64_t kStallNs = 400'000;
    void assignSlots() noexcept;
    double voiceCost(const Voice& v) const noexcept;
    WorkerPool pool_;
    // Radiation pipeline: sample records (active ports and their drive) -> radiation thread.
    static constexpr std::uint32_t ringSize = 1024;      // records; power of two, >= 2 * maxBlockFrames
    struct Ring {
        std::vector<std::uint8_t> keys, count;           // [rec*88 + k] port | 0x80 for unstruck; [rec]
        std::vector<double> g;                           // [rec*88 + k]
        std::vector<float> outL, outR;                   // [rec]
    } ring_;
    alignas(64) std::atomic<std::uint64_t> produced_{0};
    alignas(64) std::atomic<std::uint64_t> consumed_{0};
    alignas(64) std::atomic<std::uint32_t> radLock_{0};
    std::atomic<bool> radFault_{false};
    std::atomic<unsigned> audioCsr_{0};
    std::uint64_t blockStart_{0};
    std::uint32_t blockFrames_{0};
    std::vector<float> blockL_, blockR_;                 // single-thread block output
    static bool radiationPump(void* self) noexcept;      // worker: process pending records
    bool pumpRecords(std::uint32_t maxRecords, unsigned holdUs = 0) noexcept; // takes the lock; false if nothing was done
    std::atomic<unsigned> testHelperHoldUs_{0};          // tests: the helper keeps the lock this long per batch (a descheduled helper)
    void radiateRecord(std::uint64_t index) noexcept;
    void publishSkipped(std::uint64_t n) noexcept;      // advance produced_/consumed_ together, under the lock
    PipelineThread radThread_;
    int workerThreads_{0}, activeWorkers_{0};
    std::array<int, 88> ports_{};
    int nPorts_{};
    std::array<std::uint8_t, WorkerPool::kMaxTasks> owner_{};
    std::array<double, WorkerPool::kMaxWorkers + 1> slotLoad_{};
    bool slotsDirty_{true};
    std::array<std::uint8_t, 88> heldCount_{};
    bool heldCount_on_{true};
    Quality quality_{Quality::Full};
    double truncation_{0.0}, sleepForce_{1e-7}, sleepSeconds_{1.0}, tensionFloor_{1e-14};
    bool qualitySet_{}, idleSkip_{}, idle_{};
    int idleCount_{};
    bool idleStep() noexcept;       // true (and idle_) when the frame can be skipped
    void trackIdle() noexcept;
    void checkIdle() noexcept;
    std::uint64_t sleepSamples_{48000};
    bool fast_{false};
    int tensionLimit_{108};
    std::array<Voice, 88> voices_;
    piano_mvp_v3::Soundboard soundboard_;
    GrandBoard board_;
    bool sharedBoard_{true}, grandGeometry_{true}, sympathetic_{true}, shared_{};
    int sympLimit_{maxPolyphony};
    std::uint64_t sampleClock_{};
    void sampleShared(float& left, float& right) noexcept;
    void wakeSympathetic() noexcept;
    bool isDamped(const Voice& v) const noexcept { return !v.held && !pedal_ && !v.undamped; }
    void primeBridge(Voice& v, int port) noexcept;
    std::vector<float> coincidence_;   // [candidate*88 + sounding] harmonic coincidence score
    bool feedback_{true};
    bool unisons_{true};
    bool residualLoss_{true};
    bool softFelt_{true};
    bool bridgeMass_{true};
    bool saturatingLoss_{true};
    bool tension_{true};
    bool leaky_{true}, leakyRelease_{false}, leakyActive_{};
    double damperRate_{defaultDamperRate}, damperExponent_{0.0};
    int leakyLimit_{maxPolyphony};
    int tensionMaxKey_{108};
    bool unaCordaEnabled_{true}, undampedTreble_{true}, unaCordaDown_{};
    int voiceLimit_{maxVoices};
    BridgeMassTransmission bridgeMassFilter_;
    double fs_{48000.0}, releaseStep_{}, peakBridgeMotion_{};
    std::uint64_t clock_{};
    std::uint64_t numericalFaults_{};
    std::uint64_t voiceSteals_{};
    bool pedal_{}, ready_{};
};
}

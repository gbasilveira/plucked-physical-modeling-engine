// SPDX-License-Identifier: MIT
// 0.6 step 2: renders one note in Full (modal radiation, no pedal) and captures the bridge force of its port and the
// stereo output. Writes PREFIX.force.f64 (raw doubles, N per sample, 48 kHz), PREFIX.wav (32-bit float stereo), and the same with
// the sympathetic and leaky strings off: PREFIX.iso.wav, PREFIX.iso.force.f64.
//   piano-baked-capture KEY MIDI_VELOCITY PREFIX [SECONDS = 8]
#include "common.hpp"
using pb::Piano;
int main(int argc, char** argv) {
    try {
        if (argc < 4) throw std::runtime_error("usage: piano-baked-capture KEY MIDI_VELOCITY PREFIX [SECONDS]");
        const int key = std::stoi(argv[1]), vel = std::stoi(argv[2]);
        const double seconds = argc > 4 ? std::stod(argv[4]) : 8.0;
        const int frames = int(seconds * pb::kFs);
        Piano p; pb::configure(p, Piano::Quality::Full, Piano::Radiation::Modal, 0);
        std::vector<pb::Ev> ev{{10, {144, std::uint8_t(key), std::uint8_t(vel)}}, {frames - 2, {128, std::uint8_t(key), 0}}};
        std::vector<double> force(frames);
        const auto out = pb::render(p, ev, frames, [&](int i) { force[i] = p.portForce(key); });
        std::ofstream f(std::string(argv[3]) + ".force.f64", std::ios::binary);
        f.write(reinterpret_cast<const char*>(force.data()), std::streamsize(force.size() * sizeof(double)));
        pb::writeWav(std::string(argv[3]) + ".wav", out, false);
        {   // struck string only: no sympathetic and no leaky strings (their radiation is what a force-only voice cannot reproduce)
            Piano q; q.setSympatheticStrings(false); q.setLeakyDampers(false);
            pb::configure(q, Piano::Quality::Full, Piano::Radiation::Modal, 0);
            std::vector<double> f2(frames);
            const auto iso = pb::render(q, ev, frames, [&](int i) { f2[i] = q.portForce(key); });
            pb::writeWav(std::string(argv[3]) + ".iso.wav", iso, false);
            std::ofstream g(std::string(argv[3]) + ".iso.force.f64", std::ios::binary);
            g.write(reinterpret_cast<const char*>(f2.data()), std::streamsize(f2.size() * sizeof(double)));
        }
        double peak = 0; for (float x : out.l) peak = std::max(peak, double(std::abs(x)));
        std::printf("key %d vel %d: %d frames, output peak %.4f\n", key, vel, frames, peak);
    } catch (const std::exception& e) { std::fprintf(stderr, "%s\n", e.what()); return 1; }
}

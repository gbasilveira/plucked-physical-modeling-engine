// SPDX-License-Identifier: MIT
// 0.6 mode-selection measurement: renders a scenario in Full with the modal radiation (sample() path, one thread)
// and writes the radiated energy of each of the 512 board modes (sum over samples of termL^2 + termR^2).
//   piano-baked-select SCENARIO OUT.txt      SCENARIO = events:FILE:FROM:TO | sweep | note:KEY:VEL
#include "common.hpp"
#include <cstdlib>
#include <sstream>
using pb::Piano;
int main(int argc, char** argv) {
    try {
        if (argc != 3) throw std::runtime_error("usage: piano-baked-select SCENARIO OUT.txt");
        std::string sc = argv[1]; std::vector<pb::Ev> ev; int frames;
        if (sc.rfind("events:", 0) == 0) {
            std::vector<std::string> t; std::stringstream ss(sc); std::string x; while (std::getline(ss, x, ':')) t.push_back(x);
            const double a = std::stod(t[2]), b = std::stod(t[3]);
            ev = pb::excerpt(pb::readEvents(t[1]), a, b); frames = int((b - a + 2) * pb::kFs);
        } else if (sc == "sweep") { ev = pb::sweepEvents(); frames = ev.back().frame + 4 * pb::kFs; }
        else if (sc.rfind("note:", 0) == 0) { std::stringstream ss(sc.substr(5)); std::string k, v; std::getline(ss, k, ':'); std::getline(ss, v, ':'); ev = pb::noteEvents(std::stoi(k), std::stod(v), 3.0); frames = 8 * pb::kFs; }
        else throw std::runtime_error("bad scenario");
        Piano p; pb::configure(p, Piano::Quality::Full, Piano::Radiation::Modal, 0);
        std::vector<double> e(piano_mvp_v4::kBoardModeCount, 0.0);
        const double t0 = pb::now();
        pb::render(p, ev, frames, [&](int) {
            const double *L = p.board().termsL(), *R = p.board().termsR();
            for (std::size_t i = 0; i < e.size(); ++i) e[i] += L[i] * L[i] + R[i] * R[i];
        });
        std::ofstream o(argv[2]);
        o.precision(17);
        for (double v : e) o << v << "\n";
        std::printf("%s: %d frames in %.1f s\n", sc.c_str(), frames, pb::now() - t0);
    } catch (const std::exception& ex) { std::fprintf(stderr, "%s\n", ex.what()); return 1; }
}

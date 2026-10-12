// SPDX-License-Identifier: MIT
// pme_reference_cli (P0.5-R7): renders scenario files with the reference solver.
//
//   pme_reference_cli render <scenario.json> [--features out.json] [--wav out.wav]
//                            [--energy out.json] [--normalise]
//   pme_reference_cli refine <scenario.json> --grids N1,N2,N3 --quantity partial-frequencies
//                            [--partials K] [--json out.json]
//   pme_reference_cli plate-modes <scenario.json> --count N [--points x1,y1,x2,y2,...]
//                            [--grid nx,ny]
//
// Exit status: 0 success, 1 a rendering or refinement error (message on stderr, names the offending
// field), 2 usage error.
#include <algorithm>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "pme/analysis/golden.hpp"
#include "pme/analysis/wav.hpp"
#include "pme/ref/common.hpp"
#include "pme/ref/features.hpp"
#include "pme/ref/plate.hpp"
#include "pme/ref/refine.hpp"
#include "pme/ref/scenario.hpp"

namespace {

void usage(std::ostream& os) {
    os << "usage:\n"
       << "  pme_reference_cli render <scenario.json> [--features out.json] [--wav out.wav]\n"
       << "                           [--energy out.json] [--normalise]\n"
       << "  pme_reference_cli refine <scenario.json> --grids N1,N2,N3 --quantity "
          "partial-frequencies\n"
       << "                           [--partials K] [--json out.json]\n"
       << "  pme_reference_cli plate-modes <scenario.json> --count N [--points x1,y1,x2,y2,...]\n"
       << "                           [--grid nx,ny]\n"
       << "  pme_reference_cli --version\n";
}

void write_text(const std::string& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        throw pme::ref::RefError("cannot open '" + path + "' for writing");
    }
    out << text;
    if (!out) {
        throw pme::ref::RefError("cannot write '" + path + "'");
    }
}

std::vector<int> parse_grids(const std::string& text) {
    std::vector<int> grids;
    std::stringstream ss(text);
    std::string item;
    while (std::getline(ss, item, ',')) {
        try {
            std::size_t used = 0;
            const int v = std::stoi(item, &used);
            if (used != item.size() || v < 4) {
                throw std::invalid_argument(item);
            }
            grids.push_back(v);
        } catch (const std::exception&) {
            throw pme::ref::RefError("--grids: '" + item + "' is not an integer of at least 4");
        }
    }
    return grids;
}

int render(int argc, char** argv) {
    std::string scenario_path;
    std::string features_path;
    std::string wav_path;
    std::string energy_path;
    bool normalise = false;
    for (int i = 2; i < argc; ++i) {
        const std::string a = argv[i];
        auto value = [&](std::string& dst) {
            if (i + 1 >= argc) {
                throw pme::ref::RefError(a + " needs a value");
            }
            dst = argv[++i];
        };
        if (a == "--features") {
            value(features_path);
        } else if (a == "--wav") {
            value(wav_path);
        } else if (a == "--energy") {
            value(energy_path);
        } else if (a == "--normalise") {
            normalise = true;
        } else if (!a.empty() && a[0] == '-') {
            std::cerr << "pme_reference_cli: unknown option " << a << '\n';
            usage(std::cerr);
            return 2;
        } else if (scenario_path.empty()) {
            scenario_path = a;
        } else {
            usage(std::cerr);
            return 2;
        }
    }
    if (scenario_path.empty()) {
        usage(std::cerr);
        return 2;
    }
    const pme::ref::Scenario sc = pme::ref::read_scenario_file(scenario_path);
    const pme::ref::Rendered rendered = pme::ref::render_scenario(sc);
    if (!features_path.empty()) {
        write_text(features_path, pme::analysis::serialize_golden(rendered.features));
    }
    if (!wav_path.empty()) {
        pme::analysis::write_wav(wav_path, pme::ref::results_to_wav(rendered.results, normalise));
    }
    if (!energy_path.empty()) {
        write_text(energy_path, pme::ref::energy_to_json(rendered.results));
    }
    std::cout << "rendered '" << sc.name << "': " << rendered.results.steps << " steps at "
              << rendered.results.sim_rate << " Hz, " << rendered.features.partials.size()
              << " partials\n";
    if (!rendered.results.energy.total.empty()) {
        std::cout << "max relative energy step "
                  << pme::ref::max_relative_energy_step(rendered.results.energy)
                  << ", max relative energy imbalance "
                  << pme::ref::max_relative_energy_imbalance(rendered.results.energy) << '\n';
    }
    return 0;
}

int refine(int argc, char** argv) {
    std::string scenario_path;
    std::string grids_text;
    std::string quantity;
    std::string json_path;
    int partials = 5;
    for (int i = 2; i < argc; ++i) {
        const std::string a = argv[i];
        auto value = [&](std::string& dst) {
            if (i + 1 >= argc) {
                throw pme::ref::RefError(a + " needs a value");
            }
            dst = argv[++i];
        };
        if (a == "--grids") {
            value(grids_text);
        } else if (a == "--quantity") {
            value(quantity);
        } else if (a == "--json") {
            value(json_path);
        } else if (a == "--partials") {
            std::string v;
            value(v);
            partials = std::atoi(v.c_str());
        } else if (!a.empty() && a[0] == '-') {
            std::cerr << "pme_reference_cli: unknown option " << a << '\n';
            usage(std::cerr);
            return 2;
        } else if (scenario_path.empty()) {
            scenario_path = a;
        } else {
            usage(std::cerr);
            return 2;
        }
    }
    if (scenario_path.empty() || grids_text.empty() || quantity.empty()) {
        usage(std::cerr);
        return 2;
    }
    if (quantity != "partial-frequencies") {
        std::cerr << "pme_reference_cli: --quantity must be partial-frequencies\n";
        return 2;
    }
    const pme::ref::Scenario sc = pme::ref::read_scenario_file(scenario_path);
    const pme::ref::RefinementReport rep =
        pme::ref::refine_partial_frequencies(sc, parse_grids(grids_text), partials);
    std::cout << std::setprecision(10);
    std::cout << "partial frequencies (Hz) by grid:\n";
    for (const auto& lv : rep.levels) {
        std::cout << "  N=" << lv.intervals << " (oversampling " << lv.oversampling << "):";
        for (const double f : lv.frequencies_hz) {
            std::cout << ' ' << f;
        }
        std::cout << '\n';
    }
    std::cout << "observed order of the three finest grids:";
    for (const double p : rep.observed_order) {
        std::cout << ' ' << std::setprecision(4) << p;
    }
    std::cout << '\n';
    if (!json_path.empty()) {
        write_text(json_path, pme::ref::refinement_to_json(rep));
    }
    return 0;
}

// Prints the JSON of plate_modes() for the scenario's plate (grid, ribs and all).
int plate_modes_command(int argc, char** argv) {
    std::string scenario_path;
    int count = 0;
    std::vector<std::pair<double, double>> points;
    int nx = 0;
    int ny = 0;
    for (int i = 2; i < argc; ++i) {
        const std::string a = argv[i];
        auto value = [&]() -> std::string {
            if (i + 1 >= argc) {
                throw pme::ref::RefError(a + " needs a value");
            }
            return argv[++i];
        };
        if (a == "--count") {
            count = std::atoi(value().c_str());
        } else if (a == "--points" || a == "--grid") {
            const std::string text = value();
            std::vector<double> nums;
            std::stringstream ss(text);
            std::string item;
            while (std::getline(ss, item, ',')) {
                try {
                    std::size_t used = 0;
                    nums.push_back(std::stod(item, &used));
                    if (used != item.size()) {
                        throw std::invalid_argument(item);
                    }
                } catch (const std::exception&) {
                    std::string msg = a;
                    msg += ": '";
                    msg += item;
                    msg += "' is not a number";
                    throw pme::ref::RefError(msg);
                }
            }
            if (a == "--points") {
                if (nums.size() % 2 != 0) {
                    throw pme::ref::RefError("--points: expected x,y pairs");
                }
                for (std::size_t k = 0; k < nums.size(); k += 2) {
                    points.emplace_back(nums[k], nums[k + 1]);
                }
            } else {
                if (nums.size() != 2) {
                    throw pme::ref::RefError("--grid: expected nx,ny");
                }
                nx = static_cast<int>(nums[0]);
                ny = static_cast<int>(nums[1]);
            }
        } else if (!a.empty() && a[0] == '-') {
            std::cerr << "pme_reference_cli: unknown option " << a << '\n';
            usage(std::cerr);
            return 2;
        } else if (scenario_path.empty()) {
            scenario_path = a;
        } else {
            usage(std::cerr);
            return 2;
        }
    }
    if (scenario_path.empty() || count < 1) {
        usage(std::cerr);
        return 2;
    }
    const pme::ref::Scenario sc = pme::ref::read_scenario_file(scenario_path);
    if (!sc.plate.has_value()) {
        throw pme::ref::RefError("scenario.plate: plate-modes needs a plate");
    }
    const pme::ref::PlateSpec& ps = *sc.plate;
    if (nx == 0) {
        nx = ps.nx;
        ny = ps.ny;
    }
    const pme::ref::PlateConstants pc = pme::ref::derive_plate(ps.params);
    const pme::ref::PlateModes modes =
        pme::ref::plate_modes(pc, ps.params.ribs, nx, ny, count, points);
    std::cout << pme::ref::plate_modes_to_json(modes, nx, ny, points) << '\n';
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        usage(std::cerr);
        return 2;
    }
    const std::string cmd = argv[1];
    try {
        if (cmd == "--version") {
            std::cout << "pme_reference_cli " << pme::ref::kProducerVersion << '\n';
            return 0;
        }
        if (cmd == "render") {
            return render(argc, argv);
        }
        if (cmd == "refine") {
            return refine(argc, argv);
        }
        if (cmd == "plate-modes") {
            return plate_modes_command(argc, argv);
        }
        usage(std::cerr);
        return 2;
    } catch (const std::exception& e) {
        std::cerr << "pme_reference_cli: " << e.what() << '\n';
        return 1;
    }
}

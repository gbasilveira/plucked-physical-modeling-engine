// SPDX-License-Identifier: MIT
#include "testdata.hpp"

#include <cmath>
#include <fstream>
#include <numbers>
#include <stdexcept>

#include <nlohmann/json.hpp>

namespace pme::analysis::testing {

std::vector<SignalCase> load_t1_cases() {
    const std::string path = std::string(PME_ANALYSIS_TESTDATA) + "/p0_3_t1_signals.json";
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error("cannot open " + path);
    }
    const nlohmann::json doc = nlohmann::json::parse(in);
    std::vector<SignalCase> cases;
    for (const auto& jc : doc.at("cases")) {
        SignalCase c;
        c.name = jc.at("name").get<std::string>();
        c.kind = jc.at("case").get<std::string>();
        c.sample_rates_hz = jc.at("sample_rates_hz").get<std::vector<double>>();
        c.duration_s = jc.at("duration_s").get<double>();
        for (const auto& jp : jc.at("partials")) {
            c.partials.push_back({jp.at("frequency_hz").get<double>(), jp.at("amplitude").get<double>(),
                                  jp.at("sigma_per_s").get<double>(), jp.at("phase_rad").get<double>()});
        }
        if (jc.contains("partial_numbers")) {
            c.partial_numbers = jc.at("partial_numbers").get<std::vector<int>>();
            c.f0_hz = jc.at("f0_hz").get<double>();
            c.inharmonicity_b = jc.at("inharmonicity_b").get<double>();
        }
        cases.push_back(std::move(c));
    }
    return cases;
}

std::vector<double> synthesize(const SignalCase& c, double sample_rate_hz) {
    const auto n = static_cast<std::size_t>(std::llround(c.duration_s * sample_rate_hz));
    std::vector<double> x(n, 0.0);
    for (const SignalPartial& p : c.partials) {
        for (std::size_t i = 0; i < n; ++i) {
            const double t = static_cast<double>(i) / sample_rate_hz;
            x[i] += p.amplitude * std::exp(-p.sigma_per_s * t) *
                    std::cos(2.0 * std::numbers::pi * p.frequency_hz * t + p.phase_rad);
        }
    }
    return x;
}

}  // namespace pme::analysis::testing

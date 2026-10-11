// SPDX-License-Identifier: MIT
#include "pme/ref/features.hpp"

#include <algorithm>
#include <cmath>
#include <nlohmann/json.hpp>
#include <set>

#include "pme/analysis/measures.hpp"
#include "pme/analysis/partials.hpp"
#include "pme/ref/common.hpp"

namespace pme::ref {

namespace {

bool is_feature_kind(ObservationKind k) {
    switch (k) {
        case ObservationKind::StringDisplacement:
        case ObservationKind::StringVelocity:
        case ObservationKind::BridgeForce:
        case ObservationKind::BridgeDisplacement:
        case ObservationKind::PlateDisplacement:
            return true;
        default:
            return false;
    }
}

}  // namespace

pme::analysis::GoldenFeatures extract_features(const Scenario& sc, const Results& r) {
    pme::analysis::GoldenFeatures g;
    g.scenario = sc.name.empty() ? "scenario" : sc.name;
    g.sample_rate_hz = r.sim_rate;
    g.producer_tool = kProducerTool;
    g.producer_version = kProducerVersion;

    const std::vector<double>* signal = nullptr;
    for (std::size_t i = 0; i < sc.observations.size(); ++i) {
        if (is_feature_kind(sc.observations[i].kind)) {
            signal = &r.signals[i].data;
            break;
        }
    }
    if (signal == nullptr && !r.signals.empty()) {
        signal = &r.signals.front().data;
    }
    if (signal != nullptr && signal->size() >= 16) {
        double peak = 0.0;
        for (const double v : *signal) {
            peak = std::max(peak, std::fabs(v));
        }
        if (peak > 0.0) {
            pme::analysis::PartialTrackingOptions opt;
            opt.max_partials = static_cast<std::size_t>(std::max(1, sc.feature_partials));
            opt.envelope_frame_s = 0.0;
            opt.peak_threshold_db = sc.feature_threshold_db;
            const auto fit = pme::analysis::track_partials(*signal, r.sim_rate, opt);
            const double f1 = sc.strings.empty() ? 0.0 : sc.strings[0].params.f1Target;
            std::set<int> used;
            int rank = 0;
            for (const auto& p : fit.partials) {
                ++rank;
                if (!(p.frequency_hz > 0.0) || !(p.amplitude >= 0.0)) {
                    continue;
                }
                int index = rank;
                if (f1 > 0.0) {
                    index = std::max(1, static_cast<int>(std::lround(p.frequency_hz / f1)));
                }
                if (!used.insert(index).second) {
                    continue;
                }
                pme::analysis::GoldenPartial gp;
                gp.index = index;
                gp.frequency_hz = p.frequency_hz;
                gp.amplitude = p.amplitude;
                gp.sigma_per_s = p.sigma_per_s;
                gp.t60_s = p.sigma_per_s > 1e-12 ? p.t60_s : 1e12;
                g.partials.push_back(gp);
            }
            std::sort(g.partials.begin(), g.partials.end(),
                      [](const auto& a, const auto& b) { return a.index < b.index; });
            pme::analysis::SpectralEnvelopeOptions eo;
            eo.f_max_hz = std::min(20000.0, 0.45 * r.sim_rate);
            const auto env = pme::analysis::spectral_envelope(*signal, r.sim_rate, eo);
            g.spectral_envelope =
                pme::analysis::GoldenEnvelope{env.frequencies_hz, env.magnitude_db};
        }
    }
    for (std::size_t i = 0; i < sc.observations.size(); ++i) {
        if (sc.observations[i].kind == ObservationKind::HammerPenetration) {
            const auto c = pme::analysis::measure_contact(r.signals[i].data, r.sim_rate, 0.0);
            if (c.found) {
                g.contact_time_s = c.duration_s;
            }
            break;
        }
    }
    if (r.energy.total.size() >= 2 && r.energy.total.front() != 0.0) {
        const auto rep = pme::analysis::account_energy(r.energy.total);
        g.energy =
            pme::analysis::GoldenEnergy{rep.max_relative_drift, rep.max_relative_step_change};
    }
    return g;
}

Rendered render_scenario(const Scenario& scenario) {
    Rendered out;
    out.results = simulate(scenario);
    out.features = extract_features(scenario, out.results);
    return out;
}

namespace {

// Hann-windowed sinc low-pass at 0.45 of the output rate, then every `factor`-th sample.
std::vector<double> decimate(const std::vector<double>& x, int factor) {
    if (factor <= 1) {
        return x;
    }
    const std::ptrdiff_t half = 8 * static_cast<std::ptrdiff_t>(factor);
    std::vector<double> taps(static_cast<std::size_t>(2 * half + 1));
    const double fc = 0.45 / factor;  // cycles per input sample
    double sum = 0.0;
    for (std::ptrdiff_t i = -half; i <= half; ++i) {
        const auto t = static_cast<double>(i);
        const double sinc = i == 0 ? 2.0 * fc : std::sin(2.0 * kPi * fc * t) / (kPi * t);
        const double win = 0.5 + 0.5 * std::cos(kPi * t / static_cast<double>(half + 1));
        taps[static_cast<std::size_t>(i + half)] = sinc * win;
        sum += sinc * win;
    }
    for (double& t : taps) {
        t /= sum;
    }
    std::vector<double> y;
    y.reserve(x.size() / static_cast<std::size_t>(factor) + 1);
    const auto len = static_cast<std::ptrdiff_t>(x.size());
    for (std::ptrdiff_t n = 0; n < len; n += factor) {
        double s = 0.0;
        for (std::ptrdiff_t i = -half; i <= half; ++i) {
            const std::ptrdiff_t idx = n + i;
            if (idx >= 0 && idx < len) {
                s += taps[static_cast<std::size_t>(i + half)] * x[static_cast<std::size_t>(idx)];
            }
        }
        y.push_back(s);
    }
    return y;
}

}  // namespace

pme::analysis::WavData results_to_wav(const Results& r, bool normalise) {
    if (r.signals.empty()) {
        throw RefError("no observation signals to write");
    }
    if (r.fs != std::floor(r.fs)) {
        throw RefError("WAV output needs an integer sample rate fs");
    }
    std::vector<std::vector<double>> chans;
    chans.reserve(r.signals.size());
    for (const auto& s : r.signals) {
        chans.push_back(decimate(s.data, r.oversampling));
    }
    pme::analysis::WavData w;
    w.sample_rate_hz = r.fs;
    w.channels = static_cast<unsigned>(chans.size());
    w.format = pme::analysis::WavFormat::float32;
    const std::size_t frames = chans[0].size();
    double peak = 0.0;
    for (const auto& c : chans) {
        for (const double v : c) {
            peak = std::max(peak, std::fabs(v));
        }
    }
    const double scale = (normalise && peak > 0.0) ? 0.9 / peak : 1.0;
    w.samples.resize(frames * chans.size());
    for (std::size_t f = 0; f < frames; ++f) {
        for (std::size_t c = 0; c < chans.size(); ++c) {
            w.samples[f * chans.size() + c] = scale * chans[c][f];
        }
    }
    return w;
}

std::string energy_to_json(const Results& r) {
    using nlohmann::ordered_json;
    ordered_json j;
    j["schema_version"] = 1;
    j["scenario"] = r.name;
    j["sample_rate_hz"] = r.sim_rate;
    j["steps"] = r.steps;
    const EnergyTrace& e = r.energy;
    ordered_json summary;
    if (e.total.size() >= 2 && e.total.front() != 0.0) {
        const auto rep = pme::analysis::account_energy(e.total);
        summary["max_relative_drift"] = rep.max_relative_drift;
        summary["max_relative_step_change"] = rep.max_relative_step_change;
        summary["max_relative_energy_step"] = max_relative_energy_step(e);
        summary["max_relative_imbalance"] = max_relative_energy_imbalance(e);
        double dissipated = 0.0;
        for (const double l : e.loss) {
            dissipated += l;
        }
        summary["dissipated"] = dissipated;
        summary["initial"] = e.total.front();
        summary["final"] = e.total.back();
    }
    j["summary"] = summary;
    j["newton"] = {{"max_iterations", r.newton.max_iterations},
                   {"max_relative_residual", r.newton.max_relative_residual},
                   {"contact_steps", r.newton.contact_steps}};
    j["total"] = e.total;
    j["string"] = e.string;
    j["bridge"] = e.bridge;
    j["plate"] = e.plate;
    j["hammer"] = e.hammer;
    j["contact"] = e.contact;
    j["loss"] = e.loss;
    j["balance"] = e.balance;
    return j.dump(1);
}

}  // namespace pme::ref

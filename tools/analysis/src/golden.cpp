// SPDX-License-Identifier: MIT
#include "pme/analysis/golden.hpp"

#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <nlohmann/json.hpp>
#include <sstream>

namespace pme::analysis {

namespace {

using Json = nlohmann::ordered_json;

[[noreturn]] void fail(const std::string& message) {
    throw GoldenError("golden features: " + message);
}

const Json& require(const Json& object, const char* key, const std::string& where) {
    if (!object.is_object() || !object.contains(key)) {
        fail("missing field '" + where + key + "'");
    }
    return object.at(key);
}

double number(const Json& object, const char* key, const std::string& where) {
    const Json& v = require(object, key, where);
    if (!v.is_number()) {
        fail("field '" + where + key + "' must be a number");
    }
    return v.get<double>();
}

std::optional<double> optional_number(const Json& object, const char* key,
                                      const std::string& where) {
    if (!object.contains(key)) {
        return std::nullopt;
    }
    return number(object, key, where);
}

std::string text(const Json& object, const char* key, const std::string& where) {
    const Json& v = require(object, key, where);
    if (!v.is_string()) {
        fail("field '" + where + key + "' must be a string");
    }
    return v.get<std::string>();
}

std::vector<double> number_array(const Json& object, const char* key, const std::string& where) {
    const Json& v = require(object, key, where);
    if (!v.is_array()) {
        fail("field '" + where + key + "' must be an array");
    }
    std::vector<double> out;
    for (const Json& e : v) {
        if (!e.is_number()) {
            fail("field '" + where + key + "' must contain only numbers");
        }
        out.push_back(e.get<double>());
    }
    return out;
}

}  // namespace

GoldenFeatures parse_golden(std::string_view json_text) {
    Json doc;
    try {
        doc = Json::parse(json_text);
    } catch (const Json::parse_error& e) {
        fail(std::string("not valid JSON: ") + e.what());
    }
    GoldenFeatures g;
    const double version = number(doc, "schema_version", "");
    if (version != 1.0) {
        fail("unsupported schema_version " + std::to_string(version));
    }
    g.schema_version = 1;
    g.scenario = text(doc, "scenario", "");
    g.sample_rate_hz = number(doc, "sample_rate_hz", "");
    const std::string root;
    const Json& producer = require(doc, "producer", root);
    g.producer_tool = text(producer, "tool", "producer.");
    g.producer_version = text(producer, "version", "producer.");

    const Json& partials = require(doc, "partials", root);
    if (!partials.is_array()) {
        fail("field 'partials' must be an array");
    }
    for (std::size_t i = 0; i < partials.size(); ++i) {
        const Json& p = partials[i];
        const std::string where = "partials[" + std::to_string(i) + "].";
        GoldenPartial gp;
        gp.index = static_cast<int>(number(p, "index", where));
        gp.frequency_hz = number(p, "frequency_hz", where);
        gp.amplitude = number(p, "amplitude", where);
        gp.sigma_per_s = number(p, "sigma_per_s", where);
        gp.t60_s = number(p, "t60_s", where);
        if (p.contains("second_decay")) {
            const Json& s = p.at("second_decay");
            GoldenDecay2 d;
            d.sigma_per_s = number(s, "sigma_per_s", where + "second_decay.");
            d.knee_s = number(s, "knee_s", where + "second_decay.");
            gp.second_decay = d;
        }
        g.partials.push_back(gp);
    }

    if (doc.contains("spectral_envelope")) {
        const Json& e = doc.at("spectral_envelope");
        GoldenEnvelope env;
        env.frequencies_hz = number_array(e, "frequencies_hz", "spectral_envelope.");
        env.magnitude_db = number_array(e, "magnitude_db", "spectral_envelope.");
        if (env.frequencies_hz.size() != env.magnitude_db.size()) {
            fail("spectral_envelope arrays differ in length");
        }
        g.spectral_envelope = std::move(env);
    }
    g.contact_time_s = optional_number(doc, "contact_time_s", "");
    if (doc.contains("energy")) {
        const Json& e = doc.at("energy");
        GoldenEnergy en;
        en.max_relative_drift = number(e, "max_relative_drift", "energy.");
        en.max_relative_step_change = number(e, "max_relative_step_change", "energy.");
        g.energy = en;
    }

    if (doc.contains("tolerances")) {
        const Json& t = doc.at("tolerances");
        if (t.contains("default")) {
            const Json& d = t.at("default");
            auto set = [&](const char* key, double& target) {
                if (const auto v = optional_number(d, key, "tolerances.default.")) {
                    target = *v;
                }
            };
            set("frequency_cents", g.tolerances.frequency_cents);
            set("amplitude_db", g.tolerances.amplitude_db);
            set("decay_rate_relative", g.tolerances.decay_rate_relative);
            set("spectral_envelope_db", g.tolerances.spectral_envelope_db);
            set("contact_time_relative", g.tolerances.contact_time_relative);
            set("energy_absolute", g.tolerances.energy_absolute);
        }
        if (t.contains("partials")) {
            for (const auto& [key, value] : t.at("partials").items()) {
                const std::string where = "tolerances.partials." + key + ".";
                PartialTolerance pt;
                pt.frequency_cents = optional_number(value, "frequency_cents", where);
                pt.amplitude_db = optional_number(value, "amplitude_db", where);
                pt.decay_rate_relative = optional_number(value, "decay_rate_relative", where);
                try {
                    g.tolerances.partials[std::stoi(key)] = pt;
                } catch (const std::exception&) {
                    fail("tolerances.partials key '" + key + "' is not a partial index");
                }
            }
        }
    }
    return g;
}

std::string serialize_golden(const GoldenFeatures& g) {
    Json doc = Json::object();
    doc["schema_version"] = g.schema_version;
    doc["scenario"] = g.scenario;
    doc["sample_rate_hz"] = g.sample_rate_hz;
    doc["producer"] = {{"tool", g.producer_tool}, {"version", g.producer_version}};
    doc["units"] = {{"frequency", "Hz"},         {"sigma", "1/s"},       {"t60", "s"},
                    {"amplitude", "linear"},     {"magnitude_db", "dB"}, {"contact_time", "s"},
                    {"tolerances", "see schema"}};
    Json partials = Json::array();
    for (const GoldenPartial& p : g.partials) {
        Json jp = {{"index", p.index},
                   {"frequency_hz", p.frequency_hz},
                   {"amplitude", p.amplitude},
                   {"sigma_per_s", p.sigma_per_s},
                   {"t60_s", p.t60_s}};
        if (p.second_decay) {
            jp["second_decay"] = {{"sigma_per_s", p.second_decay->sigma_per_s},
                                  {"knee_s", p.second_decay->knee_s}};
        }
        partials.push_back(jp);
    }
    doc["partials"] = partials;
    if (g.spectral_envelope) {
        doc["spectral_envelope"] = {{"frequencies_hz", g.spectral_envelope->frequencies_hz},
                                    {"magnitude_db", g.spectral_envelope->magnitude_db}};
    }
    if (g.contact_time_s) {
        doc["contact_time_s"] = *g.contact_time_s;
    }
    if (g.energy) {
        doc["energy"] = {{"max_relative_drift", g.energy->max_relative_drift},
                         {"max_relative_step_change", g.energy->max_relative_step_change}};
    }
    Json def = {{"frequency_cents", g.tolerances.frequency_cents},
                {"amplitude_db", g.tolerances.amplitude_db},
                {"decay_rate_relative", g.tolerances.decay_rate_relative},
                {"spectral_envelope_db", g.tolerances.spectral_envelope_db},
                {"contact_time_relative", g.tolerances.contact_time_relative},
                {"energy_absolute", g.tolerances.energy_absolute}};
    Json tol = {{"default", def}};
    if (!g.tolerances.partials.empty()) {
        Json overrides = Json::object();
        for (const auto& [index, pt] : g.tolerances.partials) {
            Json o = Json::object();
            if (pt.frequency_cents) {
                o["frequency_cents"] = *pt.frequency_cents;
            }
            if (pt.amplitude_db) {
                o["amplitude_db"] = *pt.amplitude_db;
            }
            if (pt.decay_rate_relative) {
                o["decay_rate_relative"] = *pt.decay_rate_relative;
            }
            overrides[std::to_string(index)] = o;
        }
        tol["partials"] = overrides;
    }
    doc["tolerances"] = tol;
    return doc.dump(2) + "\n";
}

GoldenFeatures read_golden_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        fail("cannot open " + path);
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return parse_golden(buffer.str());
}

void write_golden_file(const std::string& path, const GoldenFeatures& features) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        fail("cannot create " + path);
    }
    out << serialize_golden(features);
}

// ---- Comparison --------------------------------------------------------------------------------

namespace {

constexpr double kInf = std::numeric_limits<double>::infinity();

double cents_diff(double candidate, double reference) {
    if (candidate > 0.0 && reference > 0.0) {
        return std::fabs(1200.0 * std::log2(candidate / reference));
    }
    return candidate == reference ? 0.0 : kInf;
}

double db_diff(double candidate, double reference) {
    if (candidate > 0.0 && reference > 0.0) {
        return std::fabs(20.0 * std::log10(candidate / reference));
    }
    return candidate == reference ? 0.0 : kInf;
}

double relative_diff(double candidate, double reference) {
    const double d = std::fabs(candidate - reference);
    return reference == 0.0 ? d : d / std::fabs(reference);
}

void add(ComparisonResult& out, std::string feature, double difference, double tolerance,
         const char* unit, std::string note = {}) {
    FeatureResult r;
    r.feature = std::move(feature);
    r.difference = difference;
    r.tolerance = tolerance;
    r.unit = unit;
    r.pass = difference <= tolerance;  // false for NaN
    r.note = std::move(note);
    out.pass = out.pass && r.pass;
    out.results.push_back(std::move(r));
}

void add_missing(ComparisonResult& out, std::string feature, std::string note) {
    FeatureResult r;
    r.feature = std::move(feature);
    r.difference = kInf;
    r.unit = "-";
    r.pass = false;
    r.note = std::move(note);
    out.pass = false;
    out.results.push_back(std::move(r));
}

}  // namespace

ComparisonResult compare_golden(const GoldenFeatures& ref, const GoldenFeatures& cand) {
    ComparisonResult out;
    if (ref.scenario != cand.scenario) {
        out.notes.push_back("scenario names differ: reference '" + ref.scenario + "', candidate '" +
                            cand.scenario + "'");
    }
    const GoldenTolerances& tol = ref.tolerances;

    for (const GoldenPartial& rp : ref.partials) {
        const std::string name = "partial[" + std::to_string(rp.index) + "]";
        const GoldenPartial* cp = nullptr;
        for (const GoldenPartial& c : cand.partials) {
            if (c.index == rp.index) {
                cp = &c;
                break;
            }
        }
        if (cp == nullptr) {
            add_missing(out, name,
                        "partial present in the reference is missing from the candidate");
            continue;
        }
        PartialTolerance over;
        if (const auto it = tol.partials.find(rp.index); it != tol.partials.end()) {
            over = it->second;
        }
        add(out, name + ".frequency", cents_diff(cp->frequency_hz, rp.frequency_hz),
            over.frequency_cents.value_or(tol.frequency_cents), "cent");
        add(out, name + ".amplitude", db_diff(cp->amplitude, rp.amplitude),
            over.amplitude_db.value_or(tol.amplitude_db), "dB");
        const double decay_tol = over.decay_rate_relative.value_or(tol.decay_rate_relative);
        add(out, name + ".decay_rate", relative_diff(cp->sigma_per_s, rp.sigma_per_s), decay_tol,
            "relative");
        if (const auto& ref_second = rp.second_decay; ref_second) {
            const auto& cand_second = cp->second_decay;
            if (!cand_second) {
                add_missing(out, name + ".second_decay_rate",
                            "second decay present in the reference is missing from the candidate");
            } else {
                add(out, name + ".second_decay_rate",
                    relative_diff(cand_second->sigma_per_s, ref_second->sigma_per_s), decay_tol,
                    "relative");
            }
        }
    }
    for (const GoldenPartial& c : cand.partials) {
        bool known = false;
        for (const GoldenPartial& rp : ref.partials) {
            known = known || rp.index == c.index;
        }
        if (!known) {
            out.notes.push_back("candidate has partial " + std::to_string(c.index) +
                                " that the reference does not");
        }
    }

    if (ref.spectral_envelope) {
        if (!cand.spectral_envelope) {
            add_missing(out, "spectral_envelope",
                        "reference has a spectral envelope, candidate has none");
        } else {
            const GoldenEnvelope& re = *ref.spectral_envelope;
            const GoldenEnvelope& ce = *cand.spectral_envelope;
            bool same_grid = re.frequencies_hz.size() == ce.frequencies_hz.size();
            for (std::size_t i = 0; same_grid && i < re.frequencies_hz.size(); ++i) {
                same_grid = std::fabs(re.frequencies_hz[i] - ce.frequencies_hz[i]) <=
                            1e-9 * std::fabs(re.frequencies_hz[i]);
            }
            if (!same_grid) {
                add_missing(out, "spectral_envelope", "frequency grids differ");
            } else {
                double worst = 0.0;
                std::size_t at = 0;
                for (std::size_t i = 0; i < re.magnitude_db.size(); ++i) {
                    const double d = std::fabs(ce.magnitude_db[i] - re.magnitude_db[i]);
                    if (!(d <= worst)) {  // also captures NaN
                        worst = d;
                        at = i;
                    }
                }
                add(out, "spectral_envelope", worst, tol.spectral_envelope_db, "dB",
                    "largest difference at " +
                        std::to_string(re.frequencies_hz.empty() ? 0.0 : re.frequencies_hz[at]) +
                        " Hz");
            }
        }
    }
    if (ref.contact_time_s) {
        if (!cand.contact_time_s) {
            add_missing(out, "contact_time", "reference has a contact time, candidate has none");
        } else {
            add(out, "contact_time", relative_diff(*cand.contact_time_s, *ref.contact_time_s),
                tol.contact_time_relative, "relative");
        }
    }
    if (ref.energy) {
        if (!cand.energy) {
            add_missing(out, "energy", "reference has energy values, candidate has none");
        } else {
            add(out, "energy.max_relative_drift",
                std::fabs(cand.energy->max_relative_drift - ref.energy->max_relative_drift),
                tol.energy_absolute, "absolute");
            add(out, "energy.max_relative_step_change",
                std::fabs(cand.energy->max_relative_step_change -
                          ref.energy->max_relative_step_change),
                tol.energy_absolute, "absolute");
        }
    }
    return out;
}

std::string format_comparison(const ComparisonResult& result, bool failures_only) {
    std::ostringstream out;
    for (const FeatureResult& r : result.results) {
        if (failures_only && r.pass) {
            continue;
        }
        out << (r.pass ? "ok   " : "FAIL ") << std::left << std::setw(34) << r.feature << std::right
            << " difference " << std::setprecision(6) << r.difference << ' ' << r.unit
            << " (tolerance " << r.tolerance << ")";
        if (!r.note.empty()) {
            out << " - " << r.note;
        }
        out << '\n';
    }
    for (const std::string& note : result.notes) {
        out << "note: " << note << '\n';
    }
    std::size_t failed = 0;
    for (const FeatureResult& r : result.results) {
        failed += r.pass ? 0U : 1U;
    }
    out << (result.pass ? "PASS" : "FAIL") << ": " << failed << " of " << result.results.size()
        << " features outside tolerance\n";
    return out.str();
}

}  // namespace pme::analysis

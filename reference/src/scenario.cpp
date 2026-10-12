// SPDX-License-Identifier: MIT
#include "pme/ref/scenario.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <initializer_list>
#include <nlohmann/json.hpp>
#include <set>
#include <sstream>

#include "pme/ref/common.hpp"

namespace pme::ref {

namespace {

using json = nlohmann::json;

void check_keys(const json& j, std::initializer_list<const char*> allowed, const std::string& ctx) {
    if (!j.is_object()) {
        throw RefError(ctx + ": expected an object");
    }
    std::set<std::string> ok;
    for (const char* a : allowed) {
        ok.insert(a);
    }
    for (auto it = j.begin(); it != j.end(); ++it) {
        if (ok.count(it.key()) == 0) {
            throw RefError(ctx + "." + it.key() + ": unknown field");
        }
    }
}

double num(const json& j, const char* key, double fallback, const std::string& ctx) {
    if (!j.contains(key)) {
        return fallback;
    }
    if (!j[key].is_number()) {
        throw RefError(ctx + "." + key + ": expected a number");
    }
    return j[key].get<double>();
}

double req_num(const json& j, const char* key, const std::string& ctx) {
    if (!j.contains(key)) {
        throw RefError(ctx + "." + key + ": required field is missing");
    }
    return num(j, key, 0.0, ctx);
}

int integer(const json& j, const char* key, int fallback, const std::string& ctx) {
    if (!j.contains(key)) {
        return fallback;
    }
    if (!j[key].is_number_integer()) {
        throw RefError(ctx + "." + key + ": expected an integer");
    }
    return j[key].get<int>();
}

bool boolean(const json& j, const char* key, bool fallback, const std::string& ctx) {
    if (!j.contains(key)) {
        return fallback;
    }
    if (!j[key].is_boolean()) {
        throw RefError(ctx + "." + key + ": expected true or false");
    }
    return j[key].get<bool>();
}

std::string text(const json& j, const char* key, const std::string& fallback,
                 const std::string& ctx) {
    if (!j.contains(key)) {
        return fallback;
    }
    if (!j[key].is_string()) {
        throw RefError(ctx + "." + key + ": expected a string");
    }
    return j[key].get<std::string>();
}

std::vector<double> numbers(const json& j, const char* key, const std::string& ctx) {
    std::vector<double> out;
    if (!j.contains(key)) {
        return out;
    }
    if (!j[key].is_array()) {
        throw RefError(ctx + "." + key + ": expected an array of numbers");
    }
    for (const auto& v : j[key]) {
        if (!v.is_number()) {
            throw RefError(ctx + "." + key + ": expected an array of numbers");
        }
        out.push_back(v.get<double>());
    }
    return out;
}

Point point(const json& j, const std::string& ctx) {
    check_keys(j, {"x", "y", "z"}, ctx);
    Point p;
    p.x = num(j, "x", 0.0, ctx);
    p.y = num(j, "y", 0.0, ctx);
    p.z = num(j, "z", 0.0, ctx);
    return p;
}

StringParams string_params(const json& j, const std::string& ctx) {
    check_keys(j,
               {"L", "dCore", "rhoCore", "ECore", "dOuter", "rhoWind", "packing", "epsWind",
                "f1Target", "b1", "b2", "b3", "sigmaOverride", "boundary", "detuneH", "bridgeEnd"},
               ctx);
    StringParams p;
    p.L = req_num(j, "L", ctx);
    p.dCore = req_num(j, "dCore", ctx);
    p.rhoCore = req_num(j, "rhoCore", ctx);
    p.ECore = req_num(j, "ECore", ctx);
    p.dOuter = num(j, "dOuter", 0.0, ctx);
    p.rhoWind = num(j, "rhoWind", 0.0, ctx);
    p.packing = num(j, "packing", 0.0, ctx);
    p.epsWind = num(j, "epsWind", 0.0, ctx);
    p.f1Target = req_num(j, "f1Target", ctx);
    p.b1 = num(j, "b1", 0.0, ctx);
    p.b2 = num(j, "b2", 0.0, ctx);
    p.b3 = num(j, "b3", 0.0, ctx);
    p.sigmaOverride = numbers(j, "sigmaOverride", ctx);
    const std::string bnd = text(j, "boundary", "pinned", ctx);
    if (bnd == "pinned") {
        p.boundary = Boundary::Pinned;
    } else if (bnd == "clamped") {
        p.boundary = Boundary::Clamped;
    } else {
        throw RefError(ctx + R"(.boundary: expected "pinned" or "clamped")");
    }
    p.detuneH = num(j, "detuneH", 0.0, ctx);
    const std::string be = text(j, "bridgeEnd", "at_L", ctx);
    if (be == "at_L") {
        p.bridgeEnd = BridgeEnd::AtL;
    } else if (be == "at_0") {
        p.bridgeEnd = BridgeEnd::At0;
    } else {
        throw RefError(ctx + R"(.bridgeEnd: expected "at_L" or "at_0")");
    }
    return p;
}

PlateSpec plate_spec(const json& j, const std::string& ctx) {
    check_keys(j,
               {"length_x",
                "length_y",
                "thickness",
                "grain_angle",
                "youngs_l",
                "youngs_r",
                "shear_lr",
                "poisson_lr",
                "poisson_rl",
                "density",
                "moisture",
                "d_e_d_mc_l",
                "d_e_d_mc_r",
                "d_g_d_mc",
                "d_rho_d_mc",
                "loss_factor",
                "rim_stiffness",
                "rim_rotational_stiffness",
                "f_max",
                "outline",
                "thickness_grid",
                "ribs",
                "bridges",
                "grid",
                "initial_displacement",
                "loss"},
               ctx);
    PlateSpec s;
    PlateParams& p = s.params;
    p.lengthX = req_num(j, "length_x", ctx);
    p.lengthY = req_num(j, "length_y", ctx);
    p.thickness = req_num(j, "thickness", ctx);
    p.grainAngle = num(j, "grain_angle", 0.0, ctx);
    p.youngsL = req_num(j, "youngs_l", ctx);
    p.youngsR = req_num(j, "youngs_r", ctx);
    p.shearLR = req_num(j, "shear_lr", ctx);
    p.poissonLR = req_num(j, "poisson_lr", ctx);
    p.poissonRL = req_num(j, "poisson_rl", ctx);
    p.density = req_num(j, "density", ctx);
    p.moisture = num(j, "moisture", 0.0, ctx);
    p.dEdMcL = num(j, "d_e_d_mc_l", 0.0, ctx);
    p.dEdMcR = num(j, "d_e_d_mc_r", 0.0, ctx);
    p.dGdMc = num(j, "d_g_d_mc", 0.0, ctx);
    p.dRhodMc = num(j, "d_rho_d_mc", 0.0, ctx);
    p.lossFactor = num(j, "loss_factor", 0.0, ctx);
    p.rimStiffness = num(j, "rim_stiffness", 0.0, ctx);
    p.rimRotationalStiffness = num(j, "rim_rotational_stiffness", 0.0, ctx);
    p.fMax = num(j, "f_max", 0.0, ctx);
    // Features the reference does not model are parsed only so that the error names the field.
    if (j.contains("outline") && j["outline"].is_array() && !j["outline"].empty()) {
        p.outline.push_back(Point{});
    }
    if (j.contains("thickness_grid") && !j["thickness_grid"].is_null()) {
        p.thicknessNx = 2;
        p.thicknessNy = 2;
        p.thicknessGrid.assign(4, p.thickness);
    }
    if (j.contains("ribs")) {
        if (!j["ribs"].is_array()) {
            throw RefError(ctx + ".ribs: expected an array");
        }
        std::size_t ri = 0;
        for (const auto& jr : j["ribs"]) {
            const std::string c = ctx + ".ribs[" + std::to_string(ri++) + "]";
            check_keys(jr, {"start", "end", "width", "height", "youngs", "density"}, c);
            RibDesc r;
            if (!jr.contains("start") || !jr.contains("end")) {
                throw RefError(c + ": start and end are required");
            }
            r.start = point(jr["start"], c + ".start");
            r.end = point(jr["end"], c + ".end");
            r.width = req_num(jr, "width", c);
            r.height = req_num(jr, "height", c);
            r.youngs = req_num(jr, "youngs", c);
            r.density = req_num(jr, "density", c);
            p.ribs.push_back(r);
        }
    }
    if (j.contains("bridges") && j["bridges"].is_array() && !j["bridges"].empty()) {
        p.bridges.push_back(BridgeBeamDesc{});
    }
    if (j.contains("grid")) {
        check_keys(j["grid"], {"nx", "ny"}, ctx + ".grid");
        s.nx = integer(j["grid"], "nx", s.nx, ctx + ".grid");
        s.ny = integer(j["grid"], "ny", s.ny, ctx + ".grid");
    }
    if (j.contains("initial_displacement")) {
        const auto& ji = j["initial_displacement"];
        check_keys(ji, {"x", "y", "amplitude"}, ctx + ".initial_displacement");
        s.has_initial = true;
        s.initial_x = req_num(ji, "x", ctx + ".initial_displacement");
        s.initial_y = req_num(ji, "y", ctx + ".initial_displacement");
        s.initial_amplitude = req_num(ji, "amplitude", ctx + ".initial_displacement");
    }
    if (j.contains("loss")) {
        check_keys(j["loss"], {"b1", "b3", "reference_hz"}, ctx + ".loss");
        s.loss_b1 = num(j["loss"], "b1", 0.0, ctx + ".loss");
        s.loss_b3 = num(j["loss"], "b3", 0.0, ctx + ".loss");
        s.loss_reference_hz = num(j["loss"], "reference_hz", 0.0, ctx + ".loss");
    }
    return s;
}

ObservationKind observation_kind(const std::string& k, const std::string& ctx) {
    static const std::pair<const char*, ObservationKind> table[] = {
        {"string_displacement", ObservationKind::StringDisplacement},
        {"string_velocity", ObservationKind::StringVelocity},
        {"bridge_force", ObservationKind::BridgeForce},
        {"bridge_force_perp", ObservationKind::BridgeForcePerp},
        {"bridge_displacement", ObservationKind::BridgeDisplacement},
        {"bridge_velocity", ObservationKind::BridgeVelocity},
        {"hammer_force", ObservationKind::HammerForce},
        {"hammer_penetration", ObservationKind::HammerPenetration},
        {"hammer_position", ObservationKind::HammerPosition},
        {"barrier_force", ObservationKind::BarrierForce},
        {"plate_displacement", ObservationKind::PlateDisplacement},
        {"longitudinal_bridge_force", ObservationKind::LongitudinalBridgeForce},
        {"longitudinal_displacement", ObservationKind::LongitudinalDisplacement},
    };
    for (const auto& [name, kind] : table) {
        if (k == name) {
            return kind;
        }
    }
    throw RefError(ctx + ".kind: unknown observation kind '" + k + "'");
}

}  // namespace

PlateSpec parse_plate_spec(std::string_view json_text) {
    json doc;
    try {
        doc = json::parse(json_text);
    } catch (const json::exception& e) {
        throw RefError(std::string("plate: invalid JSON: ") + e.what());
    }
    return plate_spec(doc, "plate");
}

Scenario parse_scenario(std::string_view json_text) {
    json doc;
    try {
        doc = json::parse(json_text);
    } catch (const json::exception& e) {
        throw RefError(std::string("scenario: invalid JSON: ") + e.what());
    }
    const std::string ctx = "scenario";
    check_keys(doc,
               {"schema_version", "name", "fs", "oversampling", "duration", "theta", "strings",
                "hammers", "bridges", "plate", "barriers", "observations", "feature_partials",
                "feature_threshold_db"},
               ctx);
    Scenario sc;
    sc.schema_version = integer(doc, "schema_version", 0, ctx);
    if (sc.schema_version != 1) {
        throw RefError("scenario.schema_version: expected 1");
    }
    sc.name = text(doc, "name", "scenario", ctx);
    sc.fs = req_num(doc, "fs", ctx);
    sc.oversampling = integer(doc, "oversampling", 1, ctx);
    sc.duration = req_num(doc, "duration", ctx);
    sc.theta = num(doc, "theta", 0.25, ctx);
    sc.feature_partials = integer(doc, "feature_partials", 12, ctx);
    sc.feature_threshold_db = num(doc, "feature_threshold_db", 90.0, ctx);

    if (doc.contains("strings")) {
        std::size_t idx = 0;
        for (const auto& js : doc["strings"]) {
            const std::string c = ctx + ".strings[" + std::to_string(idx++) + "]";
            check_keys(
                js, {"name", "params", "grid", "bridge", "initial_displacement", "geometric"}, c);
            StringSpec s;
            s.name = text(js, "name", "string", c);
            if (!js.contains("params")) {
                throw RefError(c + ".params: required field is missing");
            }
            s.params = string_params(js["params"], c + ".params");
            if (js.contains("grid")) {
                check_keys(js["grid"], {"intervals"}, c + ".grid");
                s.intervals = integer(js["grid"], "intervals", s.intervals, c + ".grid");
            }
            s.bridge = integer(js, "bridge", -1, c);
            if (js.contains("initial_displacement")) {
                const auto& ji = js["initial_displacement"];
                check_keys(ji, {"amplitude", "position", "angle"}, c + ".initial_displacement");
                s.initial.enabled = true;
                s.initial.amplitude = req_num(ji, "amplitude", c + ".initial_displacement");
                s.initial.position = req_num(ji, "position", c + ".initial_displacement");
                s.initial.angle = num(ji, "angle", 0.0, c + ".initial_displacement");
            }
            if (js.contains("geometric")) {
                const auto& jg = js["geometric"];
                check_keys(
                    jg,
                    {"tension_modulation", "longitudinal", "longitudinal_b1", "longitudinal_b3"},
                    c + ".geometric");
                s.geometric.tension_modulation =
                    boolean(jg, "tension_modulation", false, c + ".geometric");
                s.geometric.longitudinal = boolean(jg, "longitudinal", false, c + ".geometric");
                s.geometric.longitudinal_b1 = num(jg, "longitudinal_b1", 0.0, c + ".geometric");
                s.geometric.longitudinal_b3 = num(jg, "longitudinal_b3", 0.0, c + ".geometric");
                if (jg.contains("longitudinal_initial")) {
                    const auto& jl = jg["longitudinal_initial"];
                    check_keys(jl, {"amplitude", "position"},
                               c + ".geometric.longitudinal_initial");
                    s.geometric.has_longitudinal_initial = true;
                    s.geometric.longitudinal_initial_amplitude =
                        req_num(jl, "amplitude", c + ".geometric.longitudinal_initial");
                    s.geometric.longitudinal_initial_position =
                        req_num(jl, "position", c + ".geometric.longitudinal_initial");
                }
            }
            sc.strings.push_back(std::move(s));
        }
    }
    if (doc.contains("hammers")) {
        std::size_t idx = 0;
        for (const auto& jh : doc["hammers"]) {
            const std::string c = ctx + ".hammers[" + std::to_string(idx++) + "]";
            check_keys(
                jh,
                {"mass", "K", "p", "mu", "width", "strike_x", "velocity", "gap", "strings", "wall"},
                c);
            HammerSpec h;
            h.mass = req_num(jh, "mass", c);
            h.K = req_num(jh, "K", c);
            h.p = num(jh, "p", 2.5, c);
            h.mu = num(jh, "mu", 0.0, c);
            h.width = num(jh, "width", 0.0, c);
            h.strike_x = num(jh, "strike_x", 0.0, c);
            h.velocity = req_num(jh, "velocity", c);
            h.gap = num(jh, "gap", 0.0, c);
            h.wall = boolean(jh, "wall", false, c);
            if (jh.contains("strings")) {
                for (const auto& v : jh["strings"]) {
                    if (!v.is_number_integer()) {
                        throw RefError(c + ".strings: expected an array of string indices");
                    }
                    h.strings.push_back(v.get<int>());
                }
            }
            sc.hammers.push_back(std::move(h));
        }
    }
    if (doc.contains("bridges")) {
        std::size_t idx = 0;
        for (const auto& jb : doc["bridges"]) {
            const std::string c = ctx + ".bridges[" + std::to_string(idx++) + "]";
            check_keys(jb, {"position", "theta_b", "conductance_h", "model"}, c);
            BridgeSpec b;
            if (jb.contains("position")) {
                b.desc.position = point(jb["position"], c + ".position");
            }
            b.desc.thetaB = num(jb, "theta_b", 0.0, c);
            b.desc.conductanceH = num(jb, "conductance_h", 0.0, c);
            if (jb.contains("model")) {
                const auto& jm = jb["model"];
                const std::string type = text(jm, "type", "", c + ".model");
                if (type == "mass_spring_dashpot") {
                    check_keys(jm, {"type", "mass", "stiffness", "damping"}, c + ".model");
                    b.kind = BridgeModelKind::MassSpringDashpot;
                    b.mass = num(jm, "mass", 0.0, c + ".model");
                    b.stiffness = num(jm, "stiffness", 0.0, c + ".model");
                    b.damping = num(jm, "damping", 0.0, c + ".model");
                } else if (type == "conductance") {
                    check_keys(jm, {"type", "G"}, c + ".model");
                    b.kind = BridgeModelKind::Conductance;
                    b.conductance = req_num(jm, "G", c + ".model");
                } else {
                    throw RefError(c +
                                   ".model.type: expected \"mass_spring_dashpot\" or "
                                   "\"conductance\"");
                }
            }
            sc.bridges.push_back(b);
        }
    }
    if (doc.contains("plate")) {
        sc.plate = plate_spec(doc["plate"], ctx + ".plate");
    }
    if (doc.contains("barriers")) {
        std::size_t idx = 0;
        for (const auto& jb : doc["barriers"]) {
            const std::string c = ctx + ".barriers[" + std::to_string(idx++) + "]";
            check_keys(jb, {"string", "side", "K", "alpha", "mu", "x", "b", "w"}, c);
            BarrierSpec b;
            b.string = integer(jb, "string", 0, c);
            b.params.side = integer(jb, "side", 1, c);
            b.params.K = req_num(jb, "K", c);
            b.params.alpha = num(jb, "alpha", 2.0, c);
            b.params.muB = num(jb, "mu", 0.0, c);
            b.params.x = numbers(jb, "x", c);
            b.params.b = numbers(jb, "b", c);
            b.params.w = numbers(jb, "w", c);
            sc.barriers.push_back(std::move(b));
        }
    }
    if (doc.contains("observations")) {
        std::size_t idx = 0;
        for (const auto& jo : doc["observations"]) {
            const std::string c = ctx + ".observations[" + std::to_string(idx++) + "]";
            check_keys(jo,
                       {"name", "kind", "string", "bridge", "hammer", "barrier", "x",
                        "polarisation", "point"},
                       c);
            ObservationSpec o;
            o.kind = observation_kind(text(jo, "kind", "", c), c);
            o.name = text(jo, "name", "obs" + std::to_string(idx - 1), c);
            o.index = std::max({integer(jo, "string", 0, c), integer(jo, "bridge", 0, c),
                                integer(jo, "hammer", 0, c), integer(jo, "barrier", 0, c)});
            o.x = num(jo, "x", 0.0, c);
            const std::string pol = text(jo, "polarisation", "v", c);
            if (pol != "v" && pol != "h") {
                throw RefError(c + R"(.polarisation: expected "v" or "h")");
            }
            o.polarisation = pol == "h" ? 1 : 0;
            if (jo.contains("point")) {
                o.point = point(jo["point"], c + ".point");
            }
            sc.observations.push_back(std::move(o));
        }
    }
    validate_scenario(sc);
    return sc;
}

Scenario read_scenario_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw RefError("cannot open scenario file '" + path + "'");
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return parse_scenario(ss.str());
}

void validate_scenario(const Scenario& sc) {
    if (sc.schema_version != 1) {
        throw RefError("scenario.schema_version: expected 1");
    }
    if (!(sc.fs > 0.0) || sc.oversampling < 1 || !(sc.duration > 0.0)) {
        throw RefError("scenario: fs, oversampling and duration must be positive");
    }
    if (!(sc.theta >= 0.25) || sc.theta > 1.0) {
        throw RefError("scenario.theta: must lie in [1/4, 1] (the unconditionally stable range)");
    }
    const int ns = static_cast<int>(sc.strings.size());
    const int nb = static_cast<int>(sc.bridges.size());
    for (std::size_t i = 0; i < sc.strings.size(); ++i) {
        const auto& s = sc.strings[i];
        const std::string c = "scenario.strings[" + std::to_string(i) + "]";
        if (s.intervals < 4) {
            throw RefError(c + ".grid.intervals: must be at least 4");
        }
        if (s.bridge >= nb) {
            throw RefError(c + ".bridge: no such bridge point");
        }
        if (s.params.boundary == Boundary::Clamped && s.bridge >= 0) {
            throw RefError(c +
                           ".params.boundary: clamped ends are modelled only without a bridge "
                           "(the bridge end moves)");
        }
        if ((s.geometric.tension_modulation || s.geometric.longitudinal)) {
            if (ns != 1 || s.bridge >= 0 || !sc.hammers.empty() || !sc.barriers.empty() ||
                sc.plate.has_value()) {
                throw RefError(c +
                               ".geometric: tension modulation and longitudinal waves are "
                               "supported for a single fixed-end string without hammer, "
                               "barrier, bridge or plate");
            }
            if (!s.initial.enabled) {
                throw RefError(c + ".geometric: needs initial_displacement");
            }
        }
        if (s.geometric.longitudinal && !s.geometric.tension_modulation) {
            throw RefError(c + ".geometric.longitudinal: requires tension_modulation");
        }
    }
    for (std::size_t i = 0; i < sc.hammers.size(); ++i) {
        const auto& h = sc.hammers[i];
        const std::string c = "scenario.hammers[" + std::to_string(i) + "]";
        if (!(h.mass > 0.0) || !(h.K > 0.0) || !(h.p > 1.0) || h.mu < 0.0) {
            throw RefError(c + ": mass and K must be positive, p > 1, mu >= 0");
        }
        if (!h.wall && h.strings.empty()) {
            throw RefError(c + ".strings: a hammer needs at least one string or wall = true");
        }
        for (const int s : h.strings) {
            if (s < 0 || s >= ns) {
                throw RefError(c + ".strings: no such string");
            }
        }
    }
    if (sc.plate.has_value()) {
        if (sc.bridges.empty() && !sc.strings.empty()) {
            throw RefError("scenario.plate: strings couple to the plate through bridge points");
        }
        for (const auto& s : sc.strings) {
            if (s.bridge < 0) {
                throw RefError(
                    "scenario.strings[].bridge: with a plate every string needs a "
                    "bridge point");
            }
        }
    }
    for (std::size_t i = 0; i < sc.barriers.size(); ++i) {
        const auto& b = sc.barriers[i];
        const std::string c = "scenario.barriers[" + std::to_string(i) + "]";
        if (b.string < 0 || b.string >= ns) {
            throw RefError(c + ".string: no such string");
        }
        if (b.params.x.empty() || b.params.x.size() != b.params.b.size() ||
            b.params.x.size() != b.params.w.size()) {
            throw RefError(c + ": x, b and w must be non-empty arrays of equal length");
        }
        if (b.params.side != 1 && b.params.side != -1) {
            throw RefError(c + ".side: expected 1 or -1");
        }
        if (!(b.params.K > 0.0) || !(b.params.alpha > 1.0) || b.params.muB < 0.0) {
            throw RefError(c + ": K must be positive, alpha > 1, mu >= 0");
        }
        const auto& s = sc.strings[static_cast<std::size_t>(b.string)];
        const double h = s.params.L / s.intervals;
        for (const double x : b.params.x) {
            if (!(x >= h) || !(x <= (s.intervals - 1) * h)) {
                throw RefError(c + ".x: positions must lie within [h, L - h] of the string grid");
            }
        }
    }
    for (std::size_t i = 0; i < sc.observations.size(); ++i) {
        const auto& o = sc.observations[i];
        const std::string c = "scenario.observations[" + std::to_string(i) + "]";
        switch (o.kind) {
            case ObservationKind::StringDisplacement:
            case ObservationKind::StringVelocity:
            case ObservationKind::LongitudinalBridgeForce:
            case ObservationKind::LongitudinalDisplacement:
                if (o.index < 0 || o.index >= ns) {
                    throw RefError(c + ".string: no such string");
                }
                break;
            case ObservationKind::BridgeForce:
            case ObservationKind::BridgeForcePerp:
            case ObservationKind::BridgeDisplacement:
            case ObservationKind::BridgeVelocity:
                if (o.index < 0 || o.index >= nb) {
                    throw RefError(c + ".bridge: no such bridge point");
                }
                break;
            case ObservationKind::HammerForce:
            case ObservationKind::HammerPenetration:
            case ObservationKind::HammerPosition:
                if (o.index < 0 || o.index >= static_cast<int>(sc.hammers.size())) {
                    throw RefError(c + ".hammer: no such hammer");
                }
                break;
            case ObservationKind::BarrierForce:
                if (o.index < 0 || o.index >= static_cast<int>(sc.barriers.size())) {
                    throw RefError(c + ".barrier: no such barrier");
                }
                break;
            case ObservationKind::PlateDisplacement:
                if (!sc.plate.has_value()) {
                    throw RefError(c + ": plate observation without a plate");
                }
                break;
        }
    }
}

}  // namespace pme::ref

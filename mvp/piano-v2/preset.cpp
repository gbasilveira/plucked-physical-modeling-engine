// SPDX-License-Identifier: MIT
#include "preset.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace piano_mvp_v2 {
namespace {
ReportRow rowForKey(int key) {
    if (key >= 24 && key <= 107) return kReportRows[key - 24];
    const auto& edge = key < 24 ? kReportRows.front() : kReportRows.back();
    const auto& neighbour = key < 24 ? kReportRows[1] : kReportRows[82];
    const double steps = key < 24 ? 24 - key : key - 107;
    const auto extend = [steps](double a, double b) { return a * std::pow(a / b, steps); };
    auto row = edge;
    row.midi_key = key;
    row.frequency_hz = extend(edge.frequency_hz, neighbour.frequency_hz);
    row.length_m = extend(edge.length_m, neighbour.length_m);
    row.diameter_mm = extend(edge.diameter_mm, neighbour.diameter_mm);
    row.effective_density_kg_m3 = extend(edge.effective_density_kg_m3, neighbour.effective_density_kg_m3);
    row.tension_n = extend(edge.tension_n, neighbour.tension_n);
    const double i = key - 20; // A0 is key 1 in the report's equations (2)–(6).
    row.transverse_loss_per_s = std::max(0.0, 0.005 * i - 0.015);
    row.transverse_viscosity_s = 2.78e-11 * i + 1.5274e-9;
    row.hammer_mass_g = 1000 * (-6.2348e-5 * i + 0.0112);
    row.felt_exponent = 2.4295e-4 * i * i - 0.007703 * i + 2.337;
    row.felt_stiffness = std::pow(10.0, 0.053097 * i + 7.6425);
    row.strike_m = 0.12 * row.length_m;
    // Retained for provenance only; the incompatible report dissipation is not used.
    row.felt_dissipation = extend(edge.felt_dissipation, neighbour.felt_dissipation);
    return row;
}
}
NoteParameters parametersForKey(int key) {
    if (key < 21 || key > 108) throw std::out_of_range("piano key must be 21..108");
    NoteParameters result;
    result.source = rowForKey(key);
    result.extrapolated = key < 24 || key > 107;
    const auto& r = result.source;
    auto& p = result.string;
    p.L = r.length_m; p.dCore = p.dOuter = r.diameter_mm * 0.001;
    p.rhoCore = r.effective_density_kg_m3;
    p.ECore = 2.02e11; // Report section 3.2; bass uses homogenised effective density.
    const double area = std::numbers::pi * p.dCore * p.dCore / 4;
    const double ei = p.ECore * std::numbers::pi * std::pow(p.dCore, 4) / 64;
    // compileString accepts pitch, not tension. Invert its own tension-for-pitch formula
    // so the published T0 survives exactly; frequency_hz is rounded source metadata.
    p.f1Target = std::sqrt((r.tension_n + std::numbers::pi * std::numbers::pi * ei / (p.L * p.L)) /
                          (4 * p.L * p.L * p.rhoCore * area));
    auto& h = result.hammer;
    h.mass = r.hammer_mass_g * 0.001; h.K = r.felt_stiffness; h.p = r.felt_exponent;
    h.strikeRatio = r.strike_m / r.length_m;
    // Keep the existing felt dissipator and window as declared MVP assumptions.
    // Report RH multiplies d(delta^p)/dt, unlike HC's K*mu*delta^p*delta_dot.
    h.muHC = 0.5; h.width = std::min(0.01, p.L * 0.04);
    return result;
}
pme::Status compilePresetString(const NoteParameters& description, double fs, pme::StringModes& out) {
    auto p = description.string;
    const double limit = std::min(12000.0, fs * 0.42);
    pme::StringModes modes;
    if (const auto status = pme::compileString(p, fs, limit, {}, modes); status != pme::Status::Ok)
        return status;
    // Project JASA 2013 eq. (8) onto the pinned EB modes:
    // 2*rhoA*Ru*u_t - 2*T*eta*u_txx -> sigma_n = Ru + eta*T/rhoA*k_n^2.
    // The original paper's full Timoshenko/shear/longitudinal system is not reproduced.
    for (double k : modes.k)
        p.sigmaOverride.push_back(description.source.transverse_loss_per_s +
            description.source.transverse_viscosity_s * modes.tension / modes.rhoA * k * k);
    return pme::compileString(p, fs, limit, {}, out);
}
}

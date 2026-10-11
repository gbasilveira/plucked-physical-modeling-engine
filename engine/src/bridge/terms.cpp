// SPDX-License-Identifier: MIT
// Bridge terms of one segment (A4-R1): SPEC-BRG "Modal equation with a moving support", "Bridge
// force" and "Contract". See docs/dev/bridge.md.
#include <cmath>
#include <cstddef>
#include <utility>
#include <vector>

#include "pme/bridge.hpp"
#include "pme/bridge/terms.hpp"

namespace pme {

namespace {

bool finitePositive(double v) noexcept { return std::isfinite(v) && v > 0.0; }

// Shared validation for all term builders: ADR-0006 InvalidArgument conditions.
Status validateModes(const StringModes& m) noexcept {
    const std::size_t n = m.k.size();
    if (n == 0 || m.omega.size() != n || m.sigma.size() != n || m.oscV.size() != n ||
        m.oscH.size() != n) {
        return Status::InvalidArgument;
    }
    if (!finitePositive(m.L) || !finitePositive(m.tension) || !finitePositive(m.rhoA)) {
        return Status::InvalidArgument;
    }
    for (std::size_t i = 0; i < n; ++i) {
        if (!finitePositive(m.k[i]) || !std::isfinite(m.omega[i]) || !std::isfinite(m.sigma[i]) ||
            !std::isfinite(m.oscV[i].omegaHat2) || !std::isfinite(m.oscH[i].omegaHat2)) {
            return Status::InvalidArgument;
        }
    }
    return Status::Ok;
}

}  // namespace

namespace bridge {

Status liftingOverlaps(const StringModes& modes, BridgeEnd end, std::vector<double>& g) {
    if (const Status s = validateModes(modes); s != Status::Ok) {
        return s;
    }
    const std::size_t n = modes.k.size();
    const double root = std::sqrt(2.0 / modes.L);
    std::vector<double> tmp(n);
    for (std::size_t i = 0; i < n; ++i) {
        // SPEC-BRG Modal equation with a moving support: g_n; mode index n = i + 1.
        const double sign = (end == BridgeEnd::AtL && (i % 2U) == 1U) ? -1.0 : 1.0;
        tmp[i] = sign * root / modes.k[i];
        if (!std::isfinite(tmp[i])) {
            return Status::InvalidArgument;
        }
    }
    g = std::move(tmp);
    return Status::Ok;
}

Status forceCoefficients(const StringModes& modes, BridgeEnd end, Polarisation pol,
                         std::vector<double>& a) {
    std::vector<double> g;
    if (const Status s = liftingOverlaps(modes, end, g); s != Status::Ok) {
        return s;
    }
    const std::size_t n = g.size();
    std::vector<double> tmp(n);
    for (std::size_t i = 0; i < n; ++i) {
        // SPEC-BRG Bridge force: a_n = rhoA omegaHat_n^2 g_n with the discrete omegaHat^2.
        const double w2 =
            (pol == Polarisation::Vertical) ? modes.oscV[i].omegaHat2 : modes.oscH[i].omegaHat2;
        tmp[i] = modes.rhoA * w2 * g[i];
        if (!std::isfinite(tmp[i])) {
            return Status::InvalidArgument;
        }
    }
    a = std::move(tmp);
    return Status::Ok;
}

Status horizontalForceCoefficients(const StringModes& modes, BridgeEnd end,
                                   std::vector<float>& aH) {
    std::vector<double> a;
    if (const Status s = forceCoefficients(modes, end, Polarisation::Horizontal, a);
        s != Status::Ok) {
        return s;
    }
    std::vector<float> tmp(a.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        tmp[i] = static_cast<float>(a[i]);  // float64 then float32 (SPEC-CONV)
    }
    aH = std::move(tmp);
    return Status::Ok;
}

}  // namespace bridge

Status compileBridgeTerms(const StringModes& modes, BridgeEnd end, SegmentBridgeTerms& out) {
    std::vector<double> g;
    std::vector<double> a;
    if (const Status s = bridge::liftingOverlaps(modes, end, g); s != Status::Ok) {
        return s;
    }
    if (const Status s = bridge::forceCoefficients(modes, end, bridge::Polarisation::Vertical, a);
        s != Status::Ok) {
        return s;
    }
    SegmentBridgeTerms terms;
    terms.end = end;
    terms.g.resize(g.size());
    terms.a.resize(a.size());
    for (std::size_t i = 0; i < g.size(); ++i) {
        terms.g[i] = static_cast<float>(g[i]);  // float64 then float32 (SPEC-CONV)
        terms.a[i] = static_cast<float>(a[i]);
    }
    terms.tOverL = modes.tension / modes.L;  // SPEC-BRG Bridge force: T / L
    out = std::move(terms);
    return Status::Ok;
}

}  // namespace pme

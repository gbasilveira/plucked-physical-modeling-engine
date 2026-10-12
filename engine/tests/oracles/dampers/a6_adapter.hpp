// SPDX-License-Identifier: MIT
// ORC-13 seam to A6 (see "A6 SEAM" in test_orc13_damper_diagonal.cpp): per-mode coefficients of the
// diagonal damper at engagement ModeInput::e, from A6's documented API (docs/dev/dampers.md,
// pme/dampers/diagonal.hpp). Expected values in the test never come from here.
#pragma once

#include <cstddef>
#include <limits>

#include "orc13_input.hpp"
#include "pme/dampers.hpp"
#include "pme/dampers/diagonal.hpp"
#include "pme/osc.hpp"
#include "pme/status.hpp"
#include "pme/string.hpp"

namespace pme::oracle::dmp {

inline pme::Status a6Coefficients(const ModeInput& in, pme::OscCoeffs& out) {
    pme::StringModes modes;
    pme::Status st =
        pme::compileString(*in.string, in.fs, std::numeric_limits<double>::infinity(), {}, modes);
    if (st != pme::Status::Ok) {
        return st;
    }
    pme::DamperParams dp;
    dp.xRatio = in.xRatio;
    dp.width = in.width;
    dp.Kd = in.Kd;
    dp.Rd = in.Rd;
    pme::dmp::DiagonalDamper damper;
    st = damper.configure(modes, dp);
    if (st != pme::Status::Ok) {
        return st;
    }
    pme::osc::OscCoeffsF64 c{};
    st = damper.coefficients(static_cast<std::size_t>(in.n - 1), in.e, c);
    if (st != pme::Status::Ok) {
        return st;
    }
    out.beta = static_cast<float>(c.beta);
    out.kappa = static_cast<float>(c.kappa);
    out.gamma = static_cast<float>(c.gamma);
    out.omegaHat2 = c.omegaHat2;
    out.sigmaHat = c.sigmaHat;
    return pme::Status::Ok;
}

}  // namespace pme::oracle::dmp

// SPDX-License-Identifier: MIT
// Time-domain reference simulation (P0.5-R1 to R6): stiff damped strings, hammers, bridges, an
// orthotropic plate, barriers and geometric nonlinearity, all in physical space, float64.
//
// Scheme summary (details and sources in docs/dev/reference-solver.md):
//  - Linear parts: the implicit theta-scheme of Bilbao, "Numerical Sound Synthesis" (2009),
//    sections 7.2 (stiff string) and 12.1 (plate), written for the full coupled system
//        M d_tt x + C d_t. x + K (x + theta k^2 d_tt x) = f,
//    with a discrete energy identity. The strings are eliminated by banded Cholesky solves and
//    Schur complements onto the bridge (or plate) degrees of freedom.
//  - Contacts (hammer, barrier): the two-step discrete gradient of the potential and a Newton
//    solve of the (small) contact system, section 8.1 style; converged to 1e-14 relative or 50
//    iterations (non-convergence throws).
//  - Tension modulation (Kirchhoff-Carrier) and longitudinal waves: section 8.1 and 8.2 style.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "pme/ref/scenario.hpp"

namespace pme::ref {

struct Signal {
    std::string name;
    std::vector<double> data;  // one value per simulation step, n = 0 .. steps - 1
};

// Energy bookkeeping of the discrete scheme (physical energies, J). total[n], n = 0 .. steps, is
// the energy H^(n-1/2) of the state pair (x^n, x^(n-1)); total[0] is the initial energy. loss[n]
// and balance[n], n = 0 .. steps - 1, belong to step n -> n + 1: loss[n] is the energy dissipated
// (>= 0) and balance[n] = total[n+1] - total[n] + loss[n], zero up to round-off.
struct EnergyTrace {
    std::vector<double> total;
    std::vector<double> string;  // strings (kinetic + elastic)
    std::vector<double> bridge;  // bridge masses and springs
    std::vector<double> plate;
    std::vector<double> hammer;   // hammer kinetic energy
    std::vector<double> contact;  // felt and barrier potentials
    std::vector<double> loss;
    std::vector<double> balance;
};

struct NewtonStats {
    int max_iterations = 0;
    double max_relative_residual = 0.0;
    std::size_t contact_steps = 0;  // steps that needed the Newton solve
};

struct Results {
    std::string name;
    double fs = 0.0;
    int oversampling = 1;
    double sim_rate = 0.0;  // fs * oversampling (Hz)
    double dt = 0.0;
    std::size_t steps = 0;
    std::vector<Signal> signals;
    EnergyTrace energy;
    NewtonStats newton;

    [[nodiscard]] const Signal* find(const std::string& name) const;
    // Throws RefError if the signal does not exist.
    [[nodiscard]] const std::vector<double>& signal(const std::string& name) const;
};

struct SimulationOptions {
    bool record_energy = true;  // energy bookkeeping costs about as much as the time stepping
};

// Runs a scenario. Throws RefError for unsupported inputs and non-convergence.
Results simulate(const Scenario& scenario, const SimulationOptions& options = {});

// Maximum over n of |H^(n+1) - H^n| / |H^0| (P0.5-T2).
double max_relative_energy_step(const EnergyTrace& energy);

// Maximum over n of |balance[n]| / |H^0|.
double max_relative_energy_imbalance(const EnergyTrace& energy);

}  // namespace pme::ref

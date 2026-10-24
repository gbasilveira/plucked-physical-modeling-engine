// SPDX-License-Identifier: MIT
#pragma once

/// @file body.hpp
/// Soundboard and body description, modal data, local bridge model (SPEC-BODY; SPEC-API).

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "pme/status.hpp"
#include "pme/types.hpp"

namespace pme {

/// Where a body's modal data comes from (SPEC-API `BodyParams::source`, SPEC-BODY).
enum class BodySource : std::uint8_t {
    PlateModel,  ///< Orthotropic plate solved offline (SPEC-BODY "Plate model").
    ModalTable,  ///< Precomputed modal table referenced by hash (SPEC-INST "Instrument file").
    MeasuredMobilityFit,  ///< Modes fitted to measured mobility.
};

/// A rib: a straight beam attached to the plate (SPEC-BODY "Plate model", SI-0003).
struct RibDesc {
    Point start;       ///< Start of the beam axis (m).
    Point end;         ///< End of the beam axis (m).
    double width{};    ///< Beam width (m).
    double height{};   ///< Beam height (m).
    double youngs{};   ///< Young's modulus along the beam (Pa).
    double density{};  ///< Density (kg/m^3).
};

/// A bridge attached to the plate: a polyline beam of rectangular section (SPEC-BODY "Plate
/// model", SI-0003).
struct BridgeBeamDesc {
    std::vector<Point> path;  ///< Polyline vertices in the body frame (m), z = 0, at least 2.
    double width{};           ///< Section width (m).
    double height{};          ///< Section height (m).
    double youngs{};          ///< Young's modulus along the bridge (Pa).
    double density{};         ///< Density (kg/m^3).
};

/// Plate geometry and wood data (SPEC-BODY "Plate model", SI-0003). Members follow the symbols of
/// the Kirchhoff form.
struct PlateParams {
    double lengthX{};     ///< Extent along +x (m); with an empty `outline` the plate is the
                          ///< rectangle (0, 0)-(lengthX, lengthY); ignored otherwise.
    double lengthY{};     ///< Extent along +y (m); see `lengthX`.
    double thickness{};   ///< Uniform thickness h (m), used when `thicknessGrid` is empty.
    double grainAngle{};  ///< Grain angle theta_g (rad), SPEC-BODY.
    double youngsL{};     ///< E_L along the grain (Pa) at reference moisture, SPEC-BODY.
    double youngsR{};     ///< E_R across the grain (Pa) at reference moisture, SPEC-BODY.
    double shearLR{};     ///< G_LR (Pa) at reference moisture, SPEC-BODY.
    double poissonLR{};   ///< nu_LR (dimensionless), SPEC-BODY.
    double poissonRL{};   ///< nu_RL (dimensionless), SPEC-BODY.
    double density{};     ///< Density rho (kg/m^3) at reference moisture, SPEC-BODY.
    double moisture{};    ///< Moisture content MC (dimensionless fraction), SPEC-BODY.
    double dEdMcL{};      ///< Linear coefficient of E_L with MC (Pa per unit MC), SPEC-BODY.
    double dEdMcR{};      ///< Linear coefficient of E_R with MC (Pa per unit MC), SPEC-BODY.
    double dGdMc{};       ///< Linear coefficient of G_LR with MC (Pa per unit MC), SPEC-BODY.
    double dRhodMc{};  ///< Linear coefficient of density with MC (kg/m^3 per unit MC), SPEC-BODY.
    double lossFactor{};         ///< Wood loss factor eta (dimensionless), SPEC-BODY "Modal data".
    double rimStiffness{};       ///< Translational rim support stiffness per unit rim length k_rim
                                 ///< (N/m^2), SPEC-BODY.
    double fMax{};               ///< Highest kept body mode frequency f_max (Hz), SPEC-BODY.
    std::vector<RibDesc> ribs;   ///< Ribs as straight attached beams, SPEC-BODY.
    std::vector<Point> outline;  ///< Outline polygon in the body frame (m), z = 0, vertices in
                                 ///< order, not closed; empty = the lengthX x lengthY rectangle.
    int thicknessNx{};           ///< Thickness grid points along x (>= 2 when the grid is used).
    int thicknessNy{};           ///< Thickness grid points along y (>= 2 when the grid is used).
    std::vector<double> thicknessGrid;    ///< h (m) on a regular Nx x Ny grid over the outline's
                                          ///< bounding box, row-major [j * Nx + i], bilinearly
                                          ///< interpolated; empty = uniform `thickness`.
    std::vector<BridgeBeamDesc> bridges;  ///< Bridges as polyline beams, SPEC-BODY.
    double rimRotationalStiffness{};      ///< Rotational rim stiffness per unit length k_rim,theta
                                          ///< (N m/rad per m); 0 = none (MAY, SPEC-BODY).
};

/// Description of a body (SPEC-API `BodyParams`; SPEC-BODY). Sample-rate independent.
struct BodyParams {
    BodySource source{BodySource::PlateModel};  ///< Which of the data sources below is used.
    PlateParams plate;           ///< Used when source == PlateModel (SPEC-BODY "Plate model").
    std::string modalTableHash;  ///< Hash of the binary modal blob when source != PlateModel
                                 ///< (SPEC-INST "Instrument file"). Provisional encoding.
};

/// Compiled modal data of a body (SPEC-BODY "Modal data (offline output)" and "Radiation
/// weights"). Immutable once built. Provisional layout: SPEC-BODY names the contents only.
struct BodyModes {
    int numModes{};             ///< K, number of body modes.
    int numPoints{};            ///< Number of bridge points the shapes were sampled at.
    int numChannels{};          ///< Number of radiation channels.
    std::vector<double> omega;  ///< Omega_k (rad/s), length K.
    std::vector<double> sigma;  ///< sigma_k = eta Omega_k / 2 + sigma_k,rad (1/s), length K.
    /// Phi_k(x_b), mass-normalised shape at bridge point b (kg^(-1/2)); phi[k * numPoints + b].
    std::vector<double> phi;
    /// alpha_c,k radiation weight of acceleration (SPEC-BODY "Radiation weights"); alpha[c * K +
    /// k].
    std::vector<double> alpha;
    /// beta_c,k radiation weight of velocity (SPEC-BODY "Radiation weights"); beta[c * K + k].
    std::vector<double> beta;
    std::vector<double>
        channelDelay;  ///< Per-channel delay tau_c = R_c / c_0 (s), length numChannels.
    std::vector<double> residualMobility;  ///< Mean residual mobility per bridge point (s/kg), from
                                           ///< which G_hf,b is derived. Provisional.
};

/// A source of local bridge modes: one bridge point's private model (SPEC-BODY "Local bridge
/// model (per bridge point, per sample, exact)"). Immutable once built. Oscillator coefficients
/// are not stored: buildLocalBridge has no sample rate, so compileInstrument derives them
/// (SPEC-OSC) from omega and sigma. Provisional layout.
struct LocalBridge {
    int point{};  ///< Bridge point index b.
    std::vector<int>
        modeIndex;  ///< Indices k of the M_b kept body modes (largest Phi_k(x_b)^2 below f_split).
    std::vector<double> omega;  ///< Omega_k (rad/s) of the kept modes.
    std::vector<double> sigma;  ///< sigma_k (1/s) of the kept modes.
    std::vector<double> phi;    ///< Phi_k(x_b) (kg^(-1/2)) of the kept modes.
    double conductanceHf{};     ///< G_hf,b (s/kg): mean residual mobility over [f_split, f_max].
    double fSplit{};            ///< f_split (Hz) used to build it.
};

/// Description of one radiation output channel (SPEC-API `ChannelDesc`; SPEC-BODY "Radiation
/// weights"). Provisional members. Per-channel gain is a structural property of the channel and
/// will be added here as `gainDb` by a later compatible ICR when D4 or E1 needs it (SPEC-API,
/// SI-0007).
struct ChannelDesc {
    std::string name;  ///< Human-readable channel name, for example "L".
    Point position;    ///< Listening position R_c (m), same frame as the body.
};

/// Compute a body's modes offline (SPEC-BODY "Contracts", SPEC-API). Slow.
///
/// @param params       The body description.
/// @param bridgePoints Positions where Phi_k is sampled.
/// @param channels     Radiation positions R_c.
/// @param out          Receives the modal data on success.
/// Thread: non-audio, offline. Allocates: yes. Failures: `InvalidArgument` for a non-physical
/// description; `NotImplemented` in the P0.4 stub.
[[nodiscard]] Status compileBody(const BodyParams& params, std::span<const Point> bridgePoints,
                                 std::span<const Point> channels, BodyModes& out);

/// Build one bridge point's local model (SPEC-BODY "Local bridge model", SPEC-API).
///
/// @param body     Compiled body modes.
/// @param point    Bridge point index b, 0 <= b < body.numPoints.
/// @param maxModes M_b, the largest number of private modes (64 SHOULD hold).
/// @param fSplit   f_split in Hz: kept modes lie below it; the conductance covers [fSplit, f_max].
/// @param out      Receives the local model on success.
/// Thread: non-audio. Allocates: yes. Failures: `InvalidArgument` if `point` is out of range or
/// maxModes < 0; `NotImplemented` in the P0.4 stub.
[[nodiscard]] Status buildLocalBridge(const BodyModes& body, int point, int maxModes, double fSplit,
                                      LocalBridge& out);

}  // namespace pme

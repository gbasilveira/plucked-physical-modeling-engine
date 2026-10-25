// SPDX-License-Identifier: MIT
#include "bridge_model.hpp"
#include "grand_board.hpp"
#include "preset.hpp"
#include "sympathetic_string.hpp"
#include "pme/bridge/bridge_point.hpp"
#include "pme/bridge/modal_segment.hpp"
#include "pme/simd/denormals.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <numbers>
#include <stdexcept>
#include <vector>

static void require(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
static void check(int key, double fs, bool lossless) {
    const auto preset = piano_mvp_v2::parametersForKey(key);
    pme::StringModes modes;
    if (lossless) {
        auto p = preset.string;
        require(pme::compileString(p,fs,std::min(12000.0,fs*0.42),{},modes) == pme::Status::Ok,
                "lossless string compile failed");
    } else {
        require(piano_mvp_v2::compilePresetString(preset,fs,modes) == pme::Status::Ok,
                "damped string compile failed");
    }
    const double pan = 0.2+0.6*(key-21)/87.0;
    pme::bridge::BridgePoint<double> point;
    require(point.configure({},piano_mvp_v4::localBridgeModel(pan,lossless),fs) == pme::Status::Ok,
            "bridge configure failed");
    pme::bridge::ModalSegment<double> segment;
    require(segment.configure(modes,pme::BridgeEnd::AtL) == pme::Status::Ok,"segment configure failed");
    for (std::size_t n = 0; n < segment.modeCount(); ++n)
        segment.qV()[n] = 1e-5/std::pow(static_cast<double>(n+1),2);
    const auto step = [&] {
        point.addForce(segment.forceV(point.wV()),0);
        point.advance(); segment.advance(point.accelV(),0);
    };
    const auto energy = [&] {
        return point.modeEnergy()+segment.energy(point.wV(),point.incrementV(),0,0);
    };
    step(); const double initial = energy();
    require(initial > 0,"non-positive coupled energy");
    double deviation = 0, maximum = initial;
    for (int i = 0; i < 20000; ++i) {
        step();
        if (i%32 == 0) {
            const double e = energy();
            require(std::isfinite(e) && e >= 0,"unstable or negative coupled energy");
            maximum = std::max(maximum,e);
            deviation = std::max(deviation,std::abs(e/initial-1));
        }
    }
    const double final = energy()/initial;
    if (lossless) require(deviation < 1e-6,"lossless bridge/string energy drift");
    else require(maximum/initial < 1+1e-6 && final < 1,"damped bridge injects net energy");
    std::cout << "key " << key << ", " << fs << " Hz, lossless=" << lossless
              << ": max deviation " << deviation << ", final/initial " << final << '\n';
}
// 0.5.1: many strings on one shared board, same partitioned scheme as BridgePoint (forces F^n
// from q^n and w^n; modes advance; strings see delta_tt w^n). Energy = coupled board modes +
// every string segment. Only the key-60 string is excited; octave and fifth neighbours must
// receive energy through the board.
static void sharedCheck(bool lossless, bool grand, bool leak = false) {
    const double fs = 48000;
    const int keys[] = {36, 48, 55, 60, 64, 67, 72, 84};
    constexpr int n = 8;
    std::vector<double> phi; piano_mvp_v4::buildParticipation(grand, phi);
    piano_mvp_v4::GrandBoard board;
    require(board.configure(fs, phi, piano_mvp_v4::kLocalBridgeModes, lossless, false), "board configure failed");
    std::vector<pme::bridge::ModalSegment<double>> segments(n);
    for (int s = 0; s < n; ++s) {
        pme::StringModes modes;
        const auto preset = piano_mvp_v2::parametersForKey(keys[s]);
        if (lossless) {
            auto p = preset.string;
            require(pme::compileString(p,fs,std::min(12000.0,fs*0.42),{},modes) == pme::Status::Ok, "string compile failed");
        } else require(piano_mvp_v2::compilePresetString(preset,fs,modes) == pme::Status::Ok, "string compile failed");
        require(segments[s].configure(modes,pme::BridgeEnd::AtL) == pme::Status::Ok, "segment configure failed");
    }
    for (std::size_t m = 0; m < segments[3].modeCount(); ++m)
        segments[3].qV()[m] = 1e-5/std::pow(static_cast<double>(m+1),2);
    // 0.5.3-T2 leaky damper on the seven unexcited strings: every step their q and u are scaled by
    // exp(-sigma_d(f_n)/fs) <= 1 (the felt's added damping, same law as the plug-in's).
    std::vector<std::vector<double>> factors(n);
    if (leak)
        for (int s = 0; s < n; ++s) if (s != 3) {
            pme::StringModes modes;
            require(piano_mvp_v2::compilePresetString(piano_mvp_v2::parametersForKey(keys[s]),fs,modes) == pme::Status::Ok, "string compile failed");
            for (std::size_t m = 0; m < segments[s].modeCount(); ++m)
                factors[s].push_back(piano_mvp_v4::SympatheticString::leakFactor(modes.omega[m]/(2*std::numbers::pi), fs, 2.0, 0.0, 200.0));
        }
    double w[n]{}, dwPrev[n]{}, dw[n]{};
    double sympathetic = 0;
    const auto step = [&] {
        for (int s = 0; s < n; ++s) board.addForce(keys[s]-21, segments[s].forceV(w[s]));
        board.advanceCoupled();
        for (int s = 0; s < n; ++s) {
            const std::size_t port = keys[s]-21;
            dw[s] = board.portIncrement(port); w[s] = board.portDisplacement(port);
            segments[s].advance((dw[s]-dwPrev[s]) * fs * fs, 0);
            dwPrev[s] = dw[s];
            if (leak && s != 3)
                for (std::size_t m = 0; m < factors[s].size(); ++m) { segments[s].qV()[m] *= factors[s][m]; segments[s].uV()[m] *= factors[s][m]; }
        }
    };
    const auto energy = [&](double* own = nullptr) {
        double e = board.coupledEnergy(), others = 0;
        for (int s = 0; s < n; ++s) {
            const double se = segments[s].energy(w[s], dw[s], 0, 0);
            e += se; if (s != 3) others += se;
        }
        if (own) *own = others;
        return e;
    };
    step(); const double initial = energy();
    require(initial > 0, "non-positive shared energy");
    double deviation = 0, maximum = initial;
    for (int i = 0; i < 40000; ++i) {
        step();
        if (i%32 == 0) {
            double others; const double e = energy(&others);
            require(std::isfinite(e) && e >= 0, "unstable or negative shared-board energy");
            maximum = std::max(maximum, e);
            deviation = std::max(deviation, std::abs(e/initial-1));
            sympathetic = std::max(sympathetic, others/initial);
        }
    }
    const double final = energy()/initial;
    if (lossless && !leak) require(deviation < 1e-6, "lossless shared-board energy drift");
    else require(maximum/initial < 1+1e-6 && final < 1, "damped shared board injects net energy");
    require(sympathetic > 1e-9, "no energy reached the other strings through the board");
    if (leak) require(maximum/initial < 1+1e-9 && final < 1, "leaky dampers injected net energy");
    std::cout << "shared board, 8 strings, lossless=" << lossless << " grand=" << grand << " leaky dampers=" << leak << ": max deviation "
              << deviation << ", final/initial " << final << ", peak energy in the 7 unexcited strings "
              << sympathetic << " of initial\n";
}
int main() {
    pme::simd::ScopedFlushDenormals denormals;
    try {
        for (double fs : {44100.,48000.,96000.}) {
            pme::StringModes modes;
            require(piano_mvp_v2::compilePresetString(piano_mvp_v2::parametersForKey(60),fs,modes)
                    ==pme::Status::Ok,"residual test preset failed");
            const auto before = modes;
            require(piano_mvp_v4::applyResidualBridgeLoss(modes,0.5)==pme::Status::Ok,
                    "residual loss compilation failed");
            require(modes.tension==before.tension && modes.omega==before.omega,
                    "residual loss changed string pitch/geometry");
            require(modes.sigma[0]-before.sigma[0]<0.01,
                    "omitted high modes excessively damp the fundamental");
            bool highLoss=false;
            for (std::size_t n=0;n<modes.omega.size();++n) {
                require(modes.sigma[n]>=before.sigma[n],"residual bridge injected energy");
                if (modes.omega[n]>2*std::numbers::pi*2000 &&
                    modes.sigma[n]-before.sigma[n]>0.1) highLoss=true;
                // An isolated string mode with these coefficients must lose energy.
                double q=1e-5,u=0;
                const auto& c=modes.oscV[n];
                const double e0=u*u+c.omegaHat2*q*q;
                for (int i=0;i<1000;++i) {
                    const double delta=c.beta*u-c.kappa*q;
                    q+=delta;u=delta;
                }
                // u here is a per-step displacement, so use compatible units.
                const double e1=u*u*fs*fs+c.omegaHat2*q*(q-u);
                require(std::isfinite(e1) && e1>=0 && e1<=e0*(1+1e-5),
                        "residual modal oscillator is not passive");
            }
            require(highLoss,"missing high-frequency bridge loss");
        }
        for (int key : {21,60,84}) for (bool lossless : {false,true}) check(key,48000,lossless);
        for (double fs : {44100.,96000.}) for (bool lossless : {false,true}) check(60,fs,lossless);
        for (bool lossless : {true,false}) for (bool grand : {false,true}) sharedCheck(lossless, grand);
        // 0.5.3-T2: passivity of the leaky damper (lossless board: any rise would be the damper's).
        for (bool lossless : {true,false}) sharedCheck(lossless, true, true);
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}

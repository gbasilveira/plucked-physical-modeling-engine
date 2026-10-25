// SPDX-License-Identifier: MIT
#include "bridge_model.hpp"
#include "preset.hpp"
#include "pme/bridge/bridge_point.hpp"
#include "pme/bridge/modal_segment.hpp"
#include "pme/simd/denormals.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

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
int main() {
    pme::simd::ScopedFlushDenormals denormals;
    try {
        for (int key : {21,60,84}) for (bool lossless : {false,true}) check(key,48000,lossless);
        for (double fs : {44100.,96000.}) for (bool lossless : {false,true}) check(60,fs,lossless);
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}

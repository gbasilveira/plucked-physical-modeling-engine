// SPDX-License-Identifier: MIT
#include "piano.hpp"
#include "pme/simd/denormals.hpp"
#include <iostream>
#include <stdexcept>
#include <string>
int main(int argc,char** argv) {
    try {
        pme::simd::ScopedFlushDenormals denormals;
        piano_mvp_v4::Piano piano;
        if (argc>2 || (argc==2 && std::string(argv[1])!="--previous"))
            throw std::runtime_error("usage: piano-hammer-audit [--previous]");
        piano.setSoftFelt(argc==1);
        if (!piano.prepare(48000)) throw std::runtime_error("prepare failed");
        std::cout << "key,velocity,contacts,last_contact_ms,total_contact_ms,peak_force_n\n";
        for (int key : {33,45,60,72,84,96}) {
          double previousDuration=1, previousForce=0;
          for (int velocity : {32,72,112}) {
            piano.reset();piano.noteOn(key,velocity/127.0);
            for (int i=0;i<4800;++i) { float l,r;piano.sample(l,r); }
            const auto d=piano.hammerDiagnostics(key);
            std::cout << key << ',' << velocity << ',' << d.contacts << ','
                      << 1000*d.lastContactDuration << ',' << 1000*d.totalContactTime
                      << ',' << d.lastPeakForce << '\n';
            if (d.contacts!=1 || piano.numericalFaults()) throw std::runtime_error("invalid contact");
            if (!(d.lastContactDuration<previousDuration && d.lastPeakForce>previousForce))
                throw std::runtime_error("hammer dynamics failed");
            previousDuration=d.lastContactDuration;previousForce=d.lastPeakForce;
          }
        }
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; }
}

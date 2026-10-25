// SPDX-License-Identifier: MIT
#include <lv2/core/lv2.h>
#include <lv2/atom/atom.h>
#include <lv2/midi/midi.h>
#include <lv2/urid/urid.h>
#include <dlfcn.h>
#include "piano.hpp"
#include "pme/simd/denormals.hpp"
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <new>
#include <stdexcept>

// Exported allocation interposition covers C++ operator new during the LV2 callback.
static thread_local bool inRun = false;
static std::size_t runAllocations = 0;
void* operator new(std::size_t size) {
    if (inRun) ++runAllocations;
    if (void* p = std::malloc(size ? size : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p,std::size_t) noexcept { std::free(p); }
void operator delete[](void* p,std::size_t) noexcept { std::free(p); }

static void require(bool ok,const char* text) { if (!ok) throw std::runtime_error(text); }
static LV2_URID map(void*,const char* uri) {
    if (!std::strcmp(uri,LV2_MIDI__MidiEvent)) return 1;
    if (!std::strcmp(uri,LV2_ATOM__Sequence)) return 2;
    if (!std::strcmp(uri,LV2_ATOM__frameTime)) return 3;
    return 4;
}
struct Message { std::int64_t frame; std::array<std::uint8_t,3> data; std::uint32_t size{3}, type{1}; };
struct Sequence {
    alignas(8) std::array<std::uint8_t,4096> bytes{};
    LV2_Atom_Sequence* get() { return reinterpret_cast<LV2_Atom_Sequence*>(bytes.data()); }
    void reset() { bytes.fill(0); get()->atom = {sizeof(LV2_Atom_Sequence_Body),2}; }
    void append(const Message& m) {
        LV2_Atom_Event event{}; event.time.frames = m.frame; event.body = {m.size,m.type};
        const auto offset = sizeof(LV2_Atom)+get()->atom.size;
        const auto padded = (m.size+7U)&~7U;
        require(offset+sizeof(event)+padded <= bytes.size(),"sequence overflow");
        std::memcpy(bytes.data()+offset,&event,sizeof(event));
        std::memcpy(bytes.data()+offset+sizeof(event),m.data.data(),std::min<std::size_t>(m.size,3));
        get()->atom.size += sizeof(event)+padded;
    }
};
// Independent reference calls exercise the actual synth with the specified event timing.
static void apply(piano_mvp_v4::Piano& p,const Message& m) {
    if (m.type != 1 || m.size < 3) return;
    const auto& d = m.data;
    if ((d[0]&0xf0) == 0x90) { if (d[2]) p.noteOn(d[1],d[2]/127.0); else p.noteOff(d[1]); }
    if ((d[0]&0xf0) == 0x80) p.noteOff(d[1]);
    if ((d[0]&0xf0) == 0xb0) {
        if (d[1] == 64) p.sustain(d[2] >= 64);
        if (d[1] == 120) p.reset();
        if (d[1] == 123) for (int key = 21; key <= 108; ++key) p.noteOff(key);
        if (d[1] == 121) p.sustain(false);
    }
}
static void exercise(const LV2_Descriptor* d,double fs) {
    LV2_URID_Map mapper{nullptr,map};
    LV2_Feature mapFeature{LV2_URID__map,&mapper};
    const LV2_Feature* features[]{&mapFeature,nullptr};
    const LV2_Feature* noFeatures[]{nullptr};
    require(!d->instantiate(d,fs,"",noFeatures),"missing URID map was accepted");
    require(!d->instantiate(d,0,"",features),"invalid sample rate was accepted");
    const auto instance = d->instantiate(d,fs,"",features);
    require(instance,"LV2 instantiate failed");
    piano_mvp_v4::Piano reference;
    require(reference.prepare(fs),"reference preparation failed");
    std::array<float,512> left{},right{};
    Sequence seq; seq.reset();
    d->connect_port(instance,0,seq.get());
    d->connect_port(instance,1,left.data()); d->connect_port(instance,2,right.data());
    d->activate(instance);
    double outputEnergy = 0;
    const auto block = [&](std::uint32_t count,std::initializer_list<Message> messages) {
        seq.reset(); for (const auto& m : messages) seq.append(m);
        inRun = true; d->run(instance,count); inRun = false;
        for (std::uint32_t frame = 0; frame < count; ++frame) {
            for (const auto& m : messages) if (m.frame == frame) apply(reference,m);
            float l,r; reference.sample(l,r);
            require(std::isfinite(left[frame]) && std::isfinite(right[frame]),"non-finite LV2 audio");
            require(left[frame] == l && right[frame] == r,"LV2 output differs from sample-timed engine");
            outputEnergy += l*l+r*r;
        }
        require(runAllocations == 0,"C++ allocation in LV2 run()");
    };
    block(512,{{127,{0x90,60,100}}});
    for (int i = 0; i < 127; ++i) require(left[i] == 0 && right[i] == 0,"note started early");
    block(257,{{3,{0xb0,64,127}},{67,{0x80,60,0}}});
    for (int i = 0; i < 48; ++i) block(512,{});
    require(reference.activeVoices() == 1 && outputEnergy > 1e-5,"sustain/keyboard produced no sound");
    block(64,{{31,{0xb0,64,0}}});
    for (int i = 0; i < 96; ++i) block(512,{});
    require(reference.activeVoices() == 0,"pedal release did not retire note");
    block(512,{{0,{0x90,60,90}},{64,{0x90,64,80}},{128,{0x90,67,100}}});
    block(257,{{0,{0x90,60,0}},{10,{0xb0,123,0}}});
    block(512,{{71,{0xb0,120,0}}});
    for (int i = 71; i < 512; ++i) require(left[i] == 0 && right[i] == 0,"panic left sound");
    // Non-MIDI, undersized and out-of-block events are ignored without early notes.
    block(512,{{0,{0x90,60,100},2},{10,{0x90,60,100},3,4},
               {600,{0x90,60,100}},{-1,{0x90,60,100}}});
    seq.reset(); seq.append({0,{0x90,60,100}});
    seq.get()->atom.size -= 6; // Truncated event body/padding.
    d->run(instance,512);
    for (float x : left) require(x == 0,"truncated MIDI was applied");
    seq.reset(); seq.append({0,{0x90,60,100}}); seq.get()->body.unit = 4;
    d->run(instance,512);
    for (float x : left) require(x == 0,"unsupported time unit was treated as frames");
    seq.reset(); seq.append({0,{0x90,60,100}}); seq.get()->body.unit = 3;
    d->run(instance,512);
    require(std::abs(left[511])+std::abs(right[511]) > 0,"explicit frame-time MIDI was ignored");
    d->deactivate(instance); d->activate(instance); reference.reset();
    block(512,{});
    left.fill(0.123f); seq.reset(); d->run(instance,0);
    require(left.front() == 0.123f,"zero-size block wrote audio");
    d->deactivate(instance); d->cleanup(instance);
    std::cout << fs << " Hz: PASS bit-exact audio, offsets, velocity, sustain, panic, reset, malformed events; C++ run allocations="
              << runAllocations << '\n';
}
int main(int argc,char** argv) {
    pme::simd::ScopedFlushDenormals denormals;
    try {
        require(argc == 2,"LV2 binary path required");
        void* library = dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);
        require(library,"dlopen failed");
        auto entry = reinterpret_cast<const LV2_Descriptor*(*)(std::uint32_t)>(dlsym(library,"lv2_descriptor"));
        require(entry && entry(0) && !entry(1),"invalid LV2 descriptor export");
        for (double fs : {44100.,48000.,96000.}) exercise(entry(0),fs);
        dlclose(library);
    } catch (const std::exception& e) { inRun = false; std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}

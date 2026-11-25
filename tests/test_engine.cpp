#include "../src/core/wreckage_engine.h"
#include <cassert>
#include <cmath>
#include <iostream>

int main() {
    using namespace NoiseMu6ix;

    WreckageEngine eng;
    assert(!eng.slot(0).hasAnySample());
    assert(eng.slot(0).midiNote == 48);
    assert(eng.slot(35).midiNote == 83);
    assert(eng.params().fx.satMix == 0.0f);
    assert(eng.params().fx.compMakeupDb == 0.0f);

    auto buf = std::make_shared<AudioBuffer>();
    buf->sampleRate = 44100;
    buf->left.resize(4410, 0.5f);
    buf->right.resize(4410, 0.5f);
    for (size_t i = 0; i < 4410; ++i) {
        float s = 0.65f * std::sin(2.0f * kPi * 80.0f * (static_cast<float>(i) / 44100.0f));
        buf->left[i] = s; buf->right[i] = s;
    }
    buf->rebuildPeaks();
    assert(buf->maxPeak > 0.6f);

    eng.slot(0).name = "KICK 01";
    eng.slot(0).items.resize(1);
    eng.slot(0).items[0].mics[0] = buf;
    eng.prepare(44100.0);
    eng.triggerNoteOn(48, 0.9f);

    std::vector<float> mainL(512, 0.0f), mainR(512, 0.0f);
    ProcessBuffers pb;
    pb.mainL = mainL.data(); pb.mainR = mainR.data();
    pb.numSamples = 512;
    pb.offlineRender = true;
    eng.process(pb);

    float pk = 0.0f;
    for (int i = 0; i < 512; ++i) {
        assert(std::isfinite(mainL[i]) && std::isfinite(mainR[i]));
        pk = std::max(pk, std::fabs(mainL[i]));
    }
    assert(pk > 0.1f);
    std::cout << "ALL v1.0.4 TESTS PASSED! peak=" << pk << std::endl;
    return 0;
}

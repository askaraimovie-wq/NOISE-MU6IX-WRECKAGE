#include "../src/core/wreckage_engine.h"
#include <cassert>
#include <cmath>
#include <iostream>

int main() {
    using namespace NoiseMu6ix;

    WreckageEngine eng;
    assert(!eng.slot(0).hasAnySample());
    assert(eng.slot(0).fakeRR && eng.slot(0).fakeVel);
    assert(eng.params().globalFakeMic);

    auto buf = std::make_shared<AudioBuffer>();
    buf->sampleRate = 44100;
    buf->left.resize(4410, 0.5f);
    buf->right.resize(4410, 0.5f);
    for (size_t i = 0; i < 4410; ++i) {
        float s = 0.65f * std::sin(2.0f * kPi * 80.0f * (static_cast<float>(i) / 44100.0f));
        buf->left[i] = s; buf->right[i] = s;
    }
    buf->rebuildPeaks();

    eng.slot(0).name = "KICK 01";
    eng.slot(0).items.resize(2);
    eng.slot(0).items[0].mics[0] = buf;
    eng.slot(0).items[1].mics[0] = buf;
    eng.deleteWaveItem(0, 1);
    assert(eng.slot(0).itemCount() == 1);

    eng.slot(0).attackMs = 20.0f;
    eng.slot(0).releaseMs = 150.0f;
    eng.prepare(44100.0);
    eng.triggerNoteOn(48, 0.9f);

    std::vector<float> mainL(512, 0.0f), mainR(512, 0.0f);
    ProcessBuffers pb;
    pb.mainL = mainL.data(); pb.mainR = mainR.data();
    pb.numSamples = 512;
    pb.offlineRender = true;
    eng.process(pb);

    float ph = eng.getSlotPlayheadNorm(0);
    assert(ph > 0.0f && ph < 1.0f);

    float pk = 0.0f;
    for (int i = 0; i < 512; ++i) {
        assert(std::isfinite(mainL[i]) && std::isfinite(mainR[i]));
        pk = std::max(pk, std::fabs(mainL[i]));
    }
    assert(pk > 0.05f);
    std::cout << "ALL v1.0.5 TESTS PASSED! peak=" << pk << " playhead=" << ph << std::endl;
    return 0;
}

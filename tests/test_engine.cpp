#include "../src/core/wreckage_engine.h"
#include <cassert>
#include <cmath>
#include <iostream>

int main() {
    using namespace NoiseMu6ix;

    // Verify dB mapping: 0.75 norm -> 0.0 dB -> linear 1.0f
    float db0 = gainNormToDb(0.75f);
    assert(std::fabs(db0) < 0.01f);
    float lin0 = gainDbToLinear(db0);
    assert(std::fabs(lin0 - 1.0f) < 0.01f);
    assert(gainNormToDb(0.0f) <= -59.5f);
    assert(std::fabs(gainNormToDb(1.0f) - 6.0f) < 0.01f);

    WreckageEngine eng;
    assert(std::fabs(eng.slot(0).gain - 1.0f) < 0.01f);

    auto buf = std::make_shared<AudioBuffer>();
    buf->sampleRate = 44100;
    buf->left.resize(4410, 0.0f);
    buf->right.resize(4410, 0.0f);
    for (size_t i = 0; i < 4410; ++i) {
        float s = 0.65f * std::sin(2.0f * kPi * 80.0f * (static_cast<float>(i) / 44100.0f));
        buf->left[i] = s; buf->right[i] = s;
    }
    buf->rebuildPeaks();

    // Test max 4 velocity layers & max 9 RRs per layer
    eng.slot(0).name = "SNARE";
    eng.slot(0).fakeVel = false;
    eng.slot(0).velLayers[0].rrs.resize(1);
    eng.slot(0).velLayers[0].rrs[0].mics[0] = buf;
    eng.slot(0).velLayers[3].rrs.resize(2);
    eng.slot(0).velLayers[3].rrs[0].mics[0] = buf;
    eng.slot(0).velLayers[3].rrs[1].mics[0] = buf;

    eng.prepare(44100.0);
    eng.triggerNoteOn(48, 0.95f); // Selects velocity layer 3

    std::vector<float> mainL(512, 0.0f), mainR(512, 0.0f);
    ProcessBuffers pb;
    pb.mainL = mainL.data(); pb.mainR = mainR.data();
    pb.numSamples = 512;
    pb.offlineRender = true;
    eng.process(pb);

    float ph = -1.0f;
    int actVl = -1, actRr = -1;
    eng.getSlotLivePlayback(0, ph, actVl, actRr);
    assert(actVl == 3);
    assert(actRr == 0 || actRr == 1);
    assert(ph > 0.0f && ph < 1.0f);

    float pk = 0.0f;
    for (int i = 0; i < 512; ++i) {
        assert(std::isfinite(mainL[i]) && std::isfinite(mainR[i]));
        pk = std::max(pk, std::fabs(mainL[i]));
    }
    assert(pk > 0.1f);
    std::cout << "ALL v1.0.6 TESTS PASSED! peak=" << pk << " vl=" << actVl << " rr=" << actRr << std::endl;
    return 0;
}

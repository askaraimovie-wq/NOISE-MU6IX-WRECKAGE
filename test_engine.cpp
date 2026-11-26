#include "../src/core/wreckage_engine.h"
#include <cassert>
#include <cmath>
#include <iostream>

int main() {
    using namespace NoiseMu6ix;

    // 1. Verify dB mapping (0.75 -> 0.0 dB -> linear 1.0)
    float db0 = gainNormToDb(0.75f);
    assert(std::fabs(db0) < 0.01f);
    assert(std::fabs(gainDbToLinear(db0) - 1.0f) < 0.01f);
    assert(gainNormToDb(0.0f) <= -59.5f);
    assert(std::fabs(gainNormToDb(1.0f) - 6.0f) < 0.01f);

    // 2. Verify Logarithmic Release mapping roundtrip
    assert(std::fabs(releaseNormToMs(0.0f) - 10.0f) < 0.5f);
    assert(std::fabs(releaseNormToMs(1.0f) - 4000.0f) < 25.0f);
    float rNorm = releaseMsToNorm(200.0f);
    assert(std::fabs(releaseNormToMs(rNorm) - 200.0f) < 5.0f);

    // 3. Verify defaults (Close ON; Room/Hall/Sub OFF; Fake RR/Vel/Mic OFF; Punish 0.0)
    WreckageEngine eng;
    assert(!eng.params().busBypass[0]);
    assert(eng.params().busBypass[1] && eng.params().busBypass[2] && eng.params().busBypass[3]);
    assert(!eng.params().globalFakeMic);
    assert(!eng.slot(0).fakeRR && !eng.slot(0).fakeVel);
    assert(std::fabs(eng.params().fx.punishAmount) < 0.001f);

    auto buf = std::make_shared<AudioBuffer>();
    buf->sampleRate = 44100;
    buf->left.resize(8820, 0.0f);
    buf->right.resize(8820, 0.0f);
    for (size_t i = 0; i < 8820; ++i) {
        float s = 0.65f * std::sin(2.0f * kPi * 100.0f * (static_cast<float>(i) / 44100.0f));
        buf->left[i] = s; buf->right[i] = s;
    }
    buf->rebuildPeaks();

    eng.slot(0).name = "SNARE";
    eng.slot(0).pitchSemitones = 12.0f; // +1 octave pitch
    eng.slot(0).releaseMs = 150.0f;
    eng.slot(0).velLayers[0].rrs.resize(1);
    eng.slot(0).velLayers[0].rrs[0].mics[0] = buf;
    eng.slot(0).velLayers[3].rrs.resize(2);
    eng.slot(0).velLayers[3].rrs[0].mics[0] = buf;
    eng.slot(0).velLayers[3].rrs[1].mics[0] = buf;

    // Enable cached Comp & Reverb to verify DSP hash-caching
    eng.params().fx.compOn = true;
    eng.params().fx.revOn = true;
    eng.prepare(44100.0);
    eng.triggerNoteOn(48, 0.95f);

    std::vector<float> mainL(512, 0.0f), mainR(512, 0.0f);
    ProcessBuffers pb;
    pb.mainL = mainL.data(); pb.mainR = mainR.data();
    pb.numSamples = 512;
    pb.offlineRender = true;
    eng.process(pb);

    int lvS = -1, lvV = -1, lvR = -1; uint32_t lvSeq = 0;
    eng.getLastTriggeredVoice(lvS, lvV, lvR, lvSeq);
    assert(lvS == 0 && lvV == 3 && (lvR == 0 || lvR == 1) && lvSeq == 1);

    float pk = 0.0f;
    for (int i = 0; i < 512; ++i) {
        assert(std::isfinite(mainL[i]) && std::isfinite(mainR[i]));
        pk = std::max(pk, std::fabs(mainL[i]));
    }
    assert(pk > 0.1f);
    std::cout << "ALL v1.0.8 TESTS PASSED! peak=" << pk << " lvS=" << lvS << " lvV=" << lvV << " lvR=" << lvR << std::endl;
    return 0;
}

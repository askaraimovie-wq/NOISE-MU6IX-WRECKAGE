#include "../src/core/wreckage_engine.h"
#include <cassert>
#include <cmath>
#include <filesystem>
#include <iostream>

int main() {
    using namespace NoiseMu6ix;

    // 1. Exact Release bounds (10 ms, 200 ms, 4000 ms)
    assert(std::fabs(releaseNormToMs(0.0f) - 10.0f) < 0.01f);
    assert(std::fabs(releaseNormToMs(0.5f) - 200.0f) < 0.5f);
    assert(std::fabs(releaseNormToMs(1.0f) - 4000.0f) < 0.01f);

    // 2. Defaults & True Zero-CPU Idle Sleep
    WreckageEngine eng;
    eng.prepare(44100.0);
    assert(!eng.params().fx.punishOn);
    assert(std::fabs(eng.params().fx.punishAmount) < 0.001f);
    assert(eng.isIdleSleeping());

    std::vector<float> mainL(512, 1.0f), mainR(512, 1.0f);
    ProcessBuffers pb;
    pb.mainL = mainL.data(); pb.mainR = mainR.data();
    pb.numSamples = 512; pb.offlineRender = true;
    eng.process(pb);
    assert(mainL[0] == 0.0f && mainR[511] == 0.0f);
    assert(eng.isIdleSleeping());

    // 3. Create synthetic drum hit
    auto buf = std::make_shared<AudioBuffer>();
    buf->sampleRate = 44100;
    buf->left.resize(8820, 0.0f);
    buf->right.resize(8820, 0.0f);
    for (size_t i = 0; i < 8820; ++i) {
        float env = std::exp(-static_cast<float>(i) / 2200.0f);
        float s = 0.7f * env * std::sin(2.0f * kPi * 120.0f * (static_cast<float>(i) / 44100.0f));
        buf->left[i] = s; buf->right[i] = s;
    }
    buf->rebuildPeaks();

    // 4. Test Reverse trim & playhead alignment
    eng.slot(0).name = "SNARE\"TEST";
    eng.slot(0).reverse = true;
    eng.slot(0).gain = 0.85f;
    eng.slot(0).pitchSemitones = -3.0f;
    eng.slot(0).attackMs = 5.0f;
    eng.slot(0).releaseMs = 250.0f;
    eng.slot(0).oneShot = false;
    eng.slot(0).chokeGroup = 2;
    eng.slot(0).velLayers[0].rrs.resize(1);
    eng.slot(0).velLayers[0].rrs[0].mics[0] = buf;
    eng.slot(0).velLayers[0].rrs[0].startNorm = 0.2f;
    eng.slot(0).velLayers[0].rrs[0].endNorm = 0.8f;

    // Test all 4 Saturator modes, 3 Delay modes, and 8-line Diffused Reverb
    eng.params().fx.satOn = true;
    eng.params().fx.satMix = 0.5f;
    eng.params().fx.delayOn = true;
    eng.params().fx.delayMix = 0.35f;
    eng.params().fx.revOn = true;
    eng.params().fx.revMix = 0.35f;

    for (int sm = 0; sm < 4; ++sm) {
        eng.params().fx.satMode = sm;
        eng.params().fx.delayMode = sm % 3;
        eng.params().fx.revMode = sm % 3;
        eng.triggerNoteOn(48, 0.9f);
        assert(!eng.isIdleSleeping());
        eng.process(pb);
        float ph = -1.0f; int vl = -1, rr = -1;
        eng.getSlotLivePlayback(0, ph, vl, rr);
        assert(ph >= 0.2f && ph <= 0.8f);
        for (int i = 0; i < 512; ++i) {
            assert(std::isfinite(mainL[i]) && std::isfinite(mainR[i]));
        }
    }

    // 5. Panic clears tails & sleeps
    eng.panicAllNotesOff();
    assert(eng.isIdleSleeping());
    assert(eng.activeVoiceCount() == 0);

    // 6. Test .nm6 format 4 full per-key persistence & quote sanitization
    auto tmpNm6 = std::filesystem::path("test_v109_kit.nm6");
    assert(eng.exportKitToNm6(tmpNm6, "TestKit"));

    WreckageEngine eng2;
    assert(eng2.loadKitFromNm6(tmpNm6));
    assert(eng2.slot(0).name == "SNARETEST");
    assert(std::fabs(eng2.slot(0).gain - 0.85f) < 0.01f);
    assert(std::fabs(eng2.slot(0).pitchSemitones - (-3.0f)) < 0.01f);
    assert(std::fabs(eng2.slot(0).attackMs - 5.0f) < 0.01f);
    assert(std::fabs(eng2.slot(0).releaseMs - 250.0f) < 0.01f);
    assert(!eng2.slot(0).oneShot && eng2.slot(0).chokeGroup == 2 && eng2.slot(0).reverse);
    assert(eng2.totalSampleRamBytes() > 0);
    std::filesystem::remove(tmpNm6);

    std::cout << "ALL v1.0.9 TESTS PASSED! ramBytes=" << eng2.totalSampleRamBytes() << std::endl;
    return 0;
}

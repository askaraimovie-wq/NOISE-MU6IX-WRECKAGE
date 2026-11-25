#include "../src/core/wreckage_engine.h"
#include <cassert>
#include <cmath>
#include <iostream>

int main() {
    using namespace NoiseMu6ix;

    WreckageEngine eng;
    assert(!eng.slot(0).hasAnySample());
    assert(eng.slot(0).midiNote == 48); // C3
    assert(eng.slot(35).midiNote == 83); // B5
    assert(eng.slot(0).stageX == 0.0f && eng.slot(0).stageY == 0.0f);
    assert(eng.params().themeIndex == 0); // Paper Sketch
    assert(eng.params().zoomIndex == 2);  // 100%

    // Test RR-primary append + Wave Start/End/Fade + FabFilter/Valhalla Master FX + .nm6 roundtrip
    auto buf = std::make_shared<AudioBuffer>();
    buf->sampleRate = 48000;
    buf->left.resize(4800, 0.5f);
    buf->right.resize(4800, 0.5f);
    for (size_t i = 0; i < 4800; ++i) {
        float s = 0.7f * std::sin(2.0f * kPi * 70.0f * (static_cast<float>(i) / 48000.0f));
        buf->left[i] = s; buf->right[i] = s;
    }
    buf->rebuildPeaks();

    eng.slot(0).name = "THAVIL HIT";
    eng.slot(0).items.resize(1);
    eng.slot(0).items[0].mics[0] = buf;
    eng.slot(0).items[0].startNorm = 0.05f;
    eng.slot(0).items[0].endNorm = 0.90f;
    eng.slot(0).items[0].fadeInNorm = 0.10f;
    eng.slot(0).items[0].fadeOutNorm = 0.15f;

    eng.params().fx.filterOn = true;
    eng.params().fx.eqOn = true;
    eng.params().fx.compOn = true;
    eng.params().fx.satOn = true;
    eng.params().fx.delayOn = true;
    eng.params().fx.revOn = true;

    eng.prepare(48000.0);
    eng.triggerNoteOn(48, 0.95f);

    std::vector<float> mainL(1024, 0.0f), mainR(1024, 0.0f);
    ProcessBuffers pb;
    pb.mainL = mainL.data(); pb.mainR = mainR.data();
    pb.numSamples = 1024;
    pb.offlineRender = true;
    eng.process(pb);

    float pk = 0.0f;
    for (int i = 0; i < 1024; ++i) {
        assert(std::isfinite(mainL[i]) && std::isfinite(mainR[i]));
        assert(std::fabs(mainL[i]) <= 0.97f);
        pk = std::max(pk, std::fabs(mainL[i]));
    }
    assert(pk > 0.05f);

    std::filesystem::path packPath = "test_v103.nm6";
    assert(eng.exportKitToNm6(packPath, "Test v103"));
    WreckageEngine eng2;
    assert(eng2.loadKitFromNm6(packPath));
    assert(eng2.slot(0).name == "THAVIL HIT");
    assert(std::fabs(eng2.slot(0).items[0].startNorm - 0.05f) < 1e-4f);
    std::filesystem::remove(packPath);

    std::cout << "ALL v1.0.3 TESTS PASSED! peak=" << pk << std::endl;
    return 0;
}

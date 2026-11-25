#include "../src/core/wreckage_engine.h"
#include <cassert>
#include <cmath>
#include <iostream>

int main() {
    using namespace NoiseMu6ix;

    auto m1 = SampleAutoParser::parseFilename("Dhol_Close_V6_RR8.wav");
    assert(m1.velocityIndex == 5 && m1.rrIndex == 7 && m1.mic == MicBus::Close);
    auto m2 = SampleAutoParser::parseFilename("Thavil-room-vel12-r3.wav");
    assert(m2.velocityIndex == 11 && m2.rrIndex == 2 && m2.mic == MicBus::Room);
    auto m3 = SampleAutoParser::parseFilename("Snare_LFE_V2_RR4.wav");
    assert(m3.velocityIndex == 1 && m3.rrIndex == 3 && m3.mic == MicBus::LFE);

    WreckageEngine eng;
    eng.prepare(48000.0);
    eng.triggerNoteOn(36, 0.95f);
    eng.triggerNoteOn(40, 0.80f);

    std::vector<float> mainL(1024, 0.0f), mainR(1024, 0.0f);
    std::vector<float> lfeL(1024, 0.0f), lfeR(1024, 0.0f);
    ProcessBuffers pb;
    pb.mainL = mainL.data(); pb.mainR = mainR.data();
    pb.auxL[3] = lfeL.data(); pb.auxR[3] = lfeR.data();
    pb.auxActive[3] = true;
    pb.numSamples = 1024;
    pb.offlineRender = true;
    eng.process(pb);

    float peakMain = 0.0f, peakLfe = 0.0f;
    for (int i = 0; i < 1024; ++i) {
        assert(std::isfinite(mainL[i]) && std::isfinite(mainR[i]));
        assert(std::fabs(mainL[i]) <= 0.97f);
        assert(std::fabs(lfeL[i] - lfeR[i]) < 1e-6f);
        peakMain = std::max(peakMain, std::fabs(mainL[i]));
        peakLfe  = std::max(peakLfe,  std::fabs(lfeL[i]));
    }
    assert(peakMain > 0.1f && peakLfe > 0.01f);

    std::filesystem::path packPath = "test_kit.nm6";
    assert(eng.exportKitToNm6(packPath, "Test Library"));
    Nm6Pack loaded;
    assert(loaded.loadFromFile(packPath));
    assert(loaded.samples.size() == 8);
    AudioBuffer decoded;
    assert(loaded.decodeSample(0, decoded) && !decoded.empty());
    std::filesystem::remove(packPath);

    std::cout << "ALL NOISE MU6IX WRECKAGE CORE TESTS PASSED! peakMain="
              << peakMain << " peakLfe=" << peakLfe << std::endl;
    return 0;
}

#include "../src/core/wreckage_engine.h"
#include <cassert>
#include <cmath>
#include <iostream>

int main() {
    using namespace NoiseMu6ix;

    // 1. Default kit must be empty (0 samples) and octave base = C3 (MIDI 48)
    WreckageEngine eng;
    assert(!eng.slot(0).hasAnySample());
    assert(eng.slot(0).midiNote == 48);
    assert(eng.slot(35).midiNote == 83);

    // 2. Test Auto-Parser with 4 mics (Close, Room, Hall, Sub)
    auto m1 = SampleAutoParser::parseFilename("Dhol_Close_V6_RR8.wav");
    assert(m1.velocityIndex == 5 && m1.rrIndex == 7 && m1.mic == MicBus::Close);
    auto m2 = SampleAutoParser::parseFilename("Thavil-room-vel12-r3.wav");
    assert(m2.velocityIndex == 11 && m2.rrIndex == 2 && m2.mic == MicBus::Room);
    auto m3 = SampleAutoParser::parseFilename("Snare_Sub_V2_RR4.wav");
    assert(m3.velocityIndex == 1 && m3.rrIndex == 3 && m3.mic == MicBus::Sub);

    // 3. Load synthetic test buffer into slot 0 and verify 4-Bus + Master FX + .nm6 Save/Load
    auto buf = std::make_shared<AudioBuffer>();
    buf->sampleRate = 48000;
    buf->left.resize(4800, 0.5f);
    buf->right.resize(4800, 0.5f);
    for (size_t i = 0; i < 4800; ++i) {
        float s = 0.7f * std::sin(2.0f * kPi * 60.0f * (static_cast<float>(i) / 48000.0f));
        buf->left[i] = s; buf->right[i] = s;
    }
    eng.slot(0).name = "BIG TAIKO";
    eng.slot(0).layers.resize(1);
    eng.slot(0).layers[0].roundRobins.resize(1);
    eng.slot(0).layers[0].roundRobins[0].mics[0] = buf;

    eng.prepare(48000.0);
    eng.triggerNoteOn(48, 0.95f);

    std::vector<float> mainL(1024, 0.0f), mainR(1024, 0.0f);
    std::vector<float> subL(1024, 0.0f), subR(1024, 0.0f);
    ProcessBuffers pb;
    pb.mainL = mainL.data(); pb.mainR = mainR.data();
    pb.auxL[3] = subL.data(); pb.auxR[3] = subR.data();
    pb.auxActive[3] = true;
    pb.numSamples = 1024;
    pb.offlineRender = true;
    eng.process(pb);

    float peakMain = 0.0f, peakSub = 0.0f;
    for (int i = 0; i < 1024; ++i) {
        assert(std::isfinite(mainL[i]) && std::isfinite(mainR[i]));
        assert(std::fabs(mainL[i]) <= 0.97f);
        assert(std::fabs(subL[i] - subR[i]) < 1e-6f);
        peakMain = std::max(peakMain, std::fabs(mainL[i]));
        peakSub  = std::max(peakSub,  std::fabs(subL[i]));
    }
    assert(peakMain > 0.1f && peakSub > 0.01f);

    std::filesystem::path packPath = "test_v102.nm6";
    assert(eng.exportKitToNm6(packPath, "Test v102"));
    WreckageEngine eng2;
    assert(eng2.loadKitFromNm6(packPath));
    assert(eng2.slot(0).name == "BIG TAIKO");
    assert(eng2.slot(0).hasAnySample());
    std::filesystem::remove(packPath);

    std::cout << "ALL v1.0.2 TESTS PASSED! peakMain=" << peakMain << " peakSub=" << peakSub << std::endl;
    return 0;
}

#include "../src/core/wreckage_engine.h"
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <iostream>

static std::vector<uint8_t> makeFloat64Wav(uint32_t sr, const std::vector<double>& mono) {
    uint32_t dataSize = static_cast<uint32_t>(mono.size() * 8);
    uint32_t riffSize = 36 + dataSize;
    std::vector<uint8_t> out;
    auto pushU16 = [&](uint16_t v) { out.push_back(v & 0xFF); out.push_back((v >> 8) & 0xFF); };
    auto pushU32 = [&](uint32_t v) {
        out.push_back(v & 0xFF); out.push_back((v >> 8) & 0xFF);
        out.push_back((v >> 16) & 0xFF); out.push_back((v >> 24) & 0xFF);
    };
    auto pushStr = [&](const char* s) { for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>(s[i])); };
    pushStr("RIFF"); pushU32(riffSize); pushStr("WAVE");
    pushStr("fmt "); pushU32(16); pushU16(3); pushU16(1); pushU32(sr);
    pushU32(sr * 8); pushU16(8); pushU16(64);
    pushStr("data"); pushU32(dataSize);
    for (double d : mono) {
        uint8_t b[8];
        std::memcpy(b, &d, 8);
        out.insert(out.end(), b, b + 8);
    }
    return out;
}

int main() {
    using namespace NoiseMu6ix;

    // 1. Test 64-bit Float WAV decoding
    std::vector<double> f64Samples(64, 0.5);
    auto wav64 = makeFloat64Wav(44100, f64Samples);
    AudioBuffer decoded64;
    assert(WavLoader::loadFromMemory(wav64.data(), wav64.size(), decoded64));
    assert(decoded64.numFrames() == 64);
    assert(std::fabs(decoded64.left[0] - 0.5f) < 1e-4f);

    // 2. Test REVERSE: ON from Sample 0 (startNorm=0.0, endNorm=1.0) - both unit pitch & pitched!
    WreckageEngine eng;
    eng.prepare(44100.0);
    auto ramp = std::make_shared<AudioBuffer>();
    ramp->sampleRate = 44100;
    ramp->left.resize(1024);
    ramp->right.resize(1024);
    for (size_t i = 0; i < 1024; ++i) {
        float v = static_cast<float>(i + 1) / 1024.0f; // ramps 0.001 -> 1.0
        ramp->left[i] = v; ramp->right[i] = v;
    }
    ramp->rebuildPeaks();

    eng.slot(0).name = "REV_TEST";
    eng.slot(0).reverse = true;
    eng.slot(0).velLayers[0].rrs.resize(1);
    eng.slot(0).velLayers[0].rrs[0].mics[0] = ramp;
    eng.slot(0).velLayers[0].rrs[0].startNorm = 0.0f;
    eng.slot(0).velLayers[0].rrs[0].endNorm = 1.0f;

    eng.params().fx.limiterOn = false;
    eng.params().fx.monoBass = false;

    std::vector<float> mainL(256, 0.0f), mainR(256, 0.0f);
    ProcessBuffers pb;
    pb.mainL = mainL.data(); pb.mainR = mainR.data();
    pb.numSamples = 256; pb.offlineRender = true;

    // Unit pitch reverse: first sample should be ~1.0 (end of ramp) and descending!
    eng.triggerNoteOn(48, 1.0f);
    eng.process(pb);
    assert(mainL[0] > 0.05f);
    assert(mainL[12] > 0.5f);
    assert(mainL[12] > mainL[200]);

    // Non-unit pitch reverse (+5 semitones): must also NOT die at sample 0!
    eng.panicAllNotesOff();
    eng.slot(0).pitchSemitones = 5.0f;
    eng.triggerNoteOn(48, 1.0f);
    eng.process(pb);
    assert(mainL[0] > 0.05f);
    assert(mainL[12] > 0.5f);
    assert(mainL[12] > mainL[120]);

    // 3. Test Reverb LFO modulation (2 seconds continuous -> finite, no spikes/clicks)
    eng.panicAllNotesOff();
    eng.slot(0).reverse = false;
    eng.slot(0).pitchSemitones = 0.0f;
    eng.params().fx.revOn = true;
    eng.params().fx.revMode = 2; // HALL
    eng.params().fx.revDecaySec = 3.0f;
    eng.params().fx.revMix = 0.5f;
    eng.params().fx.delayOn = true;
    eng.params().fx.delayMode = 1; // PING-PONG
    eng.params().fx.delayMix = 0.3f;
    eng.params().fx.satOn = true;
    eng.params().fx.satMode = 1; // TUBE
    eng.params().fx.satMix = 0.4f;
    eng.triggerNoteOn(48, 0.9f);
    for (int blk = 0; blk < 350; ++blk) {
        eng.process(pb);
        for (int i = 0; i < 256; ++i) {
            assert(std::isfinite(mainL[i]) && std::isfinite(mainR[i]));
        }
    }

    // 4. Test Multi-Out Aux Tail Idle Sleep safety
    eng.panicAllNotesOff();
    eng.params().fx.revOn = false;
    eng.params().fx.delayOn = false;
    eng.params().globalFakeMic = true;
    eng.params().busBypass[2] = false; // Hall ON
    std::vector<float> auxL(256, 0.0f), auxR(256, 0.0f);
    std::vector<float> auxCloseL(256, 0.0f), auxCloseR(256, 0.0f);
    ProcessBuffers pbAux;
    pbAux.mainL = mainL.data(); pbAux.mainR = mainR.data();
    pbAux.auxActive[0] = true; pbAux.auxL[0] = auxCloseL.data(); pbAux.auxR[0] = auxCloseR.data();
    pbAux.auxActive[2] = true; pbAux.auxL[2] = auxL.data(); pbAux.auxR[2] = auxR.data();
    pbAux.numSamples = 256; pbAux.offlineRender = true;
    eng.triggerNoteOn(48, 0.9f);
    for (int blk = 0; blk < 10; ++blk) eng.process(pbAux);
    // Voice finished (1024 samples < 2560), Main Out is silent, but Hall Aux tail is still alive!
    assert(!eng.isIdleSleeping());

    // 5. Test Choke Group same-note batch safety
    eng.panicAllNotesOff();
    eng.slot(1).midiNote = 48;
    eng.slot(0).chokeGroup = 1;
    eng.slot(1).chokeGroup = 1;
    eng.slot(1).velLayers[0].rrs.resize(1);
    eng.slot(1).velLayers[0].rrs[0].mics[0] = ramp;
    eng.triggerNoteOn(48, 0.9f);
    eng.process(pb);
    assert(eng.activeVoiceCount() == 2); // Both slots on note 48 play together without choking each other!

    // 6. Test clearSlot() full parameter reset
    eng.slot(1).gain = 0.25f;
    eng.slot(1).pitchSemitones = -7.0f;
    eng.slot(1).reverse = true;
    eng.slot(1).fakeRR = true;
    eng.clearSlot(1);
    assert(std::fabs(eng.slot(1).gain - 1.0f) < 1e-4f);
    assert(std::fabs(eng.slot(1).pitchSemitones) < 1e-4f);
    assert(!eng.slot(1).reverse && !eng.slot(1).fakeRR && eng.slot(1).chokeGroup == 0);

    // 7. Test .nm6 Format 5 full Kit + Mixer + FX + PUNISH persistence
    eng.params().fx.filterOn = true;
    eng.params().fx.satMode = 2;
    eng.params().fx.delayMode = 1;
    eng.params().fx.punishOn = true;
    eng.params().fx.punishAmount = 0.42f;
    eng.params().busBypass[1] = false;
    eng.slot(0).name = "SNARE\"V110";
    auto tmpNm6 = std::filesystem::path("test_v110_kit.nm6");
    assert(eng.exportKitToNm6(tmpNm6, "V110Lib"));

    WreckageEngine eng2;
    bool hasMix = false;
    assert(eng2.loadKitFromNm6(tmpNm6, &hasMix));
    assert(hasMix);
    assert(eng2.slot(0).name == "SNAREV110");
    assert(eng2.params().fx.filterOn);
    assert(eng2.params().fx.satMode == 2);
    assert(eng2.params().fx.delayMode == 1);
    assert(eng2.params().fx.punishOn && std::fabs(eng2.params().fx.punishAmount - 0.42f) < 0.01f);
    assert(!eng2.params().busBypass[1]);
    std::filesystem::remove(tmpNm6);

    // 8. CPU Benchmark (1000 blocks of 256 samples = 5.8 sec audio with 4 active voices)
    eng2.prepare(44100.0);
    eng2.params().fx.filterOn = false;
    eng2.params().fx.satOn = false;
    eng2.params().fx.delayOn = false;
    eng2.params().fx.revOn = false;
    eng2.params().fx.punishOn = false;
    eng2.params().busBypass[1] = true;
    eng2.params().busBypass[2] = true;
    eng2.params().busBypass[3] = true;
    auto t0 = std::chrono::steady_clock::now();
    for (int blk = 0; blk < 1000; ++blk) {
        if (blk % 25 == 0) eng2.triggerNoteOn(48, 0.9f);
        eng2.process(pb);
    }
    auto t1 = std::chrono::steady_clock::now();
    double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    double realMs = (1000.0 * 256.0 / 44100.0) * 1000.0;
    double cpuPct = (ms / realMs) * 100.0;
    std::cout << "ALL v1.1.1 TESTS PASSED! 5.8s audio rendered in " << ms << " ms (~" << cpuPct << "% CPU)" << std::endl;
    return 0;
}

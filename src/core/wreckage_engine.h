#pragma once
#include "wreckage_params.h"
#include "wreckage_themes.h"
#include "wreckage_wav.h"
#include "wreckage_autoparser.h"
#include "wreckage_nm6_pack.h"
#include "wreckage_dsp.h"

#include <array>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <locale>
#include <sstream>
#include <string>
#include <vector>

namespace NoiseMu6ix {

struct MultiMicSample {
    std::array<std::shared_ptr<AudioBuffer>, 4> mics;
    std::array<std::string, 4> sourcePathsUtf8;
    float startNorm   = 0.0f;
    float endNorm     = 1.0f;
    float fadeInNorm  = 0.0f;
    float fadeOutNorm = 0.0f;
    float velMin      = 0.0f;
    float velMax      = 1.0f;

    bool hasMic(MicBus m) const {
        auto idx = static_cast<size_t>(m);
        return idx < 4 && mics[idx] && !mics[idx]->empty();
    }
    int realMicCount() const {
        int c = 0;
        for (int i = 0; i < 4; ++i) if (mics[i] && !mics[i]->empty()) ++c;
        return c;
    }
    std::shared_ptr<AudioBuffer> primarySource() const {
        if (hasMic(MicBus::Close)) return mics[0];
        for (int i = 1; i < 4; ++i) {
            if (mics[static_cast<size_t>(i)] && !mics[static_cast<size_t>(i)]->empty())
                return mics[static_cast<size_t>(i)];
        }
        return nullptr;
    }
};

struct VelocityLayer {
    std::vector<MultiMicSample> rrs; // Max kMaxRoundRobins (9) per velocity layer
    int lastRRIdx = -1;
};

struct SlotConfig {
    std::string name = "EMPTY";
    int midiNote = kBaseMidiNote;
    uint32_t keyColor = 0;
    bool hasCustomColor = false;

    float gain = 1.0f; // Linear 1.0f = 0.0 dB
    float pitchSemitones = 0.0f;
    float toneLowDb = 0.0f;
    float toneHighDb = 0.0f;
    float attackMs = 0.0f;
    float releaseMs = 4000.0f;

    // Per-Key Fake RR & Fake Velocity (Fake Mic stays Global)
    bool fakeRR  = false;
    bool fakeVel = false;

    bool oneShot = true;
    int chokeGroup = 0;
    bool reverse = false;
    bool purged = false;

    float stageX = 0.0f;
    float stageY = 0.0f;

    std::array<VelocityLayer, kMaxVelLayers> velLayers; // Max 4 velocity layers
    int fakeRRStep = 0;

    bool hasAnySample() const {
        for (int v = 0; v < kMaxVelLayers; ++v) {
            for (const auto& it : velLayers[v].rrs) {
                if (it.realMicCount() > 0) return true;
            }
        }
        return false;
    }

    int activeVelLayerCount() const {
        int lastActive = 0;
        for (int v = 0; v < kMaxVelLayers; ++v) {
            if (!velLayers[v].rrs.empty()) lastActive = v + 1;
        }
        return std::max(1, lastActive);
    }

    int rrCountInLayer(int velLayerIdx) const {
        int vl = std::clamp(velLayerIdx, 0, kMaxVelLayers - 1);
        return static_cast<int>(velLayers[vl].rrs.size());
    }
};

struct Voice {
    bool active = false;
    bool releasing = false;
    bool choking = false;
    int slotIndex = 0;
    int velLayerIndex = 0;
    int rrIndex = 0;
    int midiNote = 0;
    int chokeGroup = 0;
    bool reverse = false;
    bool oneShot = true;
    bool unitPitch = true;

    std::array<std::shared_ptr<AudioBuffer>, 4> micBufs;
    std::shared_ptr<AudioBuffer> primaryBuf;
    const float* primL = nullptr;
    const float* primR = nullptr;
    size_t primLen = 0;

    double playPos = 0.0;
    double startFrame = 0.0;
    double endFrame = 0.0;
    double fadeInFrames = 0.0;
    double fadeOutFrames = 0.0;
    double pitchRatio = 1.0;

    float velGain = 1.0f;
    float slotGain = 1.0f;
    float baseGain = 1.0f; // velGain * slotGain
    float panL = 0.707f, panR = 0.707f;
    float stageDepth = 0.0f;

    float envVal = 0.0f;
    float attackInc = 1.0f;
    float releaseMul = 0.999f; // Exponential percussion decay multiplier
    double releaseStartFrame = 0.0;
    double elapsedHostSamples = 0.0;
    float chokeFade = 1.0f;
    float chokeDec = 0.004f;

    Biquad formantL, formantR;
    Biquad velLpfL, velLpfR;
    Biquad stageLpfL, stageLpfR;
    Biquad toneLowL, toneLowR, toneHiL, toneHiR;
    bool useFormant = false;
    bool useVelLpf = false;
    bool useStageLpf = false;
    bool useToneEq = false;
    float velDrive = 0.0f;

    bool synthRoom = false;
    bool synthHall = false;
    bool synthSub  = false;
    bool hasRealRoom = false;
    bool hasRealHall = false;
    bool hasRealSub  = false;

    uint64_t age = 0;
};

struct EngineParams {
    float busGain[4]   = { 1.0f, 1.0f, 1.0f, 1.0f }; // 1.0f = 0.0 dB default
    bool  busBypass[4] = { false, true, true, true };

    bool  globalFakeMic = false;

    MasterFxParams fx;
    VelocityCurve velCurve = VelocityCurve::Linear;

    int themeIndex = 0;
    int zoomIndex  = 2;
};

struct ProcessBuffers {
    float* mainL = nullptr;
    float* mainR = nullptr;
    float* auxL[4] = { nullptr, nullptr, nullptr, nullptr };
    float* auxR[4] = { nullptr, nullptr, nullptr, nullptr };
    bool   auxActive[4] = { false, false, false, false };
    int    numSamples = 0;
    bool   offlineRender = false;
};

class WreckageEngine {
public:
    WreckageEngine() {
        for (int i = 0; i < kTotalSlots; ++i) {
            slots_[i].midiNote = kBaseMidiNote + i;
            slots_[i].name = "EMPTY";
            slots_[i].stageX = 0.0f;
            slots_[i].stageY = 0.0f;
        }
        ensureScratch(2048);
    }

    void resetSlotDefaultsLocked(SlotConfig& s) {
        for (int vl = 0; vl < kMaxVelLayers; ++vl) {
            s.velLayers[vl].rrs.clear();
            s.velLayers[vl].lastRRIdx = -1;
        }
        s.name = "EMPTY";
        s.hasCustomColor = false;
        s.keyColor = 0;
        s.stageX = 0.0f;
        s.stageY = 0.0f;
        s.gain = 1.0f;
        s.pitchSemitones = 0.0f;
        s.toneLowDb = 0.0f;
        s.toneHighDb = 0.0f;
        s.attackMs = 0.0f;
        s.releaseMs = 4000.0f;
        s.fakeRR = false;
        s.fakeVel = false;
        s.oneShot = true;
        s.chokeGroup = 0;
        s.reverse = false;
        s.purged = false;
        s.fakeRRStep = 0;
    }

    void resetKit() {
        std::lock_guard<std::mutex> lock(stateMutex_);
        for (auto& v : voices_) v.active = false;
        for (int i = 0; i < kTotalSlots; ++i) {
            resetSlotDefaultsLocked(slots_[i]);
        }
        activeVoices_.store(0, std::memory_order_relaxed);
        recalcRamBytesLocked();
    }

    void clearSlot(int idx) {
        std::lock_guard<std::mutex> lock(stateMutex_);
        for (auto& v : voices_) {
            if (v.active && v.slotIndex == idx) v.active = false;
        }
        resetSlotDefaultsLocked(slot(idx));
        recalcRamBytesLocked();
    }

    void deleteWaveItem(int slotIdx, int velLayerIdx, int rrIdx) {
        std::lock_guard<std::mutex> lock(stateMutex_);
        for (auto& v : voices_) {
            if (v.active && v.slotIndex == slotIdx) v.active = false;
        }
        auto& s = slot(slotIdx);
        int vl = std::clamp(velLayerIdx, 0, kMaxVelLayers - 1);
        auto& rrs = s.velLayers[vl].rrs;
        if (rrIdx >= 0 && rrIdx < static_cast<int>(rrs.size())) {
            rrs.erase(rrs.begin() + rrIdx);
            s.velLayers[vl].lastRRIdx = -1;
            if (!s.hasAnySample()) {
                s.name = "EMPTY";
                s.hasCustomColor = false;
                s.stageX = 0.0f;
                s.stageY = 0.0f;
            }
            recalcRamBytesLocked();
        }
    }

    void resetStagePositions() {
        std::lock_guard<std::mutex> lock(stateMutex_);
        for (int i = 0; i < kTotalSlots; ++i) {
            slots_[i].stageX = 0.0f;
            slots_[i].stageY = 0.0f;
        }
    }

    void prepare(double sampleRate) {
        sr_ = static_cast<float>(std::max(8000.0, sampleRate));
        roomBus_.prepare(sr_, 8.0f, 0.75f, 120.0f, 9000.0f);
        hallBus_.prepare(sr_, 24.0f, 3.20f, 120.0f, 8500.0f);
        subBus_.prepare(sr_);
        masterFx_.prepare(sr_);
        wasRoomRun_ = false;
        wasHallRun_ = false;
        for (auto& v : voices_) v.active = false;
        ensureScratch(4096);
    }

    EngineParams& params() { return params_; }
    const EngineParams& params() const { return params_; }

    SlotConfig& slot(int idx) { return slots_[std::clamp(idx, 0, kTotalSlots - 1)]; }
    const SlotConfig& slot(int idx) const { return slots_[std::clamp(idx, 0, kTotalSlots - 1)]; }

    int activeVoiceCount() const { return activeVoices_.load(std::memory_order_relaxed); }
    float currentGrDb() const { return masterFx_.currentGrDb(); }
    float currentOutLevel() const { return masterFx_.currentOutLevel(); }

    void getLastTriggeredVoice(int& outSlot, int& outVelLayer, int& outRR, uint32_t& outSeq) const {
        outSlot = lastVoiceSlot_.load(std::memory_order_relaxed);
        outVelLayer = lastVoiceVel_.load(std::memory_order_relaxed);
        outRR = lastVoiceRR_.load(std::memory_order_relaxed);
        outSeq = voiceSeq_.load(std::memory_order_relaxed);
    }

    void getSlotLivePlayback(int slotIdx, float& outPlayheadNorm, int& outActiveVelLayer, int& outActiveRR) {
        std::unique_lock<std::mutex> lock(stateMutex_, std::try_to_lock);
        outPlayheadNorm = -1.0f;
        outActiveVelLayer = -1;
        outActiveRR = -1;
        if (!lock.owns_lock()) return;
        uint64_t latestAge = 0;
        for (const auto& v : voices_) {
            if (v.active && v.slotIndex == slotIdx && v.primaryBuf && v.age >= latestAge) {
                latestAge = v.age;
                outActiveVelLayer = v.velLayerIndex;
                outActiveRR = v.rrIndex;
                double totalF = static_cast<double>(v.primLen);
                if (totalF > 1.0) {
                    outPlayheadNorm = std::clamp(static_cast<float>(v.playPos / totalF), 0.0f, 1.0f);
                }
            }
        }
    }

    uint64_t totalSampleRamBytes() const {
        return totalRamBytes_.load(std::memory_order_relaxed);
    }

    bool isIdleSleeping() const {
        return idleSleeping_.load(std::memory_order_relaxed);
    }

    bool isNotePlaying(int midiNote) const {
        if (midiNote < 0 || midiNote >= 128) return false;
        return notePlaying_[midiNote].load(std::memory_order_relaxed) > 0;
    }

    void panicAllNotesOff() {
        std::lock_guard<std::mutex> lock(stateMutex_);
        for (auto& v : voices_) v.active = false;
        for (int i = 0; i < 128; ++i) notePlaying_[i].store(0, std::memory_order_relaxed);
        activeVoices_.store(0, std::memory_order_relaxed);
        roomBus_.reset();
        hallBus_.reset();
        subBus_.reset();
        masterFx_.resetTails();
        idleSamples_ = 1000000;
        idleSleeping_.store(true, std::memory_order_relaxed);
    }

    // Reload a specific WAV into an exact (slot, velLayer, rr, mic) for FL Studio project recall
    bool reloadWavIntoExactLocation(int slotIdx, int vl, int rr, int mic,
                                    const std::filesystem::path& filePath, const std::string& pathUtf8,
                                    float startNorm, float endNorm, float fadeInNorm, float fadeOutNorm) {
        if (slotIdx < 0 || slotIdx >= kTotalSlots || vl < 0 || vl >= kMaxVelLayers ||
            rr < 0 || rr >= kMaxRoundRobins || mic < 0 || mic >= 4) return false;
        auto buf = std::make_shared<AudioBuffer>();
        if (!WavLoader::loadFromFile(filePath, *buf)) return false;

        std::lock_guard<std::mutex> lock(stateMutex_);
        auto& s = slots_[slotIdx];
        auto& rrs = s.velLayers[vl].rrs;
        if (static_cast<int>(rrs.size()) <= rr) rrs.resize(static_cast<size_t>(rr + 1));
        auto& it = rrs[static_cast<size_t>(rr)];
        it.mics[static_cast<size_t>(mic)] = buf;
        it.sourcePathsUtf8[static_cast<size_t>(mic)] = pathUtf8;
        it.startNorm   = std::clamp(startNorm, 0.0f, 0.98f);
        it.endNorm     = std::clamp(endNorm, it.startNorm + 0.02f, 1.0f);
        it.fadeInNorm  = std::clamp(fadeInNorm, 0.0f, 0.5f);
        it.fadeOutNorm = std::clamp(fadeOutNorm, 0.0f, 0.5f);
        recalcRamBytesLocked();
        return true;
    }

    // Loads sample respecting Max 4 Velocity Layers and Max 9 Round-Robins per layer
    bool loadSampleIntoSlot(int slotIdx, const std::filesystem::path& filePath, int preferredVelLayer = -1,
                            int* outVelLayer = nullptr, int* outRR = nullptr, const std::string& pathUtf8 = "") {
        auto buf = std::make_shared<AudioBuffer>();
        if (!WavLoader::loadFromFile(filePath, *buf)) return false;

        std::string fname = filePath.filename().u8string();
        std::string fullU8 = pathUtf8.empty() ? filePath.u8string() : pathUtf8;
        auto meta = SampleAutoParser::parseFilename(fname);

        std::lock_guard<std::mutex> lock(stateMutex_);
        auto& s = slot(slotIdx);
        bool firstLoad = !s.hasAnySample();

        int vl = 0;
        if (meta.hasExplicitVel) {
            vl = std::clamp(meta.velocityIndex, 0, kMaxVelLayers - 1);
        } else if (preferredVelLayer >= 0) {
            vl = std::clamp(preferredVelLayer, 0, kMaxVelLayers - 1);
        }

        auto& rrs = s.velLayers[vl].rrs;
        int targetIdx = -1;
        if (meta.hasExplicitMic && meta.mic != MicBus::Close) {
            int desiredRR = meta.hasExplicitRR ? meta.rrIndex : std::max(0, static_cast<int>(rrs.size()) - 1);
            desiredRR = std::clamp(desiredRR, 0, kMaxRoundRobins - 1);
            if (desiredRR < static_cast<int>(rrs.size()) && !rrs[static_cast<size_t>(desiredRR)].hasMic(meta.mic)) {
                targetIdx = desiredRR;
            }
        }
        if (targetIdx < 0) {
            if (static_cast<int>(rrs.size()) >= kMaxRoundRobins) {
                return false; // Hard limit of 9 RRs per velocity layer
            }
            MultiMicSample mm;
            mm.velMin = static_cast<float>(vl) / static_cast<float>(kMaxVelLayers);
            mm.velMax = static_cast<float>(vl + 1) / static_cast<float>(kMaxVelLayers);
            rrs.push_back(std::move(mm));
            targetIdx = static_cast<int>(rrs.size()) - 1;
        }
        rrs[static_cast<size_t>(targetIdx)].mics[static_cast<size_t>(meta.mic)] = buf;
        rrs[static_cast<size_t>(targetIdx)].sourcePathsUtf8[static_cast<size_t>(meta.mic)] = fullU8;

        if (s.name == "EMPTY") s.name = meta.cleanInstrumentName;
        if (!s.hasCustomColor) {
            s.keyColor = kStudioKeyColors[slotIdx % kNumKeyColors];
            s.hasCustomColor = true;
        }
        if (firstLoad) {
            s.stageX = 0.0f;
            s.stageY = 0.0f;
        }
        s.purged = false;
        recalcRamBytesLocked();
        if (outVelLayer) *outVelLayer = vl;
        if (outRR) *outRR = targetIdx;
        return true;
    }

    void triggerNoteOn(int midiNote, float velocity01) {
        std::lock_guard<std::mutex> lock(stateMutex_);
        velocity01 = std::clamp(velocity01, 0.01f, 1.0f);
        float shapedVel = applyVelocityCurve(velocity01);

        if (midiNote >= 0 && midiNote < 128)
            notePlaying_[midiNote].fetch_add(1, std::memory_order_relaxed);

        // Batch age prevents same-note slots in same choke group from choking each other
        uint64_t batchAge = voiceCounter_;

        for (int sIdx = 0; sIdx < kTotalSlots; ++sIdx) {
            auto& s = slots_[sIdx];
            if (s.midiNote != midiNote || s.purged || !s.hasAnySample()) continue;

            if (s.chokeGroup > 0) {
                for (auto& v : voices_) {
                    if (v.active && v.age <= batchAge && v.chokeGroup == s.chokeGroup) {
                        v.choking = true;
                        v.chokeDec = 1.0f / std::max(1.0f, sr_ * 0.005f);
                    }
                }
            }
            spawnVoice(sIdx, midiNote, shapedVel);
        }
    }

    void triggerNoteOff(int midiNote) {
        std::lock_guard<std::mutex> lock(stateMutex_);
        if (midiNote >= 0 && midiNote < 128) {
            int cur = notePlaying_[midiNote].load(std::memory_order_relaxed);
            notePlaying_[midiNote].store(std::max(0, cur - 1), std::memory_order_relaxed);
        }
        for (auto& v : voices_) {
            if (v.active && !v.oneShot && v.midiNote == midiNote)
                v.releasing = true;
        }
    }

    static std::string fmtFloatC(float v) {
        if (!std::isfinite(v)) v = 0.0f;
        std::ostringstream oss;
        oss.imbue(std::locale::classic());
        oss.precision(6);
        oss << std::fixed << v;
        return oss.str();
    }

    static float parseFloatC(const char* s) {
        if (!s) return 0.0f;
        std::istringstream iss(s);
        iss.imbue(std::locale::classic());
        float v = 0.0f;
        iss >> v;
        return std::isfinite(v) ? v : 0.0f;
    }

    static std::string sanitizeJsonStr(const std::string& in) {
        std::string out;
        out.reserve(in.size());
        for (char c : in) {
            if (c == '"' || c == '\\' || c == '\n' || c == '\r') continue;
            out.push_back(c);
        }
        return out.empty() ? "SLOT" : out;
    }

    // Format 5: Saves 36 slots + full 4-Bus Mixer + Global Fake Mic + 6 Master FX + PUNISH
    bool exportKitToNm6(const std::filesystem::path& targetPath, const std::string& libName) {
        std::lock_guard<std::mutex> lock(stateMutex_);
        Nm6Pack pack;
        pack.libraryName = sanitizeJsonStr(libName);

        const auto& p = params_;
        const auto& fx = p.fx;
        std::string json = "{\"format\":5,\"mix\":{";
        json += "\"bg0\":" + fmtFloatC(p.busGain[0]) +
                ",\"bg1\":" + fmtFloatC(p.busGain[1]) +
                ",\"bg2\":" + fmtFloatC(p.busGain[2]) +
                ",\"bg3\":" + fmtFloatC(p.busGain[3]) +
                ",\"bb0\":" + std::to_string(p.busBypass[0] ? 1 : 0) +
                ",\"bb1\":" + std::to_string(p.busBypass[1] ? 1 : 0) +
                ",\"bb2\":" + std::to_string(p.busBypass[2] ? 1 : 0) +
                ",\"bb3\":" + std::to_string(p.busBypass[3] ? 1 : 0) +
                ",\"gfm\":" + std::to_string(p.globalFakeMic ? 1 : 0) +
                ",\"fon\":" + std::to_string(fx.filterOn ? 1 : 0) +
                ",\"ftp\":" + std::to_string(fx.filterType) +
                ",\"fco\":" + fmtFloatC(fx.filterCutoffHz) +
                ",\"frs\":" + fmtFloatC(fx.filterRes) +
                ",\"eon\":" + std::to_string(fx.eqOn ? 1 : 0) +
                ",\"elf\":" + fmtFloatC(fx.eqLowFreq) +
                ",\"elg\":" + fmtFloatC(fx.eqLowGain) +
                ",\"emf\":" + fmtFloatC(fx.eqMidFreq) +
                ",\"emg\":" + fmtFloatC(fx.eqMidGain) +
                ",\"ehf\":" + fmtFloatC(fx.eqHighFreq) +
                ",\"ehg\":" + fmtFloatC(fx.eqHighGain) +
                ",\"con\":" + std::to_string(fx.compOn ? 1 : 0) +
                ",\"cth\":" + fmtFloatC(fx.compThreshDb) +
                ",\"crt\":" + fmtFloatC(fx.compRatio) +
                ",\"cat\":" + fmtFloatC(fx.compAttackMs) +
                ",\"crl\":" + fmtFloatC(fx.compReleaseMs) +
                ",\"ckn\":" + fmtFloatC(fx.compKneeDb) +
                ",\"cmk\":" + fmtFloatC(fx.compMakeupDb) +
                ",\"son\":" + std::to_string(fx.satOn ? 1 : 0) +
                ",\"smd\":" + std::to_string(fx.satMode) +
                ",\"sdr\":" + fmtFloatC(fx.satDrive) +
                ",\"swm\":" + fmtFloatC(fx.satWarmth) +
                ",\"smx\":" + fmtFloatC(fx.satMix) +
                ",\"don\":" + std::to_string(fx.delayOn ? 1 : 0) +
                ",\"dmd\":" + std::to_string(fx.delayMode) +
                ",\"dtm\":" + fmtFloatC(fx.delayTimeMs) +
                ",\"dfb\":" + fmtFloatC(fx.delayFeedback) +
                ",\"dtn\":" + fmtFloatC(fx.delayTone) +
                ",\"dwd\":" + fmtFloatC(fx.delayWidth) +
                ",\"dmx\":" + fmtFloatC(fx.delayMix) +
                ",\"ron\":" + std::to_string(fx.revOn ? 1 : 0) +
                ",\"rmd\":" + std::to_string(fx.revMode) +
                ",\"rpd\":" + fmtFloatC(fx.revPreDelayMs) +
                ",\"rdc\":" + fmtFloatC(fx.revDecaySec) +
                ",\"rsz\":" + fmtFloatC(fx.revSize) +
                ",\"rlc\":" + fmtFloatC(fx.revLowCutHz) +
                ",\"rhc\":" + fmtFloatC(fx.revHighCutHz) +
                ",\"rmx\":" + fmtFloatC(fx.revMix) +
                ",\"lim\":" + std::to_string(fx.limiterOn ? 1 : 0) +
                ",\"mbs\":" + std::to_string(fx.monoBass ? 1 : 0) +
                ",\"pon\":" + std::to_string(fx.punishOn ? 1 : 0) +
                ",\"pam\":" + fmtFloatC(fx.punishAmount) +
                ",\"pmd\":" + std::to_string(static_cast<int>(fx.punishMode));
        json += "},\"slots\":[";
        for (int i = 0; i < kTotalSlots; ++i) {
            const auto& s = slots_[i];
            if (i > 0) json += ",";
            json += "{\"i\":" + std::to_string(i) +
                    ",\"n\":\"" + sanitizeJsonStr(s.name) + "\"" +
                    ",\"m\":" + std::to_string(s.midiNote) +
                    ",\"c\":" + std::to_string(s.hasCustomColor ? s.keyColor : 0u) +
                    ",\"x\":" + fmtFloatC(s.stageX) +
                    ",\"y\":" + fmtFloatC(s.stageY) +
                    ",\"g\":" + fmtFloatC(s.gain) +
                    ",\"p\":" + fmtFloatC(s.pitchSemitones) +
                    ",\"a\":" + fmtFloatC(s.attackMs) +
                    ",\"r\":" + fmtFloatC(s.releaseMs) +
                    ",\"fr\":" + std::to_string(s.fakeRR ? 1 : 0) +
                    ",\"fv\":" + std::to_string(s.fakeVel ? 1 : 0) +
                    ",\"os\":" + std::to_string(s.oneShot ? 1 : 0) +
                    ",\"cg\":" + std::to_string(s.chokeGroup) +
                    ",\"rv\":" + std::to_string(s.reverse ? 1 : 0) +
                    ",\"s\":[";
            bool firstSamp = true;
            for (int vl = 0; vl < kMaxVelLayers; ++vl) {
                const auto& rrs = s.velLayers[vl].rrs;
                for (size_t r = 0; r < rrs.size(); ++r) {
                    const auto& it = rrs[r];
                    for (int mic = 0; mic < 4; ++mic) {
                        const auto& ptr = it.mics[static_cast<size_t>(mic)];
                        if (!ptr || ptr->empty()) continue;
                        uint32_t sid = pack.addOrFindSample(*ptr);
                        if (!firstSamp) json += ",";
                        firstSamp = false;
                        json += "[" + std::to_string(vl) + "," + std::to_string(r) + "," + std::to_string(mic) + "," +
                                std::to_string(sid) + "," + fmtFloatC(it.startNorm) + "," + fmtFloatC(it.endNorm) + "," +
                                fmtFloatC(it.fadeInNorm) + "," + fmtFloatC(it.fadeOutNorm) + "]";
                    }
                }
            }
            json += "]}";
        }
        json += "]}";
        pack.kitMetadataJson = json;
        return pack.saveAtomic(targetPath);
    }

    bool loadKitFromNm6(const std::filesystem::path& packPath, bool* outHasMixParams = nullptr, bool loadMixParams = true) {
        Nm6Pack pack;
        if (!pack.loadFromFile(packPath)) return false;

        std::vector<std::shared_ptr<AudioBuffer>> decoded(pack.samples.size());
        for (size_t i = 0; i < pack.samples.size(); ++i) {
            auto b = std::make_shared<AudioBuffer>();
            if (pack.decodeSample(static_cast<uint32_t>(i), *b)) decoded[i] = b;
        }

        std::lock_guard<std::mutex> lock(stateMutex_);
        for (auto& v : voices_) v.active = false;
        for (int i = 0; i < kTotalSlots; ++i) {
            resetSlotDefaultsLocked(slots_[i]);
        }

        const std::string& js = pack.kitMetadataJson;
        bool hasMix = false;
        auto mixPos = js.find("\"mix\":{");
        if (loadMixParams && mixPos != std::string::npos) {
            auto mixEnd = js.find("},\"slots\"", mixPos);
            if (mixEnd != std::string::npos) {
                hasMix = true;
                std::string mChunk = js.substr(mixPos, mixEnd - mixPos + 1);
                auto readMInt = [&](const char* k, int& dst) {
                    auto kp = mChunk.find(k);
                    if (kp != std::string::npos) dst = std::atoi(mChunk.c_str() + kp + std::strlen(k));
                };
                auto readMBool = [&](const char* k, bool& dst) {
                    auto kp = mChunk.find(k);
                    if (kp != std::string::npos) dst = (std::atoi(mChunk.c_str() + kp + std::strlen(k)) != 0);
                };
                auto readMFlt = [&](const char* k, float& dst) {
                    auto kp = mChunk.find(k);
                    if (kp != std::string::npos) dst = parseFloatC(mChunk.c_str() + kp + std::strlen(k));
                };
                auto& p = params_;
                auto& fx = p.fx;
                readMFlt("\"bg0\":", p.busGain[0]); readMFlt("\"bg1\":", p.busGain[1]);
                readMFlt("\"bg2\":", p.busGain[2]); readMFlt("\"bg3\":", p.busGain[3]);
                readMBool("\"bb0\":", p.busBypass[0]); readMBool("\"bb1\":", p.busBypass[1]);
                readMBool("\"bb2\":", p.busBypass[2]); readMBool("\"bb3\":", p.busBypass[3]);
                readMBool("\"gfm\":", p.globalFakeMic);
                readMBool("\"fon\":", fx.filterOn); readMInt("\"ftp\":", fx.filterType);
                readMFlt("\"fco\":", fx.filterCutoffHz); readMFlt("\"frs\":", fx.filterRes);
                readMBool("\"eon\":", fx.eqOn);
                readMFlt("\"elf\":", fx.eqLowFreq); readMFlt("\"elg\":", fx.eqLowGain);
                readMFlt("\"emf\":", fx.eqMidFreq); readMFlt("\"emg\":", fx.eqMidGain);
                readMFlt("\"ehf\":", fx.eqHighFreq); readMFlt("\"ehg\":", fx.eqHighGain);
                readMBool("\"con\":", fx.compOn); readMFlt("\"cth\":", fx.compThreshDb);
                readMFlt("\"crt\":", fx.compRatio); readMFlt("\"cat\":", fx.compAttackMs);
                readMFlt("\"crl\":", fx.compReleaseMs); readMFlt("\"ckn\":", fx.compKneeDb);
                readMFlt("\"cmk\":", fx.compMakeupDb);
                readMBool("\"son\":", fx.satOn); readMInt("\"smd\":", fx.satMode);
                readMFlt("\"sdr\":", fx.satDrive); readMFlt("\"swm\":", fx.satWarmth); readMFlt("\"smx\":", fx.satMix);
                readMBool("\"don\":", fx.delayOn); readMInt("\"dmd\":", fx.delayMode);
                readMFlt("\"dtm\":", fx.delayTimeMs); readMFlt("\"dfb\":", fx.delayFeedback);
                readMFlt("\"dtn\":", fx.delayTone); readMFlt("\"dwd\":", fx.delayWidth); readMFlt("\"dmx\":", fx.delayMix);
                readMBool("\"ron\":", fx.revOn); readMInt("\"rmd\":", fx.revMode);
                readMFlt("\"rpd\":", fx.revPreDelayMs); readMFlt("\"rdc\":", fx.revDecaySec);
                readMFlt("\"rsz\":", fx.revSize); readMFlt("\"rlc\":", fx.revLowCutHz);
                readMFlt("\"rhc\":", fx.revHighCutHz); readMFlt("\"rmx\":", fx.revMix);
                readMBool("\"lim\":", fx.limiterOn); readMBool("\"mbs\":", fx.monoBass);
                readMBool("\"pon\":", fx.punishOn); readMFlt("\"pam\":", fx.punishAmount);
                int pmd = static_cast<int>(fx.punishMode);
                readMInt("\"pmd\":", pmd);
                fx.punishMode = static_cast<PunishMode>(std::clamp(pmd, 0, 2));
            }
        }
        if (outHasMixParams) *outHasMixParams = hasMix;

        size_t pos = 0;
        while ((pos = js.find("{\"i\":", pos)) != std::string::npos) {
            int idx = std::atoi(js.c_str() + pos + 5);
            size_t endObj = js.find("]}", pos);
            if (endObj == std::string::npos) break;
            std::string chunk = js.substr(pos, endObj - pos + 2);
            if (idx >= 0 && idx < kTotalSlots) {
                auto& s = slots_[idx];
                auto nPos = chunk.find("\"n\":\"");
                if (nPos != std::string::npos) {
                    auto nEnd = chunk.find("\"", nPos + 5);
                    if (nEnd != std::string::npos) s.name = chunk.substr(nPos + 5, nEnd - (nPos + 5));
                }
                auto readIntKey = [&](const char* k, int& dst) {
                    auto kp = chunk.find(k);
                    if (kp != std::string::npos) dst = std::atoi(chunk.c_str() + kp + std::strlen(k));
                };
                auto readFltKey = [&](const char* k, float& dst) {
                    auto kp = chunk.find(k);
                    if (kp != std::string::npos) dst = parseFloatC(chunk.c_str() + kp + std::strlen(k));
                };
                readIntKey("\"m\":", s.midiNote);
                auto cPos = chunk.find("\"c\":");
                if (cPos != std::string::npos) {
                    s.keyColor = static_cast<uint32_t>(std::strtoul(chunk.c_str() + cPos + 4, nullptr, 10));
                    s.hasCustomColor = (s.keyColor != 0);
                }
                readFltKey("\"x\":", s.stageX);
                readFltKey("\"y\":", s.stageY);
                readFltKey("\"g\":", s.gain);
                readFltKey("\"p\":", s.pitchSemitones);
                readFltKey("\"a\":", s.attackMs);
                readFltKey("\"r\":", s.releaseMs);
                int bTmp = 0;
                if (chunk.find("\"fr\":") != std::string::npos) { readIntKey("\"fr\":", bTmp); s.fakeRR = (bTmp != 0); }
                if (chunk.find("\"fv\":") != std::string::npos) { readIntKey("\"fv\":", bTmp); s.fakeVel = (bTmp != 0); }
                if (chunk.find("\"os\":") != std::string::npos) { readIntKey("\"os\":", bTmp); s.oneShot = (bTmp != 0); }
                readIntKey("\"cg\":", s.chokeGroup);
                if (chunk.find("\"rv\":") != std::string::npos) { readIntKey("\"rv\":", bTmp); s.reverse = (bTmp != 0); }

                auto sArr = chunk.find("\"s\":[");
                if (sArr != std::string::npos) {
                    size_t p = sArr + 5;
                    while ((p = chunk.find('[', p)) != std::string::npos) {
                        size_t closeBr = chunk.find(']', p);
                        if (closeBr == std::string::npos) break;
                        std::string tupleStr = chunk.substr(p + 1, closeBr - (p + 1));
                        std::replace(tupleStr.begin(), tupleStr.end(), ',', ' ');
                        std::istringstream tiss(tupleStr);
                        tiss.imbue(std::locale::classic());
                        int vl = 0, r = 0, mic = 0, sid = 0;
                        float st = 0.0f, en = 1.0f, fi = 0.0f, fo = 0.0f;
                        if (tiss >> vl >> r >> mic >> sid) {
                            tiss >> st >> en >> fi >> fo;
                            if (vl >= 0 && vl < kMaxVelLayers && r >= 0 && r < kMaxRoundRobins &&
                                mic >= 0 && mic < 4 && sid >= 0 && static_cast<size_t>(sid) < decoded.size() && decoded[sid]) {
                                auto& rrs = s.velLayers[vl].rrs;
                                if (static_cast<int>(rrs.size()) <= r) rrs.resize(static_cast<size_t>(r + 1));
                                auto& it = rrs[static_cast<size_t>(r)];
                                it.mics[static_cast<size_t>(mic)] = decoded[sid];
                                it.startNorm = st; it.endNorm = en; it.fadeInNorm = fi; it.fadeOutNorm = fo;
                            }
                        }
                        p = chunk.find(']', p);
                        if (p == std::string::npos) break;
                        ++p;
                    }
                }
            }
            pos = endObj + 2;
        }
        recalcRamBytesLocked();
        return true;
    }

    // Ultra-Low CPU Block-Based Audio Processing (Active Voice List + Fast-Path Playback + Multi-Out Safe Idle Sleep)
    void process(ProcessBuffers& pb) {
        if (!pb.mainL || !pb.mainR || pb.numSamples <= 0) return;

        const int N = pb.numSamples;
        bool runClose = !params_.busBypass[0] && params_.busGain[0] > 0.0001f;
        bool runRoom  = !params_.busBypass[1] && params_.busGain[1] > 0.0001f;
        bool runHall  = !params_.busBypass[2] && params_.busGain[2] > 0.0001f;
        bool runSub   = !params_.busBypass[3] && params_.busGain[3] > 0.0001f;

        std::unique_lock<std::mutex> lock(stateMutex_, std::try_to_lock);
        if (!lock.owns_lock() && !pb.offlineRender) {
            std::fill(pb.mainL, pb.mainL + N, 0.0f);
            std::fill(pb.mainR, pb.mainR + N, 0.0f);
            return;
        }
        if (!lock.owns_lock() && pb.offlineRender) lock.lock();

        // Collect ONLY active voices once per block
        Voice* activeList[kMaxVoices];
        int activeCount = 0;
        for (auto& v : voices_) {
            if (v.active && v.primaryBuf && v.primLen >= 4) {
                activeList[activeCount++] = &v;
            }
        }

        if (activeCount == 0 && idleSleeping_.load(std::memory_order_relaxed)) {
            std::fill(pb.mainL, pb.mainL + N, 0.0f);
            std::fill(pb.mainR, pb.mainR + N, 0.0f);
            for (int b = 0; b < 4; ++b) {
                if (pb.auxActive[b] && pb.auxL[b] && pb.auxR[b]) {
                    std::fill(pb.auxL[b], pb.auxL[b] + N, 0.0f);
                    std::fill(pb.auxR[b], pb.auxR[b] + N, 0.0f);
                }
            }
            activeVoices_.store(0, std::memory_order_relaxed);
            return;
        }

        ensureScratch(N);
        float* cL = scratchCloseL_.data(); float* cR = scratchCloseR_.data();
        float* rrL = scratchRoomRealL_.data(); float* rrR = scratchRoomRealR_.data();
        float* srL = scratchSendRoomL_.data(); float* srR = scratchSendRoomR_.data();
        float* hrL = scratchHallRealL_.data(); float* hrR = scratchHallRealR_.data();
        float* shL = scratchSendHallL_.data(); float* shR = scratchSendHallR_.data();
        float* sbL = scratchSubRealL_.data();  float* sbR = scratchSubRealR_.data();
        float* ssL = scratchSendSubL_.data();  float* ssR = scratchSendSubR_.data();

        std::fill(cL, cL + N, 0.0f); std::fill(cR, cR + N, 0.0f);
        if (runRoom) {
            std::fill(rrL, rrL + N, 0.0f); std::fill(rrR, rrR + N, 0.0f);
            std::fill(srL, srL + N, 0.0f); std::fill(srR, srR + N, 0.0f);
        }
        if (runHall) {
            std::fill(hrL, hrL + N, 0.0f); std::fill(hrR, hrR + N, 0.0f);
            std::fill(shL, shL + N, 0.0f); std::fill(shR, shR + N, 0.0f);
        }
        if (runSub) {
            std::fill(sbL, sbL + N, 0.0f); std::fill(sbR, sbR + N, 0.0f);
            std::fill(ssL, ssL + N, 0.0f); std::fill(ssR, ssR + N, 0.0f);
        }

        // Reset synthetic mic reverb tails cleanly on OFF -> ON transition
        if (runRoom && !wasRoomRun_) roomBus_.reset();
        wasRoomRun_ = runRoom;
        if (runHall && !wasHallRun_) hallBus_.reset();
        wasHallRun_ = runHall;

        // Voice-outer, sample-inner block loop (dramatically higher CPU L1 cache hit rate!)
        int remainingActive = 0;
        for (int vi = 0; vi < activeCount; ++vi) {
            Voice& v = *activeList[vi];
            const double regionLen = std::max(8.0, v.endFrame - v.startFrame);
            const float depth = std::clamp(v.stageDepth, 0.0f, 1.0f);
            const float closeDryScale = 1.0f - depth * 0.62f;
            const float roomDepthSend = 0.35f + depth * 1.15f;
            const float hallDepthSend = 0.20f + depth * 1.85f;
            const bool needRoomSend = !v.hasRealRoom && v.synthRoom && runRoom;
            const bool needHallSend = !v.hasRealHall && v.synthHall && runHall;
            const bool needSubSend  = !v.hasRealSub  && v.synthSub  && runSub;
            const bool needRealRoom = v.hasRealRoom && runRoom;
            const bool needRealHall = v.hasRealHall && runHall;
            const bool needRealSub  = v.hasRealSub  && runSub;
            const bool hasExtraDsp  = v.useToneEq || v.useFormant || v.useVelLpf || (v.velDrive > 0.01f) || v.useStageLpf;

            for (int n = 0; n < N; ++n) {
                if (v.choking) {
                    v.chokeFade -= v.chokeDec;
                    if (v.chokeFade <= 0.0f) { v.active = false; break; }
                }

                double relPos = v.playPos - v.startFrame;
                if (relPos < 0.0 || relPos >= regionLen) {
                    v.active = false;
                    break;
                }

                if (!v.releasing) {
                    if (v.envVal < 1.0f) v.envVal = std::min(1.0f, v.envVal + v.attackInc);
                    if (v.oneShot && v.elapsedHostSamples >= v.releaseStartFrame) {
                        v.releasing = true;
                    }
                }
                if (v.releasing) {
                    v.envVal *= v.releaseMul;
                    if (v.envVal <= 0.0005f) { v.active = false; break; }
                }

                float waveEnv = 1.0f;
                if (v.fadeInFrames > 1.0 && relPos < v.fadeInFrames) {
                    float tIn = static_cast<float>(relPos / v.fadeInFrames);
                    waveEnv *= tIn * tIn;
                }
                double rem = regionLen - relPos;
                if (v.fadeOutFrames > 1.0 && rem < v.fadeOutFrames) {
                    float tOut = static_cast<float>(std::max(0.0, rem) / v.fadeOutFrames);
                    waveEnv *= tOut * tOut;
                }

                float totalGain = v.envVal * waveEnv * v.chokeFade * v.baseGain;

                float sL = 0.0f, sR = 0.0f;
                if (v.unitPitch) {
                    size_t idx = v.reverse
                        ? static_cast<size_t>(std::clamp<int64_t>(static_cast<int64_t>(v.primLen - 1) - static_cast<int64_t>(v.playPos), 0, static_cast<int64_t>(v.primLen - 1)))
                        : static_cast<size_t>(v.playPos);
                    if (idx >= v.primLen) { v.active = false; break; }
                    sL = v.primL[idx];
                    sR = v.primR[idx];
                } else {
                    if (!readHermite(*v.primaryBuf, v.playPos, v.reverse, sL, sR)) {
                        v.active = false;
                        break;
                    }
                }

                if (hasExtraDsp) {
                    if (v.useToneEq) {
                        sL = v.toneHiL.process(v.toneLowL.process(sL));
                        sR = v.toneHiR.process(v.toneLowR.process(sR));
                    }
                    if (v.useFormant) {
                        sL = v.formantL.process(sL);
                        sR = v.formantR.process(sR);
                    }
                    if (v.useVelLpf) {
                        sL = v.velLpfL.process(sL);
                        sR = v.velLpfR.process(sR);
                    }
                    if (v.velDrive > 0.01f) {
                        float d = 1.0f + v.velDrive * 1.5f;
                        sL = fastTanh(sL * d) / d * 1.15f;
                        sR = fastTanh(sR * d) / d * 1.15f;
                    }
                    if (v.useStageLpf) {
                        sL = v.stageLpfL.process(sL);
                        sR = v.stageLpfR.process(sR);
                    }
                }

                float stagedL = sL * totalGain * v.panL;
                float stagedR = sR * totalGain * v.panR;

                cL[n] += stagedL * closeDryScale;
                cR[n] += stagedR * closeDryScale;

                if (needRealRoom) {
                    float rl = 0.0f, rr = 0.0f;
                    if (readHermite(*v.micBufs[1], v.playPos, v.reverse, rl, rr)) {
                        rrL[n] += rl * totalGain * v.panL * roomDepthSend;
                        rrR[n] += rr * totalGain * v.panR * roomDepthSend;
                    }
                } else if (needRoomSend) {
                    srL[n] += stagedL * roomDepthSend;
                    srR[n] += stagedR * roomDepthSend;
                }

                if (needRealHall) {
                    float hl = 0.0f, hr = 0.0f;
                    if (readHermite(*v.micBufs[2], v.playPos, v.reverse, hl, hr)) {
                        hrL[n] += hl * totalGain * v.panL * hallDepthSend;
                        hrR[n] += hr * totalGain * v.panR * hallDepthSend;
                    }
                } else if (needHallSend) {
                    shL[n] += stagedL * hallDepthSend;
                    shR[n] += stagedR * hallDepthSend;
                }

                if (needRealSub) {
                    float ll = 0.0f, lr = 0.0f;
                    if (readHermite(*v.micBufs[3], v.playPos, v.reverse, ll, lr)) {
                        float m = 0.5f * (ll + lr) * totalGain;
                        sbL[n] += m; sbR[n] += m;
                    }
                } else if (needSubSend) {
                    ssL[n] += stagedL; ssR[n] += stagedR;
                }

                v.playPos += v.pitchRatio;
                v.elapsedHostSamples += 1.0;
            }
            if (v.active) ++remainingActive;
        }

        // Mix 4 Mic Buses + Master FX + Multi-Out Aux peak tracking
        float blockPeak = 0.0f;
        const float g0 = params_.busGain[0], g1 = params_.busGain[1], g2 = params_.busGain[2], g3 = params_.busGain[3];

        for (int n = 0; n < N; ++n) {
            float busL[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
            float busR[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

            if (runClose) { busL[0] = cL[n] * g0; busR[0] = cR[n] * g0; }
            if (runRoom) {
                float sL = 0.0f, sR = 0.0f;
                roomBus_.process(srL[n], srR[n], sL, sR);
                busL[1] = (rrL[n] + sL) * g1;
                busR[1] = (rrR[n] + sR) * g1;
            }
            if (runHall) {
                float sL = 0.0f, sR = 0.0f;
                hallBus_.process(shL[n], shR[n], sL, sR);
                busL[2] = (hrL[n] + sL) * g2;
                busR[2] = (hrR[n] + sR) * g2;
            }
            if (runSub) {
                float sL = 0.0f, sR = 0.0f;
                subBus_.process(ssL[n], ssR[n], sL, sR);
                busL[3] = (sbL[n] + sL) * g3;
                busR[3] = (sbR[n] + sR) * g3;
            }

            float mainL = 0.0f, mainR = 0.0f;
            for (int b = 0; b < 4; ++b) {
                if (pb.auxActive[b] && pb.auxL[b] && pb.auxR[b]) {
                    pb.auxL[b][n] = busL[b]; pb.auxR[b][n] = busR[b];
                    float auxPk = std::max(std::fabs(busL[b]), std::fabs(busR[b]));
                    if (auxPk > blockPeak) blockPeak = auxPk;
                } else {
                    mainL += busL[b]; mainR += busR[b];
                }
            }

            masterFx_.process(mainL, mainR, params_.fx);
            pb.mainL[n] = mainL;
            pb.mainR[n] = mainR;
            float mPk = std::max(std::fabs(mainL), std::fabs(mainR));
            if (mPk > blockPeak) blockPeak = mPk;
        }

        activeVoices_.store(remainingActive, std::memory_order_relaxed);
        if (remainingActive == 0 && blockPeak < 1e-5f) {
            idleSamples_ += N;
            if (idleSamples_ > static_cast<int>(sr_ * 0.25f)) {
                idleSleeping_.store(true, std::memory_order_relaxed);
            }
        } else {
            idleSamples_ = 0;
            idleSleeping_.store(false, std::memory_order_relaxed);
        }
    }

private:
    void ensureScratch(int n) {
        if (static_cast<int>(scratchCloseL_.size()) >= n) return;
        size_t sz = static_cast<size_t>(std::max(n, 2048));
        scratchCloseL_.assign(sz, 0.0f);    scratchCloseR_.assign(sz, 0.0f);
        scratchRoomRealL_.assign(sz, 0.0f); scratchRoomRealR_.assign(sz, 0.0f);
        scratchSendRoomL_.assign(sz, 0.0f); scratchSendRoomR_.assign(sz, 0.0f);
        scratchHallRealL_.assign(sz, 0.0f); scratchHallRealR_.assign(sz, 0.0f);
        scratchSendHallL_.assign(sz, 0.0f); scratchSendHallR_.assign(sz, 0.0f);
        scratchSubRealL_.assign(sz, 0.0f);  scratchSubRealR_.assign(sz, 0.0f);
        scratchSendSubL_.assign(sz, 0.0f);  scratchSendSubR_.assign(sz, 0.0f);
    }

    float applyVelocityCurve(float v) const {
        if (params_.velCurve == VelocityCurve::Soft) return std::pow(v, 0.65f);
        if (params_.velCurve == VelocityCurve::Hard) return std::pow(v, 1.55f);
        return v;
    }

    // Fixed Reverse Sample-0 boundary check (`idxF > n - 1` instead of `>=`)
    static bool readHermite(const AudioBuffer& buf, double pos, bool rev, float& outL, float& outR) {
        size_t n = buf.numFrames();
        if (n < 4) return false;
        double maxIdx = static_cast<double>(n - 1);
        double idxF = rev ? (maxIdx - pos) : pos;
        if (idxF < 0.0 || idxF > maxIdx) return false;

        int i1 = std::min(static_cast<int>(idxF), static_cast<int>(n - 1));
        float t = static_cast<float>(idxF - static_cast<double>(i1));
        if (t <= 1e-6f) {
            outL = buf.left[i1];
            outR = buf.right[i1];
            return true;
        }
        int i0 = std::max(0, i1 - 1);
        int i2 = std::min(static_cast<int>(n - 1), i1 + 1);
        int i3 = std::min(static_cast<int>(n - 1), i1 + 2);

        const float* lPtr = buf.left.data();
        const float* rPtr = buf.right.data();
        auto hermite = [&](const float* ch) -> float {
            float y0 = ch[i0], y1 = ch[i1], y2 = ch[i2], y3 = ch[i3];
            float c0 = y1;
            float c1 = 0.5f * (y2 - y0);
            float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
            float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
            return ((c3 * t + c2) * t + c1) * t + c0;
        };
        outL = hermite(lPtr);
        outR = hermite(rPtr);
        return true;
    }

    int selectVelocityLayer(const SlotConfig& s, float shapedVel) const {
        int populated[kMaxVelLayers];
        int popCount = 0;
        for (int vl = 0; vl < kMaxVelLayers; ++vl) {
            if (!s.velLayers[vl].rrs.empty()) populated[popCount++] = vl;
        }
        if (popCount == 0) return 0;
        if (popCount == 1) return populated[0];

        int bucket = std::clamp(static_cast<int>(shapedVel * static_cast<float>(kMaxVelLayers)), 0, kMaxVelLayers - 1);
        if (!s.velLayers[bucket].rrs.empty()) return bucket;

        int bestVl = populated[0];
        int bestDist = std::abs(bestVl - bucket);
        for (int i = 1; i < popCount; ++i) {
            int d = std::abs(populated[i] - bucket);
            if (d < bestDist) { bestDist = d; bestVl = populated[i]; }
        }
        return bestVl;
    }

    void spawnVoice(int slotIdx, int midiNote, float vel) {
        auto& s = slots_[slotIdx];
        if (!s.hasAnySample()) return;

        Voice* target = nullptr;
        uint64_t oldestAge = UINT64_MAX;
        for (auto& v : voices_) {
            if (!v.active) { target = &v; break; }
            if (v.age < oldestAge) { oldestAge = v.age; target = &v; }
        }
        if (!target) return;

        int vlIdx = selectVelocityLayer(s, vel);
        auto& layer = s.velLayers[vlIdx];
        int numRRs = static_cast<int>(layer.rrs.size());
        if (numRRs <= 0) return;

        int chosenRR = 0;
        if (numRRs == 2) {
            chosenRR = (layer.lastRRIdx == 0) ? 1 : 0;
        } else if (numRRs > 2) {
            rngState_ = rngState_ * 1664525u + 1013904223u;
            int pick = static_cast<int>((rngState_ >> 16) % static_cast<uint32_t>(numRRs));
            chosenRR = (pick == layer.lastRRIdx) ? ((pick + 1) % numRRs) : pick;
        }
        layer.lastRRIdx = chosenRR;
        const auto& mm = layer.rrs[static_cast<size_t>(chosenRR)];
        auto prim = mm.primarySource();
        if (!prim || prim->numFrames() < 4) return;

        float rrStrength  = s.fakeRR  ? ((numRRs <= 2) ? 1.0f : 0.45f) : 0.0f;
        float velStrength = s.fakeVel ? 1.0f : 0.0f;
        bool enableFakeMics = params_.globalFakeMic;

        Voice& v = *target;
        v.active = true;
        v.releasing = false;
        v.choking = false;
        v.slotIndex = slotIdx;
        v.velLayerIndex = vlIdx;
        v.rrIndex = chosenRR;
        v.midiNote = midiNote;
        v.chokeGroup = s.chokeGroup;
        v.reverse = s.reverse;
        v.oneShot = s.oneShot;
        v.micBufs = mm.mics;
        v.primaryBuf = prim;
        v.primL = prim->left.data();
        v.primR = prim->right.data();
        v.primLen = prim->numFrames();
        v.slotGain = s.gain;
        v.stageDepth = s.stageY;
        v.chokeFade = 1.0f;
        v.elapsedHostSamples = 0.0;
        v.age = ++voiceCounter_;

        lastVoiceSlot_.store(slotIdx, std::memory_order_relaxed);
        lastVoiceVel_.store(vlIdx, std::memory_order_relaxed);
        lastVoiceRR_.store(chosenRR, std::memory_order_relaxed);
        voiceSeq_.fetch_add(1, std::memory_order_relaxed);

        idleSamples_ = 0;
        idleSleeping_.store(false, std::memory_order_relaxed);

        double totalF = static_cast<double>(v.primLen);
        v.startFrame = std::min(std::max(0.0, totalF - 4.0), std::clamp(static_cast<double>(mm.startNorm), 0.0, 0.98) * totalF);
        v.endFrame   = std::min(totalF, std::max(v.startFrame + 4.0, std::clamp(static_cast<double>(mm.endNorm), 0.02, 1.0) * totalF));
        double regLen = std::max(8.0, v.endFrame - v.startFrame);
        v.fadeInFrames  = std::clamp(static_cast<double>(mm.fadeInNorm), 0.0, 0.5) * regLen;
        v.fadeOutFrames = std::clamp(static_cast<double>(mm.fadeOutNorm), 0.0, 0.5) * regLen;

        if (std::fabs(s.stageX) < 0.005f) {
            v.panL = 0.70710678f;
            v.panR = 0.70710678f;
        } else {
            float pan01 = (s.stageX + 1.0f) * 0.5f;
            v.panL = std::cos(pan01 * kPi * 0.5f);
            v.panR = std::sin(pan01 * kPi * 0.5f);
        }

        v.useStageLpf = (s.stageY > 0.01f);
        if (v.useStageLpf) {
            float depthCutoff = 19500.0f - std::clamp(s.stageY, 0.0f, 1.0f) * 12500.0f;
            v.stageLpfL.setLowPass(sr_, depthCutoff); v.stageLpfR.setLowPass(sr_, depthCutoff);
            v.stageLpfL.reset(); v.stageLpfR.reset();
        }

        v.useToneEq = (std::fabs(s.toneLowDb) > 0.05f || std::fabs(s.toneHighDb) > 0.05f);
        if (v.useToneEq) {
            v.toneLowL.setPeaking(sr_, 110.0f, 0.8f, s.toneLowDb);
            v.toneLowR.setPeaking(sr_, 110.0f, 0.8f, s.toneLowDb);
            v.toneHiL.setPeaking(sr_, 6500.0f, 0.8f, s.toneHighDb);
            v.toneHiR.setPeaking(sr_, 6500.0f, 0.8f, s.toneHighDb);
            v.toneLowL.reset(); v.toneLowR.reset(); v.toneHiL.reset(); v.toneHiR.reset();
        }

        static constexpr float kStartMs[6]    = { 0.0f, 1.1f, 2.3f, 0.5f, 1.8f, 3.0f };
        static constexpr float kFormantHz[6]  = { 320.0f, 540.0f, 760.0f, 430.0f, 920.0f, 620.0f };
        static constexpr float kFormantDb[6]  = { 1.8f, -1.6f, 1.5f, -1.2f, 1.3f, -1.7f };
        static constexpr float kPitchCents[6] = { 0.0f, 3.5f, -3.0f, 2.0f, -2.5f, 1.5f };
        int step = s.fakeRRStep;
        s.fakeRRStep = (s.fakeRRStep + 1) % 6;

        double sampleRateRatio = static_cast<double>(prim->sampleRate) / static_cast<double>(sr_);
        double centsOffset = (rrStrength > 0.01f) ? (kPitchCents[step] * rrStrength) : 0.0;
        double totalSemi = static_cast<double>(s.pitchSemitones) + centsOffset / 100.0;
        v.pitchRatio = (std::fabs(totalSemi) < 1e-4) ? sampleRateRatio : (sampleRateRatio * std::pow(2.0, totalSemi / 12.0));
        v.unitPitch = (std::fabs(v.pitchRatio - 1.0) < 1e-6);

        double rrStartOffset = (rrStrength > 0.01f && !s.reverse) ? (kStartMs[step] * rrStrength * 0.001 * prim->sampleRate) : 0.0;
        v.playPos = std::min(v.endFrame - 4.0, v.startFrame + rrStartOffset);

        v.useFormant = (rrStrength > 0.05f);
        if (v.useFormant) {
            v.formantL.setPeaking(sr_, kFormantHz[step], 1.1f, kFormantDb[step] * rrStrength);
            v.formantR.setPeaking(sr_, kFormantHz[step], 1.1f, kFormantDb[step] * rrStrength);
            v.formantL.reset(); v.formantR.reset();
        }

        v.velGain = std::pow(vel, 1.35f);
        v.baseGain = v.velGain * v.slotGain;

        float extraAttackMs = (velStrength > 0.01f) ? ((1.0f - vel) * 8.5f * velStrength) : 0.0f;
        float totalAttackMs = std::max(0.2f, s.attackMs + extraAttackMs);
        double attSamples = sr_ * totalAttackMs * 0.001;
        v.attackInc = (attSamples <= 1.0) ? 1.0f : static_cast<float>(1.0 / attSamples);

        // Natural Exponential Percussion Release: reaches -60 dB (0.001) in releaseMs
        double relSamples = std::max(16.0, static_cast<double>(sr_) * std::max(10.0f, s.releaseMs) * 0.001);
        v.releaseMul = fastExp(-6.907755f / static_cast<float>(relSamples));
        v.releaseStartFrame = std::max(attSamples * 0.5, relSamples * 0.25);
        if (s.releaseMs >= 3800.0f) {
            v.releaseStartFrame = 1e12;
        }
        v.envVal = (attSamples <= 1.0) ? 1.0f : 0.0f;

        v.useVelLpf = (velStrength > 0.05f && vel < 0.92f);
        if (v.useVelLpf) {
            float cutoff = 2200.0f + std::pow(vel, 1.8f) * 17500.0f;
            float blendedCutoff = 19500.0f * (1.0f - velStrength) + cutoff * velStrength;
            v.velLpfL.setLowPass(sr_, blendedCutoff); v.velLpfR.setLowPass(sr_, blendedCutoff);
            v.velLpfL.reset(); v.velLpfR.reset();
        }
        v.velDrive = (vel > 0.72f) ? ((vel - 0.72f) * 3.0f * velStrength) : 0.0f;

        // Prevent double-playback when a single Sub/Room/Hall file is the primarySource
        v.hasRealRoom = mm.hasMic(MicBus::Room) && (mm.mics[1] != prim);
        v.hasRealHall = mm.hasMic(MicBus::Hall) && (mm.mics[2] != prim);
        v.hasRealSub  = mm.hasMic(MicBus::Sub)  && (mm.mics[3] != prim);

        v.synthRoom = enableFakeMics && !v.hasRealRoom;
        v.synthHall = enableFakeMics && !v.hasRealHall;
        v.synthSub  = enableFakeMics && !v.hasRealSub;
    }

    void recalcRamBytesLocked() {
        uint64_t bytes = 0;
        for (const auto& s : slots_) {
            for (int vl = 0; vl < kMaxVelLayers; ++vl) {
                for (const auto& it : s.velLayers[vl].rrs) {
                    for (int m = 0; m < 4; ++m) {
                        if (it.mics[static_cast<size_t>(m)])
                            bytes += it.mics[static_cast<size_t>(m)]->numFrames() * 2 * sizeof(float);
                    }
                }
            }
        }
        totalRamBytes_.store(bytes, std::memory_order_relaxed);
    }

    float sr_ = 44100.0f;
    EngineParams params_;
    std::array<SlotConfig, kTotalSlots> slots_;
    std::array<Voice, kMaxVoices> voices_;
    std::array<std::atomic<int>, 128> notePlaying_{};

    std::atomic<int> activeVoices_{0};
    std::atomic<uint64_t> totalRamBytes_{0};
    std::atomic<bool> idleSleeping_{true};
    int idleSamples_ = 1000000;
    bool wasRoomRun_ = false;
    bool wasHallRun_ = false;

    std::atomic<int> lastVoiceSlot_{-1};
    std::atomic<int> lastVoiceVel_{-1};
    std::atomic<int> lastVoiceRR_{-1};
    std::atomic<uint32_t> voiceSeq_{0};
    uint64_t voiceCounter_ = 0;
    uint32_t rngState_ = 0x9E3779B9u;
    std::mutex stateMutex_;

    // Pre-allocated block-processing scratch buffers
    std::vector<float> scratchCloseL_, scratchCloseR_;
    std::vector<float> scratchRoomRealL_, scratchRoomRealR_, scratchSendRoomL_, scratchSendRoomR_;
    std::vector<float> scratchHallRealL_, scratchHallRealR_, scratchSendHallL_, scratchSendHallR_;
    std::vector<float> scratchSubRealL_, scratchSubRealR_, scratchSendSubL_, scratchSendSubR_;

    StereoSpaceBus roomBus_;
    StereoSpaceBus hallBus_;
    SubBassBus subBus_;
    MasterFxRack masterFx_;
};

} // namespace NoiseMu6ix

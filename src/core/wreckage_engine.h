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
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace NoiseMu6ix {

struct MultiMicSample {
    std::array<std::shared_ptr<AudioBuffer>, 4> mics;
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
    float releaseMs = 1200.0f;

    // Per-Key Fake RR & Fake Velocity (Fake Mic stays Global)
    bool fakeRR  = true;
    bool fakeVel = true;

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

    MultiMicSample sample;
    std::shared_ptr<AudioBuffer> primaryBuf;
    double playPos = 0.0;
    double startFrame = 0.0;
    double endFrame = 0.0;
    double fadeInFrames = 0.0;
    double fadeOutFrames = 0.0;
    double pitchRatio = 1.0;

    float velGain = 1.0f;
    float slotGain = 1.0f;
    float panL = 0.707f, panR = 0.707f;
    float stageDepth = 0.0f;

    float envVal = 0.0f;
    float attackInc = 1.0f;
    float releaseDec = 0.001f;
    double releaseStartFrame = 0.0;
    float chokeFade = 1.0f;
    float chokeDec = 0.004f;

    Biquad formantL, formantR;
    Biquad velLpfL, velLpfR;
    Biquad stageLpfL, stageLpfR;
    Biquad toneLowL, toneLowR, toneHiL, toneHiR;
    bool useFormant = false;
    bool useVelLpf = false;
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
    bool  busBypass[4] = { false, false, false, false };

    bool  globalFakeMic = true;

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
    }

    void resetKit() {
        std::lock_guard<std::mutex> lock(stateMutex_);
        for (auto& v : voices_) v.active = false;
        for (int i = 0; i < kTotalSlots; ++i) {
            for (int vl = 0; vl < kMaxVelLayers; ++vl) {
                slots_[i].velLayers[vl].rrs.clear();
                slots_[i].velLayers[vl].lastRRIdx = -1;
            }
            slots_[i].name = "EMPTY";
            slots_[i].hasCustomColor = false;
            slots_[i].stageX = 0.0f;
            slots_[i].stageY = 0.0f;
            slots_[i].gain = 1.0f;
            slots_[i].pitchSemitones = 0.0f;
            slots_[i].toneLowDb = 0.0f;
            slots_[i].toneHighDb = 0.0f;
            slots_[i].attackMs = 0.0f;
            slots_[i].releaseMs = 1200.0f;
            slots_[i].fakeRR = true;
            slots_[i].fakeVel = true;
            slots_[i].purged = false;
        }
        activeVoices_.store(0, std::memory_order_relaxed);
    }

    void clearSlot(int idx) {
        std::lock_guard<std::mutex> lock(stateMutex_);
        auto& s = slot(idx);
        for (int vl = 0; vl < kMaxVelLayers; ++vl) {
            s.velLayers[vl].rrs.clear();
            s.velLayers[vl].lastRRIdx = -1;
        }
        s.name = "EMPTY";
        s.hasCustomColor = false;
        s.stageX = 0.0f;
        s.stageY = 0.0f;
        s.purged = false;
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
        for (auto& v : voices_) v.active = false;
    }

    EngineParams& params() { return params_; }
    const EngineParams& params() const { return params_; }

    SlotConfig& slot(int idx) { return slots_[std::clamp(idx, 0, kTotalSlots - 1)]; }
    const SlotConfig& slot(int idx) const { return slots_[std::clamp(idx, 0, kTotalSlots - 1)]; }

    int activeVoiceCount() const { return activeVoices_.load(std::memory_order_relaxed); }
    float currentGrDb() const { return masterFx_.currentGrDb(); }
    float currentOutLevel() const { return masterFx_.currentOutLevel(); }

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
                double totalF = static_cast<double>(v.primaryBuf->numFrames());
                if (totalF > 1.0) {
                    double actualFrame = v.reverse ? (totalF - 1.0 - v.playPos) : v.playPos;
                    outPlayheadNorm = std::clamp(static_cast<float>(actualFrame / totalF), 0.0f, 1.0f);
                }
            }
        }
    }

    uint64_t totalSampleRamBytes() const {
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
        return bytes;
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
    }

    // Loads sample respecting Max 4 Velocity Layers and Max 9 Round-Robins per layer
    bool loadSampleIntoSlot(int slotIdx, const std::filesystem::path& filePath, int preferredVelLayer = -1) {
        auto buf = std::make_shared<AudioBuffer>();
        if (!WavLoader::loadFromFile(filePath, *buf)) return false;

        std::string fname = filePath.filename().u8string();
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
        return true;
    }

    void triggerNoteOn(int midiNote, float velocity01) {
        std::lock_guard<std::mutex> lock(stateMutex_);
        velocity01 = std::clamp(velocity01, 0.01f, 1.0f);
        float shapedVel = applyVelocityCurve(velocity01);

        if (midiNote >= 0 && midiNote < 128)
            notePlaying_[midiNote].fetch_add(1, std::memory_order_relaxed);

        for (int sIdx = 0; sIdx < kTotalSlots; ++sIdx) {
            auto& s = slots_[sIdx];
            if (s.midiNote != midiNote || s.purged || !s.hasAnySample()) continue;

            if (s.chokeGroup > 0) {
                for (auto& v : voices_) {
                    if (v.active && v.chokeGroup == s.chokeGroup) {
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

    bool exportKitToNm6(const std::filesystem::path& targetPath, const std::string& libName) {
        std::lock_guard<std::mutex> lock(stateMutex_);
        Nm6Pack pack;
        pack.libraryName = libName;
        std::string json = "{\"format\":3,\"slots\":[";
        for (int i = 0; i < kTotalSlots; ++i) {
            const auto& s = slots_[i];
            if (i > 0) json += ",";
            json += "{\"i\":" + std::to_string(i) +
                    ",\"n\":\"" + s.name + "\"" +
                    ",\"m\":" + std::to_string(s.midiNote) +
                    ",\"c\":" + std::to_string(s.hasCustomColor ? s.keyColor : 0u) +
                    ",\"x\":" + std::to_string(s.stageX) +
                    ",\"y\":" + std::to_string(s.stageY) +
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
                                std::to_string(sid) + "," + std::to_string(it.startNorm) + "," + std::to_string(it.endNorm) + "," +
                                std::to_string(it.fadeInNorm) + "," + std::to_string(it.fadeOutNorm) + "]";
                    }
                }
            }
            json += "]}";
        }
        json += "]}";
        pack.kitMetadataJson = json;
        return pack.saveAtomic(targetPath);
    }

    bool loadKitFromNm6(const std::filesystem::path& packPath) {
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
            for (int vl = 0; vl < kMaxVelLayers; ++vl) {
                slots_[i].velLayers[vl].rrs.clear();
                slots_[i].velLayers[vl].lastRRIdx = -1;
            }
            slots_[i].name = "EMPTY";
            slots_[i].hasCustomColor = false;
            slots_[i].stageX = 0.0f;
            slots_[i].stageY = 0.0f;
        }

        const std::string& js = pack.kitMetadataJson;
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
                auto mPos = chunk.find("\"m\":");
                if (mPos != std::string::npos) s.midiNote = std::atoi(chunk.c_str() + mPos + 4);
                auto cPos = chunk.find("\"c\":");
                if (cPos != std::string::npos) {
                    s.keyColor = static_cast<uint32_t>(std::strtoul(chunk.c_str() + cPos + 4, nullptr, 10));
                    s.hasCustomColor = (s.keyColor != 0);
                }
                auto xPos = chunk.find("\"x\":");
                if (xPos != std::string::npos) s.stageX = std::strtof(chunk.c_str() + xPos + 4, nullptr);
                auto yPos = chunk.find("\"y\":");
                if (yPos != std::string::npos) s.stageY = std::strtof(chunk.c_str() + yPos + 4, nullptr);

                auto sArr = chunk.find("\"s\":[");
                if (sArr != std::string::npos) {
                    size_t p = sArr + 5;
                    while ((p = chunk.find('[', p)) != std::string::npos) {
                        int vl = 0, r = 0, mic = 0, sid = 0;
                        float st = 0.0f, en = 1.0f, fi = 0.0f, fo = 0.0f;
                        if (std::sscanf(chunk.c_str() + p, "[%d,%d,%d,%d,%f,%f,%f,%f]",
                                        &vl, &r, &mic, &sid, &st, &en, &fi, &fo) >= 4) {
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
        return true;
    }

    void process(ProcessBuffers& pb) {
        if (!pb.mainL || !pb.mainR || pb.numSamples <= 0) return;

        bool runClose = !params_.busBypass[0] && params_.busGain[0] > 0.0001f;
        bool runRoom  = !params_.busBypass[1] && params_.busGain[1] > 0.0001f;
        bool runHall  = !params_.busBypass[2] && params_.busGain[2] > 0.0001f;
        bool runSub   = !params_.busBypass[3] && params_.busGain[3] > 0.0001f;

        std::unique_lock<std::mutex> lock(stateMutex_, std::try_to_lock);
        if (!lock.owns_lock() && !pb.offlineRender) {
            std::fill(pb.mainL, pb.mainL + pb.numSamples, 0.0f);
            std::fill(pb.mainR, pb.mainR + pb.numSamples, 0.0f);
            return;
        }
        if (!lock.owns_lock() && pb.offlineRender) lock.lock();

        for (int n = 0; n < pb.numSamples; ++n) {
            float sumCloseL = 0.0f, sumCloseR = 0.0f;
            float sumRoomRealL = 0.0f, sumRoomRealR = 0.0f, sendRoomL = 0.0f, sendRoomR = 0.0f;
            float sumHallRealL = 0.0f, sumHallRealR = 0.0f, sendHallL = 0.0f, sendHallR = 0.0f;
            float sumSubRealL  = 0.0f, sumSubRealR  = 0.0f, sendSubL  = 0.0f, sendSubR  = 0.0f;

            for (auto& v : voices_) {
                if (!v.active || !v.primaryBuf) continue;

                if (v.choking) {
                    v.chokeFade -= v.chokeDec;
                    if (v.chokeFade <= 0.0f) { v.active = false; continue; }
                }

                double regionLen = std::max(8.0, v.endFrame - v.startFrame);
                double relPos = v.playPos - v.startFrame;
                if (relPos < 0.0 || relPos >= regionLen) {
                    v.active = false;
                    continue;
                }

                if (!v.releasing) {
                    v.envVal = std::min(1.0f, v.envVal + v.attackInc);
                    if (v.oneShot && relPos >= v.releaseStartFrame) {
                        v.releasing = true;
                    }
                }
                if (v.releasing) {
                    v.envVal -= v.releaseDec;
                    if (v.envVal <= 0.0001f) { v.active = false; continue; }
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

                float totalGain = v.envVal * waveEnv * v.chokeFade * v.velGain * v.slotGain;

                float cL = 0.0f, cR = 0.0f;
                if (!readHermite(*v.primaryBuf, v.playPos, v.reverse, cL, cR)) {
                    v.active = false;
                    continue;
                }

                if (v.useToneEq) {
                    cL = v.toneHiL.process(v.toneLowL.process(cL));
                    cR = v.toneHiR.process(v.toneLowR.process(cR));
                }
                if (v.useFormant) {
                    cL = v.formantL.process(cL);
                    cR = v.formantR.process(cR);
                }
                if (v.useVelLpf) {
                    cL = v.velLpfL.process(cL);
                    cR = v.velLpfR.process(cR);
                }
                if (v.velDrive > 0.01f) {
                    float d = 1.0f + v.velDrive * 1.5f;
                    cL = fastTanh(cL * d) / d * 1.15f;
                    cR = fastTanh(cR * d) / d * 1.15f;
                }

                float depth = std::clamp(v.stageDepth, 0.0f, 1.0f);
                float stagedL = v.stageLpfL.process(cL) * totalGain * v.panL;
                float stagedR = v.stageLpfR.process(cR) * totalGain * v.panR;
                float closeDryScale = 1.0f - depth * 0.62f;

                sumCloseL += stagedL * closeDryScale;
                sumCloseR += stagedR * closeDryScale;

                float roomDepthSend = 0.35f + depth * 1.15f;
                float hallDepthSend = 0.20f + depth * 1.85f;

                if (v.hasRealRoom) {
                    float rl = 0.0f, rr = 0.0f;
                    readHermite(*v.sample.mics[1], v.playPos, v.reverse, rl, rr);
                    sumRoomRealL += rl * totalGain * v.panL * roomDepthSend;
                    sumRoomRealR += rr * totalGain * v.panR * roomDepthSend;
                } else if (v.synthRoom && runRoom) {
                    sendRoomL += stagedL * roomDepthSend;
                    sendRoomR += stagedR * roomDepthSend;
                }

                if (v.hasRealHall) {
                    float hl = 0.0f, hr = 0.0f;
                    readHermite(*v.sample.mics[2], v.playPos, v.reverse, hl, hr);
                    sumHallRealL += hl * totalGain * v.panL * hallDepthSend;
                    sumHallRealR += hr * totalGain * v.panR * hallDepthSend;
                } else if (v.synthHall && runHall) {
                    sendHallL += stagedL * hallDepthSend;
                    sendHallR += stagedR * hallDepthSend;
                }

                if (v.hasRealSub) {
                    float ll = 0.0f, lr = 0.0f;
                    readHermite(*v.sample.mics[3], v.playPos, v.reverse, ll, lr);
                    float m = 0.5f * (ll + lr) * totalGain;
                    sumSubRealL += m; sumSubRealR += m;
                } else if (v.synthSub && runSub) {
                    sendSubL += stagedL; sendSubR += stagedR;
                }

                v.playPos += v.pitchRatio;
            }

            float busL[4] = { 0, 0, 0, 0 };
            float busR[4] = { 0, 0, 0, 0 };

            if (runClose) { busL[0] = sumCloseL * params_.busGain[0]; busR[0] = sumCloseR * params_.busGain[0]; }
            if (runRoom) {
                float sL = 0.0f, sR = 0.0f;
                roomBus_.process(sendRoomL, sendRoomR, sL, sR);
                busL[1] = (sumRoomRealL + sL) * params_.busGain[1];
                busR[1] = (sumRoomRealR + sR) * params_.busGain[1];
            }
            if (runHall) {
                float sL = 0.0f, sR = 0.0f;
                hallBus_.process(sendHallL, sendHallR, sL, sR);
                busL[2] = (sumHallRealL + sL) * params_.busGain[2];
                busR[2] = (sumHallRealR + sR) * params_.busGain[2];
            }
            if (runSub) {
                float sL = 0.0f, sR = 0.0f;
                subBus_.process(sendSubL, sendSubR, sL, sR);
                busL[3] = (sumSubRealL + sL) * params_.busGain[3];
                busR[3] = (sumSubRealR + sR) * params_.busGain[3];
            }

            float mainL = 0.0f, mainR = 0.0f;
            for (int b = 0; b < 4; ++b) {
                if (pb.auxActive[b] && pb.auxL[b] && pb.auxR[b]) {
                    pb.auxL[b][n] = busL[b]; pb.auxR[b][n] = busR[b];
                } else {
                    mainL += busL[b]; mainR += busR[b];
                }
            }

            masterFx_.process(mainL, mainR, params_.fx);
            pb.mainL[n] = mainL;
            pb.mainR[n] = mainR;
        }

        int cnt = 0;
        for (const auto& v : voices_) if (v.active) ++cnt;
        activeVoices_.store(cnt, std::memory_order_relaxed);
    }

private:
    float applyVelocityCurve(float v) const {
        if (params_.velCurve == VelocityCurve::Soft) return std::pow(v, 0.65f);
        if (params_.velCurve == VelocityCurve::Hard) return std::pow(v, 1.55f);
        return v;
    }

    static bool readHermite(const AudioBuffer& buf, double pos, bool rev, float& outL, float& outR) {
        size_t n = buf.numFrames();
        if (n < 4) return false;
        double idxF = rev ? (static_cast<double>(n - 1) - pos) : pos;
        if (idxF < 0.0 || idxF >= static_cast<double>(n - 1)) return false;

        int i1 = static_cast<int>(idxF);
        float t = static_cast<float>(idxF - static_cast<double>(i1));
        int i0 = std::max(0, i1 - 1);
        int i2 = std::min(static_cast<int>(n - 1), i1 + 1);
        int i3 = std::min(static_cast<int>(n - 1), i1 + 2);

        auto hermite = [&](const std::vector<float>& ch) -> float {
            float y0 = ch[i0], y1 = ch[i1], y2 = ch[i2], y3 = ch[i3];
            float c0 = y1;
            float c1 = 0.5f * (y2 - y0);
            float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
            float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
            return ((c3 * t + c2) * t + c1) * t + c0;
        };
        outL = hermite(buf.left);
        outR = hermite(buf.right);
        return true;
    }

    int selectVelocityLayer(const SlotConfig& s, float shapedVel) const {
        std::vector<int> populated;
        for (int vl = 0; vl < kMaxVelLayers; ++vl) {
            if (!s.velLayers[vl].rrs.empty()) populated.push_back(vl);
        }
        if (populated.empty()) return 0;
        if (populated.size() == 1) return populated[0];

        int bucket = std::clamp(static_cast<int>(shapedVel * static_cast<float>(kMaxVelLayers)), 0, kMaxVelLayers - 1);
        if (!s.velLayers[bucket].rrs.empty()) return bucket;

        // Nearest populated layer fallback
        int bestVl = populated[0];
        int bestDist = std::abs(bestVl - bucket);
        for (int vl : populated) {
            int d = std::abs(vl - bucket);
            if (d < bestDist) { bestDist = d; bestVl = vl; }
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
        if (!prim) return;

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
        v.sample = mm;
        v.primaryBuf = prim;
        v.slotGain = s.gain;
        v.stageDepth = s.stageY;
        v.chokeFade = 1.0f;
        v.age = ++voiceCounter_;

        double totalF = static_cast<double>(prim->numFrames());
        if (s.reverse) {
            v.startFrame = (1.0 - std::clamp(static_cast<double>(mm.endNorm), 0.02, 1.0)) * totalF;
            v.endFrame   = (1.0 - std::clamp(static_cast<double>(mm.startNorm), 0.0, 0.98)) * totalF;
        } else {
            v.startFrame = std::clamp(static_cast<double>(mm.startNorm), 0.0, 0.98) * totalF;
            v.endFrame   = std::max(v.startFrame + 8.0, std::clamp(static_cast<double>(mm.endNorm), 0.02, 1.0) * totalF);
        }
        double regLen = std::max(8.0, v.endFrame - v.startFrame);
        v.fadeInFrames  = std::clamp(static_cast<double>(mm.fadeInNorm), 0.0, 0.5) * regLen;
        v.fadeOutFrames = std::clamp(static_cast<double>(mm.fadeOutNorm), 0.0, 0.5) * regLen;

        float pan01 = (s.stageX + 1.0f) * 0.5f;
        v.panL = std::cos(pan01 * kPi * 0.5f);
        v.panR = std::sin(pan01 * kPi * 0.5f);

        float depthCutoff = 19500.0f - std::clamp(s.stageY, 0.0f, 1.0f) * 12500.0f;
        v.stageLpfL.setLowPass(sr_, depthCutoff); v.stageLpfR.setLowPass(sr_, depthCutoff);
        v.stageLpfL.reset(); v.stageLpfR.reset();

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
        v.pitchRatio = sampleRateRatio * std::pow(2.0, (static_cast<double>(s.pitchSemitones) + centsOffset / 100.0) / 12.0);
        double rrStartOffset = (rrStrength > 0.01f && !s.reverse) ? (kStartMs[step] * rrStrength * 0.001 * prim->sampleRate) : 0.0;
        v.playPos = std::min(v.endFrame - 4.0, v.startFrame + rrStartOffset);

        v.useFormant = (rrStrength > 0.05f);
        if (v.useFormant) {
            v.formantL.setPeaking(sr_, kFormantHz[step], 1.1f, kFormantDb[step] * rrStrength);
            v.formantR.setPeaking(sr_, kFormantHz[step], 1.1f, kFormantDb[step] * rrStrength);
            v.formantL.reset(); v.formantR.reset();
        }

        v.velGain = std::pow(vel, 1.35f);

        float extraAttackMs = (velStrength > 0.01f) ? ((1.0f - vel) * 8.5f * velStrength) : 0.0f;
        float totalAttackMs = std::max(0.2f, s.attackMs + extraAttackMs);
        double attSamples = sr_ * totalAttackMs * 0.001;
        v.attackInc = (attSamples <= 1.0) ? 1.0f : static_cast<float>(1.0 / attSamples);

        double relSamples = std::max(8.0, static_cast<double>(sr_) * std::max(10.0f, s.releaseMs) * 0.001);
        v.releaseDec = static_cast<float>(1.0 / relSamples);
        v.releaseStartFrame = std::max(attSamples * 0.5, relSamples * 0.25);
        if (s.releaseMs >= 3800.0f) {
            v.releaseStartFrame = regLen + 1000.0;
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

        v.hasRealRoom = mm.hasMic(MicBus::Room);
        v.hasRealHall = mm.hasMic(MicBus::Hall);
        v.hasRealSub  = mm.hasMic(MicBus::Sub);

        v.synthRoom = enableFakeMics && !v.hasRealRoom;
        v.synthHall = enableFakeMics && !v.hasRealHall;
        v.synthSub  = enableFakeMics && !v.hasRealSub;
    }

    float sr_ = 44100.0f;
    EngineParams params_;
    std::array<SlotConfig, kTotalSlots> slots_;
    std::array<Voice, kMaxVoices> voices_;
    std::array<std::atomic<int>, 128> notePlaying_{};
    std::atomic<int> activeVoices_{0};
    uint64_t voiceCounter_ = 0;
    uint32_t rngState_ = 0x9E3779B9u;
    std::mutex stateMutex_;

    StereoSpaceBus roomBus_;
    StereoSpaceBus hallBus_;
    SubBassBus subBus_;
    MasterFxRack masterFx_;
};

} // namespace NoiseMu6ix

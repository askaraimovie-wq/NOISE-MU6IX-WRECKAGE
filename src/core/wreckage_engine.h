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

// A single multi-mic sample group (Close, Room, Hall, LFE, Crush) for one (Velocity, RR) entry
struct MultiMicSample {
    std::array<std::shared_ptr<AudioBuffer>, 5> mics;
    bool hasMic(MicBus m) const {
        auto idx = static_cast<size_t>(m);
        return idx < 5 && mics[idx] && !mics[idx]->empty();
    }
    int realMicCount() const {
        int c = 0;
        for (int i = 0; i < 5; ++i) if (mics[i] && !mics[i]->empty()) ++c;
        return c;
    }
};

// Dynamic Unlimited Velocity Layer containing unlimited Round-Robins
struct VelocityLayer {
    std::vector<MultiMicSample> roundRobins;
};

struct SlotConfig {
    std::string name = "EMPTY";
    int midiNote = kBaseMidiNote;   // Remappable + supports Pad Layering
    uint32_t keyColor = 0;          // 0 = uncolored, otherwise 0xRRGGBB
    bool hasCustomColor = false;

    // Sample Inspector Horizontal Faders
    float gain = 0.85f;             // 0..1.5
    float pitchSemitones = 0.0f;    // -24..+24
    float attackMs = 0.0f;          // 0..200 ms
    float releaseMs = 600.0f;       // 20..4000 ms
    float xfade = 0.0f;             // 0..1

    // Drum Modes
    bool oneShot = true;            // true = One-Shot, false = Gate
    int chokeGroup = 0;             // 0 = Off, 1..4
    bool reverse = false;           // Zero-CPU Reverse playback
    bool purged = false;

    // THE STAGE Spatial Position
    float stageX = 0.0f;            // -1.0 (Left) .. +1.0 (Right)
    float stageY = 0.25f;           // 0.0 (Front/Dry) .. 1.0 (Back/Far Hall)

    // 3-State Smart Hybrid Engine (AUTO / ON / OFF)
    SmartMode rrMode  = SmartMode::Auto;
    SmartMode velMode = SmartMode::Auto;
    SmartMode micMode = SmartMode::Auto;

    // Unlimited Dynamic Velocity Layers x Unlimited Round-Robins
    std::vector<VelocityLayer> layers;
    int lastRRIdx = -1;
    int fakeRRStep = 0;

    bool hasAnySample() const {
        for (const auto& vl : layers)
            for (const auto& rr : vl.roundRobins)
                if (rr.realMicCount() > 0) return true;
        return false;
    }

    int velocityCount() const { return static_cast<int>(layers.size()); }
    int maxRRCount() const {
        int m = 0;
        for (const auto& vl : layers) m = std::max(m, static_cast<int>(vl.roundRobins.size()));
        return m;
    }
};

struct Voice {
    bool active = false;
    bool releasing = false;
    bool choking = false;
    int slotIndex = 0;
    int midiNote = 0;
    int chokeGroup = 0;
    bool reverse = false;
    bool oneShot = true;

    MultiMicSample sample;
    double playPos = 0.0;
    double pitchRatio = 1.0;
    float velGain = 1.0f;
    float slotGain = 1.0f;
    float panL = 0.707f, panR = 0.707f;
    float stageDepth = 0.25f;

    // Envelope & Anti-Click Fade
    float envVal = 0.0f;
    float attackInc = 1.0f;
    float releaseDec = 0.001f;
    float chokeFade = 1.0f;
    float chokeDec = 0.004f;

    // Smart Hybrid Per-Voice DSP (Fake RR Formant EQ + Fake Vel LPF + Skin-Tension Pitch Drop)
    Biquad formantL, formantR;
    Biquad velLpfL, velLpfR;
    Biquad stageLpfL, stageLpfR;
    bool useFormant = false;
    bool useVelLpf = false;
    float velDrive = 0.0f;
    float pitchDropCents = 0.0f;
    float pitchDropDecay = 0.997f;

    // Smart Hybrid Mic Flags (true = synthesize this mic from Close)
    bool synthRoom = false;
    bool synthHall = false;
    bool synthLfe  = false;
    bool synthCrush = false;
    bool hasRealClose = false;
    bool hasRealRoom  = false;
    bool hasRealHall  = false;
    bool hasRealLfe   = false;
    bool hasRealCrush = false;

    uint64_t age = 0;
};

struct EngineParams {
    float busGain[5]   = { 0.85f, 0.55f, 0.45f, 0.60f, 0.35f };
    bool  busBypass[5] = { false, false, false, false, false };

    float masterHpfHz = 25.0f;
    float masterLpfHz = 19500.0f;
    float eqLowDb = 0.0f;
    float eqMidDb = 0.0f;
    float eqHighDb = 0.0f;
    float compAmount = 0.20f;
    float transientPunch = 0.55f;
    float stereoWidth = 0.50f;
    bool  monoBass = true;
    VelocityCurve velCurve = VelocityCurve::Linear;
    bool  limiterOn = true;
    float masterOutGain = 0.92f;

    float punishAmount = 0.30f;
    PunishMode punishMode = PunishMode::HurtMePlenty;

    float stageMasterDepth = 1.0f;
    float smartIntensity = 0.80f;
    int   themeIndex = 0;
    int   zoomIndex = 2; // 0=50%, 1=75%, 2=100%
};

// Audio output block pointers (Main + 5 optional Aux Multi-Outs)
struct ProcessBuffers {
    float* mainL = nullptr;
    float* mainR = nullptr;
    float* auxL[5] = { nullptr, nullptr, nullptr, nullptr, nullptr };
    float* auxR[5] = { nullptr, nullptr, nullptr, nullptr, nullptr };
    bool   auxActive[5] = { false, false, false, false, false };
    int    numSamples = 0;
    bool   offlineRender = false;
};

class WreckageEngine {
public:
    WreckageEngine() {
        for (int i = 0; i < kTotalSlots; ++i) {
            slots_[i].midiNote = kBaseMidiNote + i;
            slots_[i].name = "SLOT " + std::to_string(i + 1);
            // Arrange default stage positions across a semi-circle
            float angle = -0.75f + 1.5f * (static_cast<float>(i % 12) / 11.0f);
            slots_[i].stageX = std::clamp(angle, -0.85f, 0.85f);
            slots_[i].stageY = 0.18f + 0.25f * static_cast<float>(i / 12);
        }
        installBuiltInTestKit();
    }

    void prepare(double sampleRate) {
        sr_ = static_cast<float>(std::max(8000.0, sampleRate));
        roomBus_.prepare(sr_, 8.0f, 0.55f, 120.0f, 8500.0f);
        hallBus_.prepare(sr_, 28.0f, 2.55f, 150.0f, 5500.0f);
        lfeBus_.prepare(sr_);
        crushBus_.prepare(sr_);
        masterFx_.prepare(sr_);
        for (auto& v : voices_) v.active = false;
    }

    EngineParams& params() { return params_; }
    const EngineParams& params() const { return params_; }

    SlotConfig& slot(int idx) { return slots_[std::clamp(idx, 0, kTotalSlots - 1)]; }
    const SlotConfig& slot(int idx) const { return slots_[std::clamp(idx, 0, kTotalSlots - 1)]; }

    // Safe multi-file / single-file loader with Regex Auto-Parse
    bool loadSampleIntoSlot(int slotIdx, const std::filesystem::path& filePath,
                            int explicitVel = -1, int explicitRR = -1, int explicitMic = -1) {
        auto buf = std::make_shared<AudioBuffer>();
        if (!WavLoader::loadFromFile(filePath, *buf)) return false;

        std::string fname = filePath.filename().u8string();
        auto meta = SampleAutoParser::parseFilename(fname);
        int vIdx = (explicitVel >= 0) ? explicitVel : meta.velocityIndex;
        int rIdx = (explicitRR  >= 0) ? explicitRR  : meta.rrIndex;
        MicBus mic = (explicitMic >= 0 && explicitMic < 5) ? static_cast<MicBus>(explicitMic) : meta.mic;

        std::lock_guard<std::mutex> lock(stateMutex_);
        auto& s = slot(slotIdx);
        if (static_cast<int>(s.layers.size()) <= vIdx) s.layers.resize(static_cast<size_t>(vIdx + 1));
        auto& vl = s.layers[static_cast<size_t>(vIdx)];
        if (static_cast<int>(vl.roundRobins.size()) <= rIdx) vl.roundRobins.resize(static_cast<size_t>(rIdx + 1));
        vl.roundRobins[static_cast<size_t>(rIdx)].mics[static_cast<size_t>(mic)] = buf;
        if (s.name.rfind("SLOT ", 0) == 0 || s.name == "EMPTY") {
            s.name = meta.cleanInstrumentName;
        }
        s.purged = false;
        return true;
    }

    void triggerNoteOn(int midiNote, float velocity01) {
        std::lock_guard<std::mutex> lock(stateMutex_);
        velocity01 = std::clamp(velocity01, 0.01f, 1.0f);
        float shapedVel = applyVelocityCurve(velocity01);

        // Support Pad Layering: trigger ALL slots mapped to this MIDI note!
        for (int sIdx = 0; sIdx < kTotalSlots; ++sIdx) {
            auto& s = slots_[sIdx];
            if (s.midiNote != midiNote || s.purged || !s.hasAnySample()) continue;

            // Choke Group handling (5ms anti-click fade)
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
        if (midiNote >= 0 && midiNote < 128) {
            activeKeyFlash_[midiNote].store(1.0f, std::memory_order_relaxed);
        }
    }

    void triggerNoteOff(int midiNote) {
        std::lock_guard<std::mutex> lock(stateMutex_);
        for (auto& v : voices_) {
            if (v.active && !v.oneShot && v.midiNote == midiNote) {
                v.releasing = true;
            }
        }
    }

    float getKeyFlash(int midiNote) const {
        if (midiNote < 0 || midiNote >= 128) return 0.0f;
        return activeKeyFlash_[midiNote].load(std::memory_order_relaxed);
    }

    void decayKeyFlashes() {
        for (int i = 0; i < 128; ++i) {
            float v = activeKeyFlash_[i].load(std::memory_order_relaxed);
            if (v > 0.001f) activeKeyFlash_[i].store(v * 0.84f, std::memory_order_relaxed);
        }
    }

    // Export current loaded kit to a deduplicated libFLAC .nm6 pack
    bool exportKitToNm6(const std::filesystem::path& targetPath, const std::string& libName) {
        std::lock_guard<std::mutex> lock(stateMutex_);
        Nm6Pack pack;
        pack.libraryName = libName;
        std::string json = "{\"format\":1,\"slots\":[";
        for (int i = 0; i < kTotalSlots; ++i) {
            const auto& s = slots_[i];
            if (i > 0) json += ",";
            json += "{\"i\":" + std::to_string(i) +
                    ",\"n\":\"" + s.name + "\"" +
                    ",\"m\":" + std::to_string(s.midiNote) +
                    ",\"c\":" + std::to_string(s.hasCustomColor ? s.keyColor : 0u) +
                    ",\"x\":" + std::to_string(s.stageX) +
                    ",\"y\":" + std::to_string(s.stageY) +
                    ",\"os\":" + std::to_string(s.oneShot ? 1 : 0) +
                    ",\"cg\":" + std::to_string(s.chokeGroup) +
                    ",\"rv\":" + std::to_string(s.reverse ? 1 : 0) +
                    ",\"s\":[";
            bool firstSamp = true;
            for (size_t v = 0; v < s.layers.size(); ++v) {
                for (size_t r = 0; r < s.layers[v].roundRobins.size(); ++r) {
                    for (int mic = 0; mic < 5; ++mic) {
                        const auto& ptr = s.layers[v].roundRobins[r].mics[static_cast<size_t>(mic)];
                        if (!ptr || ptr->empty()) continue;
                        uint32_t sid = pack.addOrFindSample(*ptr);
                        if (!firstSamp) json += ",";
                        firstSamp = false;
                        json += "[" + std::to_string(v) + "," + std::to_string(r) + "," +
                                std::to_string(mic) + "," + std::to_string(sid) + "]";
                    }
                }
            }
            json += "]}";
        }
        json += "]}";
        pack.kitMetadataJson = json;
        return pack.saveAtomic(targetPath);
    }

    // Main real-time & offline audio processing callback
    void process(ProcessBuffers& pb) {
        if (!pb.mainL || !pb.mainR || pb.numSamples <= 0) return;

        masterFx_.updateFilters(params_.masterHpfHz, params_.masterLpfHz,
                                params_.eqLowDb, params_.eqMidDb, params_.eqHighDb);

        bool runRoom  = !params_.busBypass[1] && params_.busGain[1] > 0.001f;
        bool runHall  = !params_.busBypass[2] && params_.busGain[2] > 0.001f;
        bool runLfe   = !params_.busBypass[3] && params_.busGain[3] > 0.001f;
        bool runCrush = !params_.busBypass[4] && params_.busGain[4] > 0.001f;
        bool runClose = !params_.busBypass[0] && params_.busGain[0] > 0.001f;

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
            float sumLfeRealL  = 0.0f, sumLfeRealR  = 0.0f, sendLfeL  = 0.0f, sendLfeR  = 0.0f;
            float sumCrushRealL= 0.0f, sumCrushRealR= 0.0f, sendCrushL= 0.0f, sendCrushR= 0.0f;

            for (auto& v : voices_) {
                if (!v.active) continue;

                // Envelope & Choke
                if (v.choking) {
                    v.chokeFade -= v.chokeDec;
                    if (v.chokeFade <= 0.0f) { v.active = false; continue; }
                }
                if (!v.releasing) {
                    v.envVal = std::min(1.0f, v.envVal + v.attackInc);
                } else {
                    v.envVal -= v.releaseDec;
                    if (v.envVal <= 0.0f) { v.active = false; continue; }
                }

                float totalGain = v.envVal * v.chokeFade * v.velGain * v.slotGain;

                // Read Close (or primary fallback) with Cubic Hermite Resampler
                float cL = 0.0f, cR = 0.0f;
                bool done = false;
                if (v.hasRealClose) {
                    done = !readHermite(*v.sample.mics[0], v.playPos, v.reverse, cL, cR);
                } else {
                    done = true;
                }
                if (done) { v.active = false; continue; }

                // Apply Smart Fake RR Formant EQ & Smart Fake Velocity Tone + Drive
                if (v.useFormant) {
                    cL = v.formantL.process(cL);
                    cR = v.formantR.process(cR);
                }
                if (v.useVelLpf) {
                    cL = v.velLpfL.process(cL);
                    cR = v.velLpfR.process(cR);
                }
                if (v.velDrive > 0.01f) {
                    float d = 1.0f + v.velDrive * 1.6f;
                    cL = fastTanh(cL * d) / d * 1.15f;
                    cR = fastTanh(cR * d) / d * 1.15f;
                }

                // Stage Distance HF Roll-Off + Panning
                float depth = std::clamp(v.stageDepth * params_.stageMasterDepth, 0.0f, 1.0f);
                float stagedL = v.stageLpfL.process(cL) * totalGain * v.panL;
                float stagedR = v.stageLpfR.process(cR) * totalGain * v.panR;
                float closeDryScale = 1.0f - depth * 0.42f;

                sumCloseL += stagedL * closeDryScale;
                sumCloseR += stagedR * closeDryScale;

                // Hybrid Mic Routing (Real Mics stay untouched; Missing Mics go to Shared Synth Bus)
                float roomDepthSend = 0.45f + depth * 0.85f;
                float hallDepthSend = 0.25f + depth * 1.15f;

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

                if (v.hasRealLfe) {
                    float ll = 0.0f, lr = 0.0f;
                    readHermite(*v.sample.mics[3], v.playPos, v.reverse, ll, lr);
                    float m = 0.5f * (ll + lr) * totalGain;
                    sumLfeRealL += m; sumLfeRealR += m;
                } else if (v.synthLfe && runLfe) {
                    sendLfeL += stagedL; sendLfeR += stagedR;
                }

                if (v.hasRealCrush) {
                    float kl = 0.0f, kr = 0.0f;
                    readHermite(*v.sample.mics[4], v.playPos, v.reverse, kl, kr);
                    sumCrushRealL += kl * totalGain * v.panL;
                    sumCrushRealR += kr * totalGain * v.panR;
                } else if (v.synthCrush && runCrush) {
                    sendCrushL += stagedL; sendCrushR += stagedR;
                }

                // Advance playhead (with Skin-Tension Pitch Drop on hard hits)
                double curStep = v.pitchRatio;
                if (v.pitchDropCents > 0.05f) {
                    curStep *= std::pow(2.0, static_cast<double>(v.pitchDropCents) / 1200.0);
                    v.pitchDropCents *= v.pitchDropDecay;
                }
                v.playPos += curStep;
            }

            // Shared Zero-CPU Bus DSP Processing
            float busL[5] = { 0, 0, 0, 0, 0 };
            float busR[5] = { 0, 0, 0, 0, 0 };

            if (runClose) {
                busL[0] = sumCloseL * params_.busGain[0];
                busR[0] = sumCloseR * params_.busGain[0];
            }
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
            if (runLfe) {
                float sL = 0.0f, sR = 0.0f;
                lfeBus_.process(sendLfeL, sendLfeR, sL, sR);
                busL[3] = (sumLfeRealL + sL) * params_.busGain[3];
                busR[3] = (sumLfeRealR + sR) * params_.busGain[3];
            }
            if (runCrush) {
                float sL = 0.0f, sR = 0.0f;
                crushBus_.process(sendCrushL, sendCrushR, sL, sR);
                busL[4] = (sumCrushRealL + sL) * params_.busGain[4];
                busR[4] = (sumCrushRealR + sR) * params_.busGain[4];
            }

            // Multi-Out Auto-Routing (Active Aux Buses decouple from Main to avoid +6 dB doubling)
            float mainL = 0.0f, mainR = 0.0f;
            for (int b = 0; b < 5; ++b) {
                if (pb.auxActive[b] && pb.auxL[b] && pb.auxR[b]) {
                    pb.auxL[b][n] = busL[b];
                    pb.auxR[b][n] = busR[b];
                } else {
                    mainL += busL[b];
                    mainR += busR[b];
                }
            }

            // Master FX + 3-Mode PUNISH + Limiter on Main Out
            masterFx_.process(mainL, mainR,
                              params_.compAmount, params_.transientPunch, params_.stereoWidth,
                              params_.monoBass, params_.punishAmount, params_.punishMode,
                              params_.limiterOn, params_.masterOutGain);

            pb.mainL[n] = mainL;
            pb.mainR[n] = mainR;
        }
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

    void spawnVoice(int slotIdx, int midiNote, float vel) {
        auto& s = slots_[slotIdx];
        // Pick free voice or steal oldest
        Voice* target = nullptr;
        uint64_t oldestAge = UINT64_MAX;
        for (auto& v : voices_) {
            if (!v.active) { target = &v; break; }
            if (v.age < oldestAge) { oldestAge = v.age; target = &v; }
        }
        if (!target) return;

        // 1. Velocity Layer Selection (Unlimited dynamic split)
        int numVels = static_cast<int>(s.layers.size());
        if (numVels == 0) return;
        int velLayerIdx = std::clamp(static_cast<int>(vel * numVels), 0, numVels - 1);
        auto& vl = s.layers[static_cast<size_t>(velLayerIdx)];
        if (vl.roundRobins.empty()) return;

        // 2. Anti-Predictable Non-Repeating Round-Robin Selection
        int numRRs = static_cast<int>(vl.roundRobins.size());
        int rrIdx = 0;
        if (numRRs == 1) {
            rrIdx = 0;
        } else if (numRRs == 2) {
            rrIdx = (s.lastRRIdx == 0) ? 1 : 0;
        } else {
            rngState_ = rngState_ * 1664525u + 1013904223u;
            rrIdx = static_cast<int>((rngState_ >> 16) % static_cast<uint32_t>(numRRs));
            if (rrIdx == s.lastRRIdx) rrIdx = (rrIdx + 1) % numRRs;
        }
        s.lastRRIdx = rrIdx;
        const auto& mm = vl.roundRobins[static_cast<size_t>(rrIdx)];
        if (!mm.hasMic(MicBus::Close)) return;

        // 3. Smart Hybrid Boost Strength Computation (1-hit=1.0, 2-4=0.50 Hybrid, 5+=0.0 Pure Real)
        float rrStrength = 0.0f;
        if (s.rrMode == SmartMode::ForceOn) rrStrength = 1.0f;
        else if (s.rrMode == SmartMode::Auto) {
            if (numRRs == 1) rrStrength = 1.0f;
            else if (numRRs <= 4) rrStrength = 0.50f; // HYBRID BOOST eliminates 2-4 RR machine-gun
            else rrStrength = 0.0f;
        }
        rrStrength *= params_.smartIntensity;

        float velStrength = 0.0f;
        if (s.velMode == SmartMode::ForceOn) velStrength = 1.0f;
        else if (s.velMode == SmartMode::Auto) {
            if (numVels == 1) velStrength = 1.0f;
            else if (numVels <= 4) velStrength = 0.45f; // Hybrid dynamic smoothing
            else velStrength = 0.0f;
        }
        velStrength *= params_.smartIntensity;

        bool enableFakeMics = (s.micMode == SmartMode::ForceOn) || (s.micMode == SmartMode::Auto);

        Voice& v = *target;
        v.active = true;
        v.releasing = false;
        v.choking = false;
        v.slotIndex = slotIdx;
        v.midiNote = midiNote;
        v.chokeGroup = s.chokeGroup;
        v.reverse = s.reverse;
        v.oneShot = s.oneShot;
        v.sample = mm;
        v.slotGain = s.gain;
        v.stageDepth = s.stageY;
        v.chokeFade = 1.0f;
        v.age = ++voiceCounter_;

        // Constant-Power Stereo Panning from Stage X
        float pan01 = (s.stageX + 1.0f) * 0.5f;
        v.panL = std::cos(pan01 * kPi * 0.5f);
        v.panR = std::sin(pan01 * kPi * 0.5f);

        // Stage Distance HF Air Roll-Off
        float depthCutoff = 19500.0f - std::clamp(s.stageY * params_.stageMasterDepth, 0.0f, 1.0f) * 12500.0f;
        v.stageLpfL.setLowPass(sr_, depthCutoff); v.stageLpfR.setLowPass(sr_, depthCutoff);
        v.stageLpfL.reset(); v.stageLpfR.reset();

        // Pro Fake RR (4-Step Shuffle: Start Shift + Formant EQ + Pitch-Safe Drift)
        static constexpr float kStartMs[4]    = { 0.0f, 1.2f, 2.4f, 0.6f };
        static constexpr float kFormantHz[4]  = { 340.0f, 520.0f, 680.0f, 440.0f };
        static constexpr float kFormantDb[4]  = { 1.4f, -1.3f, 1.1f, -0.9f };
        static constexpr float kPitchCents[4] = { 0.0f, 9.0f, -8.5f, 4.5f };
        int step = s.fakeRRStep;
        s.fakeRRStep = (s.fakeRRStep + 1) & 3;

        double sampleRateRatio = static_cast<double>(mm.mics[0]->sampleRate) / static_cast<double>(sr_);
        double centsOffset = (rrStrength > 0.01f) ? (kPitchCents[step] * rrStrength) : 0.0;
        v.pitchRatio = sampleRateRatio * std::pow(2.0, (static_cast<double>(s.pitchSemitones) + centsOffset / 100.0) / 12.0);
        v.playPos = (rrStrength > 0.01f && !s.reverse) ? (kStartMs[step] * rrStrength * 0.001 * mm.mics[0]->sampleRate) : 0.0;

        v.useFormant = (rrStrength > 0.05f);
        if (v.useFormant) {
            v.formantL.setPeaking(sr_, kFormantHz[step], 1.2f, kFormantDb[step] * rrStrength);
            v.formantR.setPeaking(sr_, kFormantHz[step], 1.2f, kFormantDb[step] * rrStrength);
            v.formantL.reset(); v.formantR.reset();
        }

        // Pro Fake Velocity (Log Volume + Warm LPF + Soft Attack + Skin-Tension Pitch Drop + Drive)
        v.velGain = std::pow(vel, 1.35f);
        float extraAttackMs = (velStrength > 0.01f) ? ((1.0f - vel) * 8.5f * velStrength) : 0.0f;
        float totalAttackMs = std::max(0.5f, s.attackMs + extraAttackMs);
        v.attackInc = 1.0f / std::max(1.0f, sr_ * totalAttackMs * 0.001f);
        v.releaseDec = 1.0f / std::max(1.0f, sr_ * std::max(15.0f, s.releaseMs) * 0.001f);
        v.envVal = 0.0f;

        v.useVelLpf = (velStrength > 0.05f && vel < 0.92f);
        if (v.useVelLpf) {
            float cutoff = 2200.0f + std::pow(vel, 1.8f) * 17500.0f;
            float blendedCutoff = 19500.0f * (1.0f - velStrength) + cutoff * velStrength;
            v.velLpfL.setLowPass(sr_, blendedCutoff); v.velLpfR.setLowPass(sr_, blendedCutoff);
            v.velLpfL.reset(); v.velLpfR.reset();
        }
        v.velDrive = (vel > 0.72f) ? ((vel - 0.72f) * 3.0f * velStrength) : 0.0f;
        v.pitchDropCents = (vel > 0.75f) ? (24.0f * (vel - 0.75f) * 4.0f * velStrength) : 0.0f;
        v.pitchDropDecay = std::exp(-1.0f / (sr_ * 0.025f));

        // Smart Hybrid Mic Flags (Preserve Real Mics, Synthesize ONLY Missing Mics)
        v.hasRealClose = mm.hasMic(MicBus::Close);
        v.hasRealRoom  = mm.hasMic(MicBus::Room);
        v.hasRealHall  = mm.hasMic(MicBus::Hall);
        v.hasRealLfe   = mm.hasMic(MicBus::LFE);
        v.hasRealCrush = mm.hasMic(MicBus::Crush);

        v.synthRoom  = enableFakeMics && !v.hasRealRoom;
        v.synthHall  = enableFakeMics && !v.hasRealHall;
        v.synthLfe   = enableFakeMics && !v.hasRealLfe;
        v.synthCrush = enableFakeMics && !v.hasRealCrush;
    }

    // Built-in 1-Click Test Percussion Kit so the plugin sounds cinematic out of the box!
    void installBuiltInTestKit() {
        auto makeDrum = [](float baseHz, float pitchDrop, float decaySec, float noiseAmt) {
            auto b = std::make_shared<AudioBuffer>();
            b->sampleRate = 44100;
            size_t n = static_cast<size_t>(44100.0f * decaySec);
            b->left.resize(n); b->right.resize(n);
            double phase = 0.0;
            uint32_t rng = 1234567u;
            for (size_t i = 0; i < n; ++i) {
                float t = static_cast<float>(i) / 44100.0f;
                float env = std::exp(-t / (decaySec * 0.24f));
                float freq = baseHz + pitchDrop * std::exp(-t * 38.0f);
                phase += 2.0 * kPi * freq / 44100.0;
                rng = rng * 1664525u + 1013904223u;
                float nSample = (static_cast<float>(static_cast<int32_t>(rng)) / 2147483648.0f) * noiseAmt * std::exp(-t * 24.0f);
                float sig = fastTanh((static_cast<float>(std::sin(phase)) * env + nSample) * 1.4f);
                b->left[i] = sig * 0.85f;
                b->right[i] = sig * 0.85f;
            }
            return b;
        };

        struct PresetHit { int idx; const char* name; uint32_t color; float hz; float drop; float dec; float noise; int choke; };
        const PresetHit hits[] = {
            { 0, "TAIKO KICK",  kStudioKeyColors[0], 54.0f,  110.0f, 0.85f, 0.15f, 0 },
            { 1, "SURDO LOW",   kStudioKeyColors[1], 72.0f,   85.0f, 0.70f, 0.18f, 0 },
            { 2, "DHOL HIT",    kStudioKeyColors[3], 108.0f,  95.0f, 0.55f, 0.28f, 0 },
            { 3, "THAVIL SNAP", kStudioKeyColors[3], 165.0f, 140.0f, 0.42f, 0.45f, 0 },
            { 4, "EPIC SNARE",  kStudioKeyColors[5], 195.0f,  90.0f, 0.48f, 0.85f, 0 },
            { 5, "STUDIO CLAP", kStudioKeyColors[5], 420.0f,  60.0f, 0.35f, 0.95f, 0 },
            { 6, "HI-HAT CL",   kStudioKeyColors[7], 900.0f,  20.0f, 0.12f, 1.00f, 1 },
            { 7, "HI-HAT OP",   kStudioKeyColors[7], 820.0f,  20.0f, 0.55f, 1.00f, 1 }
        };
        for (const auto& h : hits) {
            auto& s = slots_[h.idx];
            s.name = h.name;
            s.keyColor = h.color;
            s.hasCustomColor = true;
            s.chokeGroup = h.choke;
            s.layers.resize(1);
            s.layers[0].roundRobins.resize(1);
            s.layers[0].roundRobins[0].mics[0] = makeDrum(h.hz, h.drop, h.dec, h.noise);
        }
    }

    float sr_ = 44100.0f;
    EngineParams params_;
    std::array<SlotConfig, kTotalSlots> slots_;
    std::array<Voice, kMaxVoices> voices_;
    std::array<std::atomic<float>, 128> activeKeyFlash_{};
    uint64_t voiceCounter_ = 0;
    uint32_t rngState_ = 0x9E3779B9u;
    std::mutex stateMutex_;

    StereoSpaceBus roomBus_;
    StereoSpaceBus hallBus_;
    LfeSubBus lfeBus_;
    CrushBus crushBus_;
    MasterFxRack masterFx_;
};

} // namespace NoiseMu6ix

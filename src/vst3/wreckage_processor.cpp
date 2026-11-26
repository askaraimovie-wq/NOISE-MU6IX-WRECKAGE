#include "wreckage_processor.h"
#include "wreckage_uids.h"
#include "base/source/fstreamer.h"
#include "public.sdk/source/vst/vstbus.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "pluginterfaces/base/ustring.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace NoiseMu6ix {

static std::filesystem::path pathFromUtf8(const std::string& u8) {
#if defined(__cpp_char8_t)
    return std::filesystem::path(reinterpret_cast<const char8_t*>(u8.c_str()));
#else
    return std::filesystem::u8path(u8);
#endif
}

WreckageProcessor::WreckageProcessor() {
    setControllerClass(kWreckageControllerUID);
}

tresult PLUGIN_API WreckageProcessor::initialize(FUnknown* context) {
    tresult res = AudioEffect::initialize(context);
    if (res != kResultOk) return res;

    addAudioOutput(STR16("Main Out"),  SpeakerArr::kStereo, BusTypes::kMain);
    addAudioOutput(STR16("Close Out"), SpeakerArr::kStereo, BusTypes::kAux, 0);
    addAudioOutput(STR16("Room Out"),  SpeakerArr::kStereo, BusTypes::kAux, 0);
    addAudioOutput(STR16("Hall Out"),  SpeakerArr::kStereo, BusTypes::kAux, 0);
    addAudioOutput(STR16("Sub Out"),   SpeakerArr::kStereo, BusTypes::kAux, 0);

    addEventInput(STR16("MIDI In"), 1);
    engine_.prepare(44100.0);
    return kResultOk;
}

tresult PLUGIN_API WreckageProcessor::terminate() { return AudioEffect::terminate(); }
tresult PLUGIN_API WreckageProcessor::setActive(TBool state) { return AudioEffect::setActive(state); }

tresult PLUGIN_API WreckageProcessor::setupProcessing(ProcessSetup& newSetup) {
    sampleRate_ = std::max(8000.0, newSetup.sampleRate);
    engine_.prepare(sampleRate_);
    return AudioEffect::setupProcessing(newSetup);
}

tresult PLUGIN_API WreckageProcessor::canProcessSampleSize(int32 symbolicSampleSize) {
    return (symbolicSampleSize == kSample32) ? kResultTrue : kResultFalse;
}

tresult PLUGIN_API WreckageProcessor::setBusArrangements(SpeakerArrangement* inputs, int32 numIns,
                                                         SpeakerArrangement* outputs, int32 numOuts) {
    if (numIns == 0 && numOuts >= 1 && outputs[0] == SpeakerArr::kStereo)
        return AudioEffect::setBusArrangements(inputs, numIns, outputs, numOuts);
    return kResultFalse;
}

void WreckageProcessor::applyNormalizedParam(ParamID id, ParamValue val) {
    auto& p = engine_.params();
    auto& fx = p.fx;
    float f = static_cast<float>(std::clamp(val, 0.0, 1.0));
    switch (id) {
        case kParamBusCloseGain:   p.busGain[0] = gainDbToLinear(gainNormToDb(f)); break;
        case kParamBusRoomGain:    p.busGain[1] = gainDbToLinear(gainNormToDb(f)); break;
        case kParamBusHallGain:    p.busGain[2] = gainDbToLinear(gainNormToDb(f)); break;
        case kParamBusSubGain:     p.busGain[3] = gainDbToLinear(gainNormToDb(f)); break;
        case kParamBusCloseBypass: p.busBypass[0] = (f >= 0.5f); break;
        case kParamBusRoomBypass:  p.busBypass[1] = (f >= 0.5f); break;
        case kParamBusHallBypass:  p.busBypass[2] = (f >= 0.5f); break;
        case kParamBusSubBypass:   p.busBypass[3] = (f >= 0.5f); break;

        case kParamFilterOn:     fx.filterOn = (f >= 0.5f); break;
        case kParamFilterType:   fx.filterType = std::clamp(static_cast<int>(std::round(f * 7.0f)), 0, 7); break;
        case kParamFilterCutoff: fx.filterCutoffHz = 20.0f * std::pow(1000.0f, f); break;
        case kParamFilterRes:    fx.filterRes = f; break;

        case kParamEqOn:       fx.eqOn = (f >= 0.5f); break;
        case kParamEqLowFreq:  fx.eqLowFreq  = 30.0f + f * 270.0f; break;
        case kParamEqLowGain:  fx.eqLowGain  = (f - 0.5f) * 24.0f; break;
        case kParamEqMidFreq:  fx.eqMidFreq  = 250.0f + f * 3750.0f; break;
        case kParamEqMidGain:  fx.eqMidGain  = (f - 0.5f) * 24.0f; break;
        case kParamEqHighFreq: fx.eqHighFreq = 2500.0f + f * 13500.0f; break;
        case kParamEqHighGain: fx.eqHighGain = (f - 0.5f) * 24.0f; break;

        case kParamCompOn:      fx.compOn = (f >= 0.5f); break;
        case kParamCompThresh:  fx.compThreshDb = -36.0f + f * 36.0f; break;
        case kParamCompRatio:   fx.compRatio = 1.0f + f * 11.0f; break;
        case kParamCompAttack:  fx.compAttackMs = 0.5f + f * 79.5f; break;
        case kParamCompRelease: fx.compReleaseMs = 15.0f + f * 385.0f; break;
        case kParamCompKnee:    fx.compKneeDb = f * 12.0f; break;
        case kParamCompMakeup:  fx.compMakeupDb = (f - 0.5f) * 24.0f; break;

        case kParamSatOn:     fx.satOn = (f >= 0.5f); break;
        case kParamSatDrive:  fx.satDrive = f; break;
        case kParamSatWarmth: fx.satWarmth = f; break;
        case kParamSatMix:    fx.satMix = f; break;

        case kParamDelayOn:       fx.delayOn = (f >= 0.5f); break;
        case kParamDelayMode:     fx.delayMode = (f >= 0.5f) ? 1 : 0; break;
        case kParamDelayTime:     fx.delayTimeMs = 20.0f + f * 780.0f; break;
        case kParamDelayFeedback: fx.delayFeedback = f * 0.88f; break;
        case kParamDelayTone:     fx.delayTone = f; break;
        case kParamDelayWidth:    fx.delayWidth = f; break;
        case kParamDelayMix:      fx.delayMix = f; break;

        case kParamRevOn:       fx.revOn = (f >= 0.5f); break;
        case kParamRevMode:     fx.revMode = std::clamp(static_cast<int>(std::round(f * 2.0f)), 0, 2); break;
        case kParamRevPreDelay: fx.revPreDelayMs = f * 120.0f; break;
        case kParamRevDecay:    fx.revDecaySec = 0.2f + f * 5.8f; break;
        case kParamRevSize:     fx.revSize = f; break;
        case kParamRevLowCut:   fx.revLowCutHz = 20.0f + f * 480.0f; break;
        case kParamRevHighCut:  fx.revHighCutHz = 1500.0f + f * 16500.0f; break;
        case kParamRevMix:      fx.revMix = f; break;

        case kParamLimiterOn:     fx.limiterOn = (f >= 0.5f); break;
        case kParamMonoBass:      fx.monoBass = (f >= 0.5f); break;
        case kParamVelCurve:
            p.velCurve = (f < 0.33f) ? VelocityCurve::Soft : ((f < 0.67f) ? VelocityCurve::Linear : VelocityCurve::Hard);
            break;
        case kParamGlobalFakeMic: p.globalFakeMic = (f >= 0.5f); break;

        case kParamPunishOn:     fx.punishOn = (f >= 0.5f); break;
        case kParamPunishAmount: fx.punishAmount = f; break;
        case kParamPunishMode:
            fx.punishMode = (f < 0.33f) ? PunishMode::GentlyNow : ((f < 0.67f) ? PunishMode::HurtMePlenty : PunishMode::Nightmare);
            break;

        case kParamThemeIndex: p.themeIndex = std::clamp(static_cast<int>(std::round(f * 9.0f)), 0, 9); break;
        case kParamUiZoom:     p.zoomIndex  = std::clamp(static_cast<int>(std::round(f * 2.0f)), 0, 2); break;
        default: break;
    }
}

tresult PLUGIN_API WreckageProcessor::process(ProcessData& data) {
    auto t0 = std::chrono::steady_clock::now();

    if (data.inputParameterChanges) {
        int32 numParams = data.inputParameterChanges->getParameterCount();
        for (int32 i = 0; i < numParams; ++i) {
            if (auto* q = data.inputParameterChanges->getParameterData(i)) {
                int32 numPts = q->getPointCount();
                if (numPts > 0) {
                    ParamValue val = 0.0;
                    int32 sampleOffset = 0;
                    if (q->getPoint(numPts - 1, sampleOffset, val) == kResultTrue)
                        applyNormalizedParam(q->getParameterId(), val);
                }
            }
        }
    }

    bool hadMidiEvent = false;
    if (data.inputEvents) {
        int32 nEv = data.inputEvents->getEventCount();
        for (int32 i = 0; i < nEv; ++i) {
            Event ev{};
            if (data.inputEvents->getEvent(i, ev) == kResultOk) {
                if (ev.type == Event::kNoteOnEvent) {
                    hadMidiEvent = true;
                    if (ev.noteOn.velocity > 0.0f) engine_.triggerNoteOn(ev.noteOn.pitch, ev.noteOn.velocity);
                    else engine_.triggerNoteOff(ev.noteOn.pitch);
                } else if (ev.type == Event::kNoteOffEvent) {
                    hadMidiEvent = true;
                    engine_.triggerNoteOff(ev.noteOff.pitch);
                }
            }
        }
    }

    if (data.numOutputs < 1 || data.numSamples <= 0) return kResultOk;
    if (data.outputs[0].numChannels < 2 || !data.outputs[0].channelBuffers32) return kResultOk;

    ProcessBuffers pb;
    pb.mainL = data.outputs[0].channelBuffers32[0];
    pb.mainR = data.outputs[0].channelBuffers32[1];
    pb.numSamples = data.numSamples;
    pb.offlineRender = (data.processMode == kOffline);

    for (int b = 0; b < 4; ++b) {
        int busIdx = b + 1;
        if (busIdx < data.numOutputs && busIdx < static_cast<int>(audioOutputs.size())) {
            auto* bus = FCast<AudioBus>(audioOutputs[busIdx]);
            if (bus && bus->isActive() && data.outputs[busIdx].numChannels >= 2 && data.outputs[busIdx].channelBuffers32) {
                pb.auxActive[b] = true;
                pb.auxL[b] = data.outputs[busIdx].channelBuffers32[0];
                pb.auxR[b] = data.outputs[busIdx].channelBuffers32[1];
            }
        }
    }

    engine_.process(pb);

    auto t1 = std::chrono::steady_clock::now();
    double elapsedSec = std::chrono::duration<double>(t1 - t0).count();
    double budgetSec = static_cast<double>(data.numSamples) / sampleRate_;
    float instCpu = (budgetSec > 0.0) ? static_cast<float>(std::clamp((elapsedSec / budgetSec) * 100.0, 0.0, 99.9)) : 0.0f;
    cpuLoadPct_ = cpuLoadPct_ * 0.85f + instCpu * 0.15f;

    if (hadMidiEvent || ++telemetryCounter_ >= 3) {
        telemetryCounter_ = 0;
        broadcastTelemetryToUi();
    }
    return kResultOk;
}

tresult PLUGIN_API WreckageProcessor::setState(IBStream* state) {
    if (!state) return kResultFalse;
    IBStreamer s(state, kLittleEndian);
    int32 ver = 0;
    if (!s.readInt32(ver) || ver != 6) return kResultFalse;

    auto& p = engine_.params();
    auto& fx = p.fx;
    for (int i = 0; i < 4; ++i) {
        s.readFloat(p.busGain[i]);
        int8 byp = 0; s.readInt8(byp); p.busBypass[i] = (byp != 0);
    }
    int8 gm = 1;
    s.readInt8(gm); p.globalFakeMic = (gm != 0);

    int8 b = 0; int32 iv = 0;
    s.readInt8(b); fx.filterOn = (b != 0); s.readInt32(fx.filterType); s.readFloat(fx.filterCutoffHz); s.readFloat(fx.filterRes);
    s.readInt8(b); fx.eqOn = (b != 0); s.readFloat(fx.eqLowFreq); s.readFloat(fx.eqLowGain); s.readFloat(fx.eqMidFreq); s.readFloat(fx.eqMidGain); s.readFloat(fx.eqHighFreq); s.readFloat(fx.eqHighGain);
    s.readInt8(b); fx.compOn = (b != 0); s.readFloat(fx.compThreshDb); s.readFloat(fx.compRatio); s.readFloat(fx.compAttackMs); s.readFloat(fx.compReleaseMs); s.readFloat(fx.compKneeDb); s.readFloat(fx.compMakeupDb);
    s.readInt8(b); fx.satOn = (b != 0); s.readFloat(fx.satDrive); s.readFloat(fx.satWarmth); s.readFloat(fx.satMix);
    s.readInt8(b); fx.delayOn = (b != 0); s.readInt32(fx.delayMode); s.readFloat(fx.delayTimeMs); s.readFloat(fx.delayFeedback); s.readFloat(fx.delayTone); s.readFloat(fx.delayWidth); s.readFloat(fx.delayMix);
    s.readInt8(b); fx.revOn = (b != 0); s.readInt32(fx.revMode); s.readFloat(fx.revPreDelayMs); s.readFloat(fx.revDecaySec); s.readFloat(fx.revSize); s.writeFloat(fx.revLowCutHz); s.readFloat(fx.revHighCutHz); s.readFloat(fx.revMix);
    s.readInt8(b); fx.limiterOn = (b != 0);
    s.readInt8(b); fx.monoBass = (b != 0);
    s.readInt32(iv); p.velCurve = static_cast<VelocityCurve>(iv);
    s.readInt8(b); fx.punishOn = (b != 0); s.readFloat(fx.punishAmount); s.readInt32(iv); fx.punishMode = static_cast<PunishMode>(iv);
    s.readInt32(p.themeIndex);
    s.readInt32(p.zoomIndex);
    return kResultOk;
}

tresult PLUGIN_API WreckageProcessor::getState(IBStream* state) {
    if (!state) return kResultFalse;
    IBStreamer s(state, kLittleEndian);
    s.writeInt32(6);

    const auto& p = engine_.params();
    const auto& fx = p.fx;
    for (int i = 0; i < 4; ++i) {
        s.writeFloat(p.busGain[i]);
        s.writeInt8(p.busBypass[i] ? 1 : 0);
    }
    s.writeInt8(p.globalFakeMic ? 1 : 0);

    s.writeInt8(fx.filterOn ? 1 : 0); s.writeInt32(fx.filterType); s.writeFloat(fx.filterCutoffHz); s.writeFloat(fx.filterRes);
    s.writeInt8(fx.eqOn ? 1 : 0); s.writeFloat(fx.eqLowFreq); s.writeFloat(fx.eqLowGain); s.writeFloat(fx.eqMidFreq); s.writeFloat(fx.eqMidGain); s.writeFloat(fx.eqHighFreq); s.writeFloat(fx.eqHighGain);
    s.writeInt8(fx.compOn ? 1 : 0); s.writeFloat(fx.compThreshDb); s.writeFloat(fx.compRatio); s.writeFloat(fx.compAttackMs); s.writeFloat(fx.compReleaseMs); s.writeFloat(fx.compKneeDb); s.writeFloat(fx.compMakeupDb);
    s.writeInt8(fx.satOn ? 1 : 0); s.writeFloat(fx.satDrive); s.writeFloat(fx.satWarmth); s.writeFloat(fx.satMix);
    s.writeInt8(fx.delayOn ? 1 : 0); s.writeInt32(fx.delayMode); s.writeFloat(fx.delayTimeMs); s.writeFloat(fx.delayFeedback); s.writeFloat(fx.delayTone); s.writeFloat(fx.delayWidth); s.writeFloat(fx.delayMix);
    s.writeInt8(fx.revOn ? 1 : 0); s.writeInt32(fx.revMode); s.writeFloat(fx.revPreDelayMs); s.writeFloat(fx.revDecaySec); s.writeFloat(fx.revSize); s.writeFloat(fx.revLowCutHz); s.writeFloat(fx.revHighCutHz); s.writeFloat(fx.revMix);
    s.writeInt8(fx.limiterOn ? 1 : 0);
    s.writeInt8(fx.monoBass ? 1 : 0);
    s.writeInt32(static_cast<int32>(p.velCurve));
    s.writeInt8(fx.punishOn ? 1 : 0); s.writeFloat(fx.punishAmount); s.writeInt32(static_cast<int32>(fx.punishMode));
    s.writeInt32(p.themeIndex);
    s.writeInt32(p.zoomIndex);
    return kResultOk;
}

void WreckageProcessor::broadcastSlotSummaryToUi(int slotIdx) {
    if (auto* msg = allocateMessage()) {
        msg->setMessageID("SlotUpdated");
        auto* attrs = msg->getAttributes();
        const auto& sl = engine_.slot(slotIdx);
        attrs->setInt("slot", slotIdx);
        attrs->setInt("color", static_cast<int64>(sl.hasCustomColor ? sl.keyColor : 0));
        attrs->setInt("midiNote", sl.midiNote);
        attrs->setFloat("x", sl.stageX);
        attrs->setFloat("y", sl.stageY);
        attrs->setInt("fakeRR", sl.fakeRR ? 1 : 0);
        attrs->setInt("fakeVel", sl.fakeVel ? 1 : 0);
        attrs->setInt("activeVelLayers", sl.activeVelLayerCount());
        int counts[kMaxVelLayers] = {0};
        for (int vl = 0; vl < kMaxVelLayers; ++vl) {
            counts[vl] = sl.rrCountInLayer(vl);
        }
        attrs->setBinary("rrCounts", counts, static_cast<uint32>(sizeof(counts)));
        attrs->setBinary("nameUtf8", sl.name.data(), static_cast<uint32>(sl.name.size()));
        sendMessage(msg);
        msg->release();
    }
}

void WreckageProcessor::broadcastWaveDataToUi(int slotIdx, int velLayerIdx, int rrIdx) {
    const auto& sl = engine_.slot(slotIdx);
    int vl = std::clamp(velLayerIdx, 0, kMaxVelLayers - 1);
    const auto& rrs = sl.velLayers[vl].rrs;
    if (rrIdx < 0 || rrIdx >= static_cast<int>(rrs.size())) return;
    const auto& it = rrs[static_cast<size_t>(rrIdx)];
    auto prim = it.primarySource();
    if (!prim) return;

    if (auto* msg = allocateMessage()) {
        msg->setMessageID("WaveUpdated");
        auto* attrs = msg->getAttributes();
        attrs->setInt("slot", slotIdx);
        attrs->setInt("velLayer", vl);
        attrs->setInt("rr", rrIdx);
        attrs->setFloat("start", it.startNorm);
        attrs->setFloat("end", it.endNorm);
        attrs->setFloat("fadeIn", it.fadeInNorm);
        attrs->setFloat("fadeOut", it.fadeOutNorm);
        attrs->setBinary("peaks", prim->peaks.data(), static_cast<uint32>(prim->peaks.size() * sizeof(float)));
        sendMessage(msg);
        msg->release();
    }
}

void WreckageProcessor::broadcastTelemetryToUi() {
    if (auto* msg = allocateMessage()) {
        msg->setMessageID("Telemetry");
        auto* attrs = msg->getAttributes();
        attrs->setInt("voices", engine_.activeVoiceCount());
        attrs->setInt("ramBytes", static_cast<int64>(engine_.totalSampleRamBytes()));
        attrs->setFloat("cpuPct", static_cast<double>(cpuLoadPct_));
        attrs->setFloat("grDb", static_cast<double>(engine_.currentGrDb()));
        attrs->setFloat("outLvl", static_cast<double>(engine_.currentOutLevel()));

        float ph = -1.0f;
        int actVl = -1, actRr = -1;
        engine_.getSlotLivePlayback(selectedUiSlot_, ph, actVl, actRr);
        attrs->setFloat("playhead", static_cast<double>(ph));
        attrs->setInt("actVelLayer", actVl);
        attrs->setInt("actRR", actRr);

        uint64_t lo = 0, hi = 0;
        for (int n = 0; n < 64; ++n) {
            if (engine_.isNotePlaying(n)) lo |= (1ULL << n);
            if (engine_.isNotePlaying(n + 64)) hi |= (1ULL << n);
        }
        attrs->setInt("notesLo", static_cast<int64>(lo));
        attrs->setInt("notesHi", static_cast<int64>(hi));
        sendMessage(msg);
        msg->release();
    }
}

tresult PLUGIN_API WreckageProcessor::notify(IMessage* message) {
    if (!message) return kInvalidArgument;
    const char* id = message->getMessageID();
    auto* attrs = message->getAttributes();
    if (!id || !attrs) return AudioEffect::notify(message);

    if (strcmp(id, "NoteOn") == 0) {
        int64 note = kBaseMidiNote; double vel = 0.9;
        attrs->getInt("note", note); attrs->getFloat("vel", vel);
        engine_.triggerNoteOn(static_cast<int>(note), static_cast<float>(vel));
        broadcastTelemetryToUi();
        return kResultOk;
    }
    if (strcmp(id, "NoteOff") == 0) {
        int64 note = kBaseMidiNote;
        attrs->getInt("note", note);
        engine_.triggerNoteOff(static_cast<int>(note));
        broadcastTelemetryToUi();
        return kResultOk;
    }
    if (strcmp(id, "SelectSlot") == 0) {
        int64 sl = 0, vl = 0;
        attrs->getInt("slot", sl); attrs->getInt("velLayer", vl);
        selectedUiSlot_ = std::clamp(static_cast<int>(sl), 0, kTotalSlots - 1);
        int v = std::clamp(static_cast<int>(vl), 0, kMaxVelLayers - 1);
        if (engine_.slot(selectedUiSlot_).rrCountInLayer(v) > 0) {
            broadcastWaveDataToUi(selectedUiSlot_, v, 0);
        }
        return kResultOk;
    }
    if (strcmp(id, "Panic") == 0) {
        engine_.panicAllNotesOff();
        broadcastTelemetryToUi();
        return kResultOk;
    }
    if (strcmp(id, "ResetKit") == 0) {
        engine_.resetKit();
        for (int i = 0; i < kTotalSlots; ++i) broadcastSlotSummaryToUi(i);
        broadcastTelemetryToUi();
        return kResultOk;
    }
    if (strcmp(id, "ClearSlot") == 0) {
        int64 sl = 0; attrs->getInt("slot", sl);
        engine_.clearSlot(static_cast<int>(sl));
        broadcastSlotSummaryToUi(static_cast<int>(sl));
        broadcastTelemetryToUi();
        return kResultOk;
    }
    if (strcmp(id, "DeleteWaveItem") == 0) {
        int64 sl = 0, vl = 0, rr = 0;
        attrs->getInt("slot", sl); attrs->getInt("velLayer", vl); attrs->getInt("rr", rr);
        int sIdx = static_cast<int>(sl);
        int vIdx = static_cast<int>(vl);
        engine_.deleteWaveItem(sIdx, vIdx, static_cast<int>(rr));
        broadcastSlotSummaryToUi(sIdx);
        if (engine_.slot(sIdx).rrCountInLayer(vIdx) > 0) {
            broadcastWaveDataToUi(sIdx, vIdx, 0);
        }
        broadcastTelemetryToUi();
        return kResultOk;
    }
    if (strcmp(id, "ResetStage") == 0) {
        engine_.resetStagePositions();
        for (int i = 0; i < kTotalSlots; ++i) broadcastSlotSummaryToUi(i);
        return kResultOk;
    }
    if (strcmp(id, "SetStageXY") == 0) {
        int64 sl = 0; double x = 0.0, y = 0.0;
        attrs->getInt("slot", sl); attrs->getFloat("x", x); attrs->getFloat("y", y);
        auto& s = engine_.slot(static_cast<int>(sl));
        s.stageX = std::clamp(static_cast<float>(x), -1.0f, 1.0f);
        s.stageY = std::clamp(static_cast<float>(y), 0.0f, 1.0f);
        return kResultOk;
    }
    if (strcmp(id, "SetKeyColor") == 0) {
        int64 sl = 0, col = 0;
        attrs->getInt("slot", sl); attrs->getInt("color", col);
        auto& s = engine_.slot(static_cast<int>(sl));
        s.keyColor = static_cast<uint32_t>(col);
        s.hasCustomColor = (col != 0);
        return kResultOk;
    }
    if (strcmp(id, "RenameSlot") == 0) {
        int64 sl = 0; const void* ptr = nullptr; uint32 sz = 0;
        attrs->getInt("slot", sl);
        if (attrs->getBinary("nameUtf8", ptr, sz) == kResultOk && ptr && sz > 0) {
            auto& s = engine_.slot(static_cast<int>(sl));
            s.name.assign(static_cast<const char*>(ptr), sz);
            broadcastSlotSummaryToUi(static_cast<int>(sl));
        }
        return kResultOk;
    }
    if (strcmp(id, "SetSlotProp") == 0) {
        int64 sl = 0, prop = 0, ival = 0; double fval = 0.0;
        attrs->getInt("slot", sl); attrs->getInt("prop", prop); attrs->getInt("ival", ival); attrs->getFloat("fval", fval);
        auto& s = engine_.slot(static_cast<int>(sl));
        switch (prop) {
            case 0: s.gain = gainDbToLinear(gainNormToDb(static_cast<float>(fval))); break;
            case 1: s.pitchSemitones = (static_cast<float>(fval) - 0.5f) * 48.0f; break;
            case 2: s.attackMs = static_cast<float>(fval) * 200.0f; break;
            case 3: s.releaseMs = 15.0f + static_cast<float>(fval) * 3985.0f; break;
            case 5: s.oneShot = (ival != 0); break;
            case 6: s.chokeGroup = static_cast<int>(ival); break;
            case 7: s.reverse = (ival != 0); break;
            case 8: s.purged = (ival != 0); break;
            case 10: s.toneLowDb  = (static_cast<float>(fval) - 0.5f) * 24.0f; break;
            case 11: s.toneHighDb = (static_cast<float>(fval) - 0.5f) * 24.0f; break;
            case 12: s.fakeRR = (ival != 0); break;
            case 13: s.fakeVel = (ival != 0); break;
            default: break;
        }
        return kResultOk;
    }
    if (strcmp(id, "SetWaveEdit") == 0) {
        int64 sl = 0, vl = 0, rr = 0;
        double st = 0.0, en = 1.0, fi = 0.0, fo = 0.0;
        attrs->getInt("slot", sl); attrs->getInt("velLayer", vl); attrs->getInt("rr", rr);
        attrs->getFloat("start", st); attrs->getFloat("end", en);
        attrs->getFloat("fadeIn", fi); attrs->getFloat("fadeOut", fo);
        auto& s = engine_.slot(static_cast<int>(sl));
        int vIdx = std::clamp(static_cast<int>(vl), 0, kMaxVelLayers - 1);
        auto& rrs = s.velLayers[vIdx].rrs;
        if (rr >= 0 && rr < static_cast<int>(rrs.size())) {
            auto& it = rrs[static_cast<size_t>(rr)];
            it.startNorm   = std::clamp(static_cast<float>(st), 0.0f, 0.98f);
            it.endNorm     = std::clamp(static_cast<float>(en), it.startNorm + 0.02f, 1.0f);
            it.fadeInNorm  = std::clamp(static_cast<float>(fi), 0.0f, 0.5f);
            it.fadeOutNorm = std::clamp(static_cast<float>(fo), 0.0f, 0.5f);
        }
        return kResultOk;
    }
    if (strcmp(id, "RequestWave") == 0) {
        int64 sl = 0, vl = 0, rr = 0;
        attrs->getInt("slot", sl); attrs->getInt("velLayer", vl); attrs->getInt("rr", rr);
        broadcastWaveDataToUi(static_cast<int>(sl), static_cast<int>(vl), static_cast<int>(rr));
        return kResultOk;
    }
    if (strcmp(id, "LoadWav") == 0) {
        int64 sl = 0, prefVl = -1; const void* ptr = nullptr; uint32 sz = 0;
        attrs->getInt("slot", sl); attrs->getInt("prefVelLayer", prefVl);
        if (attrs->getBinary("pathUtf8", ptr, sz) == kResultOk && ptr && sz > 0) {
            std::string u8(static_cast<const char*>(ptr), sz);
            int sIdx = static_cast<int>(sl);
            if (engine_.loadSampleIntoSlot(sIdx, pathFromUtf8(u8), static_cast<int>(prefVl))) {
                int vlUse = (prefVl >= 0) ? std::clamp(static_cast<int>(prefVl), 0, kMaxVelLayers - 1) : 0;
                int rrNew = std::max(0, engine_.slot(sIdx).rrCountInLayer(vlUse) - 1);
                broadcastSlotSummaryToUi(sIdx);
                broadcastWaveDataToUi(sIdx, vlUse, rrNew);
                broadcastTelemetryToUi();
            }
        }
        return kResultOk;
    }
    if (strcmp(id, "ExportNm6") == 0) {
        const void* ptr = nullptr; uint32 sz = 0;
        if (attrs->getBinary("pathUtf8", ptr, sz) == kResultOk && ptr && sz > 0) {
            std::string u8(static_cast<const char*>(ptr), sz);
            auto p = pathFromUtf8(u8);
            bool ok = engine_.exportKitToNm6(p, p.stem().u8string());
            if (auto* reply = allocateMessage()) {
                reply->setMessageID("SaveDone");
                reply->getAttributes()->setInt("ok", ok ? 1 : 0);
                sendMessage(reply);
                reply->release();
            }
        }
        return kResultOk;
    }
    if (strcmp(id, "LoadNm6") == 0) {
        const void* ptr = nullptr; uint32 sz = 0;
        if (attrs->getBinary("pathUtf8", ptr, sz) == kResultOk && ptr && sz > 0) {
            std::string u8(static_cast<const char*>(ptr), sz);
            if (engine_.loadKitFromNm6(pathFromUtf8(u8))) {
                for (int i = 0; i < kTotalSlots; ++i) {
                    broadcastSlotSummaryToUi(i);
                    for (int vl = 0; vl < kMaxVelLayers; ++vl) {
                        if (engine_.slot(i).rrCountInLayer(vl) > 0) {
                            broadcastWaveDataToUi(i, vl, 0);
                        }
                    }
                }
                broadcastTelemetryToUi();
            }
        }
        return kResultOk;
    }
    return AudioEffect::notify(message);
}

} // namespace NoiseMu6ix

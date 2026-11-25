#include "wreckage_processor.h"
#include "wreckage_uids.h"
#include "base/source/fstreamer.h"
#include "public.sdk/source/vst/vstbus.h"
#include <algorithm>
#include <cstring>
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "pluginterfaces/base/ustring.h"
#include <cmath>
#include <filesystem>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace NoiseMu6ix {

WreckageProcessor::WreckageProcessor() {
    setControllerClass(kWreckageControllerUID);
}

tresult PLUGIN_API WreckageProcessor::initialize(FUnknown* context) {
    tresult res = AudioEffect::initialize(context);
    if (res != kResultOk) return res;

    // Main Stereo Output + 5 Multi-Out Aux Stereo Buses (Close, Room, Hall, LFE, Crush)
    addAudioOutput(STR16("Main Out"),  SpeakerArr::kStereo, BusTypes::kMain);
    addAudioOutput(STR16("Close Out"), SpeakerArr::kStereo, BusTypes::kAux, 0);
    addAudioOutput(STR16("Room Out"),  SpeakerArr::kStereo, BusTypes::kAux, 0);
    addAudioOutput(STR16("Hall Out"),  SpeakerArr::kStereo, BusTypes::kAux, 0);
    addAudioOutput(STR16("LFE Out"),   SpeakerArr::kStereo, BusTypes::kAux, 0);
    addAudioOutput(STR16("Crush Out"), SpeakerArr::kStereo, BusTypes::kAux, 0);

    addEventInput(STR16("MIDI In"), 1);
    engine_.prepare(44100.0);
    return kResultOk;
}

tresult PLUGIN_API WreckageProcessor::terminate() {
    return AudioEffect::terminate();
}

tresult PLUGIN_API WreckageProcessor::setActive(TBool state) {
    return AudioEffect::setActive(state);
}

tresult PLUGIN_API WreckageProcessor::setupProcessing(ProcessSetup& newSetup) {
    engine_.prepare(newSetup.sampleRate);
    return AudioEffect::setupProcessing(newSetup);
}

tresult PLUGIN_API WreckageProcessor::canProcessSampleSize(int32 symbolicSampleSize) {
    return (symbolicSampleSize == kSample32) ? kResultTrue : kResultFalse;
}

tresult PLUGIN_API WreckageProcessor::setBusArrangements(SpeakerArrangement* inputs, int32 numIns,
                                                         SpeakerArrangement* outputs, int32 numOuts) {
    if (numIns == 0 && numOuts >= 1 && outputs[0] == SpeakerArr::kStereo) {
        return AudioEffect::setBusArrangements(inputs, numIns, outputs, numOuts);
    }
    return kResultFalse;
}

void WreckageProcessor::applyNormalizedParam(ParamID id, ParamValue val) {
    auto& p = engine_.params();
    float f = static_cast<float>(std::clamp(val, 0.0, 1.0));
    switch (id) {
        case kParamBusCloseGain:   p.busGain[0] = f * 1.25f; break;
        case kParamBusRoomGain:    p.busGain[1] = f * 1.25f; break;
        case kParamBusHallGain:    p.busGain[2] = f * 1.25f; break;
        case kParamBusLfeGain:     p.busGain[3] = f * 1.25f; break;
        case kParamBusCrushGain:   p.busGain[4] = f * 1.25f; break;
        case kParamBusCloseBypass: p.busBypass[0] = (f >= 0.5f); break;
        case kParamBusRoomBypass:  p.busBypass[1] = (f >= 0.5f); break;
        case kParamBusHallBypass:  p.busBypass[2] = (f >= 0.5f); break;
        case kParamBusLfeBypass:   p.busBypass[3] = (f >= 0.5f); break;
        case kParamBusCrushBypass: p.busBypass[4] = (f >= 0.5f); break;
        case kParamMasterHPF:      p.masterHpfHz = 20.0f * std::pow(20.0f, f); break;
        case kParamMasterLPF:      p.masterLpfHz = 2000.0f * std::pow(10.0f, f); break;
        case kParamEqLow:          p.eqLowDb  = (f - 0.5f) * 24.0f; break;
        case kParamEqMid:          p.eqMidDb  = (f - 0.5f) * 24.0f; break;
        case kParamEqHigh:         p.eqHighDb = (f - 0.5f) * 24.0f; break;
        case kParamCompAmount:     p.compAmount = f; break;
        case kParamTransientPunch: p.transientPunch = f; break;
        case kParamStereoWidth:    p.stereoWidth = f; break;
        case kParamMonoBass:       p.monoBass = (f >= 0.5f); break;
        case kParamVelCurve:
            p.velCurve = (f < 0.33f) ? VelocityCurve::Soft : ((f < 0.67f) ? VelocityCurve::Linear : VelocityCurve::Hard);
            break;
        case kParamLimiterOn:      p.limiterOn = (f >= 0.5f); break;
        case kParamMasterOutGain:  p.masterOutGain = f * 1.2f; break;
        case kParamPunishAmount:   p.punishAmount = f; break;
        case kParamPunishMode:
            p.punishMode = (f < 0.33f) ? PunishMode::GentlyNow : ((f < 0.67f) ? PunishMode::HurtMePlenty : PunishMode::Nightmare);
            break;
        case kParamThemeIndex:     p.themeIndex = std::clamp(static_cast<int>(std::round(f * 9.0f)), 0, 9); break;
        case kParamUiZoom:         p.zoomIndex  = std::clamp(static_cast<int>(std::round(f * 2.0f)), 0, 2); break;
        case kParamStageMasterDepth: p.stageMasterDepth = f * 1.5f; break;
        case kParamSmartIntensity:   p.smartIntensity   = f; break;
        default: break;
    }
}

tresult PLUGIN_API WreckageProcessor::process(ProcessData& data) {
    // 1. Handle Parameter Automation Changes
    if (data.inputParameterChanges) {
        int32 numParams = data.inputParameterChanges->getParameterCount();
        for (int32 i = 0; i < numParams; ++i) {
            if (auto* q = data.inputParameterChanges->getParameterData(i)) {
                int32 numPts = q->getPointCount();
                if (numPts > 0) {
                    ParamValue val = 0.0;
                    int32 sampleOffset = 0;
                    if (q->getPoint(numPts - 1, sampleOffset, val) == kResultTrue) {
                        applyNormalizedParam(q->getParameterId(), val);
                    }
                }
            }
        }
    }

    // 2. Handle Incoming MIDI Note Events
    if (data.inputEvents) {
        int32 nEv = data.inputEvents->getEventCount();
        for (int32 i = 0; i < nEv; ++i) {
            Event ev{};
            if (data.inputEvents->getEvent(i, ev) == kResultOk) {
                if (ev.type == Event::kNoteOnEvent) {
                    if (ev.noteOn.velocity > 0.0f) engine_.triggerNoteOn(ev.noteOn.pitch, ev.noteOn.velocity);
                    else engine_.triggerNoteOff(ev.noteOn.pitch);
                } else if (ev.type == Event::kNoteOffEvent) {
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

    // Multi-Out Auto-Routing check
    for (int b = 0; b < 5; ++b) {
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
    return kResultOk;
}

tresult PLUGIN_API WreckageProcessor::setState(IBStream* state) {
    if (!state) return kResultFalse;
    IBStreamer s(state, kLittleEndian);
    int32 ver = 0;
    if (!s.readInt32(ver) || ver != 1) return kResultFalse;

    auto& p = engine_.params();
    for (int i = 0; i < 5; ++i) {
        s.readFloat(p.busGain[i]);
        int8 byp = 0; s.readInt8(byp); p.busBypass[i] = (byp != 0);
    }
    s.readFloat(p.masterHpfHz);
    s.readFloat(p.masterLpfHz);
    s.readFloat(p.eqLowDb);
    s.readFloat(p.eqMidDb);
    s.readFloat(p.eqHighDb);
    s.readFloat(p.compAmount);
    s.readFloat(p.transientPunch);
    s.readFloat(p.stereoWidth);
    int8 mb = 1, lim = 1;
    s.readInt8(mb); p.monoBass = (mb != 0);
    s.readInt8(lim); p.limiterOn = (lim != 0);
    int32 vc = 1, pm = 1;
    s.readInt32(vc); p.velCurve = static_cast<VelocityCurve>(vc);
    s.readFloat(p.masterOutGain);
    s.readFloat(p.punishAmount);
    s.readInt32(pm); p.punishMode = static_cast<PunishMode>(pm);
    s.readInt32(p.themeIndex);
    s.readInt32(p.zoomIndex);

    // Restore 36 slot configs (Stage X/Y, Key Colors, Smart Modes, Drum Modes, MIDI Note)
    for (int i = 0; i < kTotalSlots; ++i) {
        auto& sl = engine_.slot(i);
        int32 note = kBaseMidiNote + i;
        uint32 col = 0;
        int8 hasCol = 0, os = 1, rev = 0, prg = 0;
        int32 cg = 0, rrM = 0, velM = 0, micM = 0;
        s.readInt32(note); sl.midiNote = note;
        s.readInt32u(col); sl.keyColor = col;
        s.readInt8(hasCol); sl.hasCustomColor = (hasCol != 0);
        s.readFloat(sl.gain);
        s.readFloat(sl.pitchSemitones);
        s.readFloat(sl.attackMs);
        s.readFloat(sl.releaseMs);
        s.readFloat(sl.xfade);
        s.readFloat(sl.stageX);
        s.readFloat(sl.stageY);
        s.readInt8(os); sl.oneShot = (os != 0);
        s.readInt32(cg); sl.chokeGroup = cg;
        s.readInt8(rev); sl.reverse = (rev != 0);
        s.readInt8(prg); sl.purged = (prg != 0);
        s.readInt32(rrM);  sl.rrMode  = static_cast<SmartMode>(rrM);
        s.readInt32(velM); sl.velMode = static_cast<SmartMode>(velM);
        s.readInt32(micM); sl.micMode = static_cast<SmartMode>(micM);
    }
    return kResultOk;
}

tresult PLUGIN_API WreckageProcessor::getState(IBStream* state) {
    if (!state) return kResultFalse;
    IBStreamer s(state, kLittleEndian);
    s.writeInt32(1); // Schema version 1 (forward-compatible)

    const auto& p = engine_.params();
    for (int i = 0; i < 5; ++i) {
        s.writeFloat(p.busGain[i]);
        s.writeInt8(p.busBypass[i] ? 1 : 0);
    }
    s.writeFloat(p.masterHpfHz);
    s.writeFloat(p.masterLpfHz);
    s.writeFloat(p.eqLowDb);
    s.writeFloat(p.eqMidDb);
    s.writeFloat(p.eqHighDb);
    s.writeFloat(p.compAmount);
    s.writeFloat(p.transientPunch);
    s.writeFloat(p.stereoWidth);
    s.writeInt8(p.monoBass ? 1 : 0);
    s.writeInt8(p.limiterOn ? 1 : 0);
    s.writeInt32(static_cast<int32>(p.velCurve));
    s.writeFloat(p.masterOutGain);
    s.writeFloat(p.punishAmount);
    s.writeInt32(static_cast<int32>(p.punishMode));
    s.writeInt32(p.themeIndex);
    s.writeInt32(p.zoomIndex);

    for (int i = 0; i < kTotalSlots; ++i) {
        const auto& sl = engine_.slot(i);
        s.writeInt32(sl.midiNote);
        s.writeInt32u(sl.keyColor);
        s.writeInt8(sl.hasCustomColor ? 1 : 0);
        s.writeFloat(sl.gain);
        s.writeFloat(sl.pitchSemitones);
        s.writeFloat(sl.attackMs);
        s.writeFloat(sl.releaseMs);
        s.writeFloat(sl.xfade);
        s.writeFloat(sl.stageX);
        s.writeFloat(sl.stageY);
        s.writeInt8(sl.oneShot ? 1 : 0);
        s.writeInt32(sl.chokeGroup);
        s.writeInt8(sl.reverse ? 1 : 0);
        s.writeInt8(sl.purged ? 1 : 0);
        s.writeInt32(static_cast<int32>(sl.rrMode));
        s.writeInt32(static_cast<int32>(sl.velMode));
        s.writeInt32(static_cast<int32>(sl.micMode));
    }
    return kResultOk;
}

void WreckageProcessor::broadcastSlotSummaryToUi(int slotIdx) {
    if (auto* msg = allocateMessage()) {
        msg->setMessageID("SlotUpdated");
        auto* attrs = msg->getAttributes();
        const auto& sl = engine_.slot(slotIdx);
        attrs->setInt("slot", slotIdx);
        attrs->setInt("velCount", sl.velocityCount());
        attrs->setInt("rrCount", sl.maxRRCount());
        attrs->setInt("color", static_cast<int64>(sl.hasCustomColor ? sl.keyColor : 0));
        attrs->setInt("midiNote", sl.midiNote);
        String128 name16{};
        UString(name16, 128).fromAscii(sl.name.c_str());
        attrs->setString("name", name16);
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
        int64 note = 36; double vel = 0.9;
        attrs->getInt("note", note);
        attrs->getFloat("vel", vel);
        engine_.triggerNoteOn(static_cast<int>(note), static_cast<float>(vel));
        return kResultOk;
    }
    if (strcmp(id, "NoteOff") == 0) {
        int64 note = 36;
        attrs->getInt("note", note);
        engine_.triggerNoteOff(static_cast<int>(note));
        return kResultOk;
    }
    if (strcmp(id, "SetStageXY") == 0) {
        int64 sl = 0; double x = 0.0, y = 0.25;
        attrs->getInt("slot", sl);
        attrs->getFloat("x", x);
        attrs->getFloat("y", y);
        auto& s = engine_.slot(static_cast<int>(sl));
        s.stageX = std::clamp(static_cast<float>(x), -1.0f, 1.0f);
        s.stageY = std::clamp(static_cast<float>(y), 0.0f, 1.0f);
        return kResultOk;
    }
    if (strcmp(id, "SetKeyColor") == 0) {
        int64 sl = 0, col = 0;
        attrs->getInt("slot", sl);
        attrs->getInt("color", col);
        auto& s = engine_.slot(static_cast<int>(sl));
        s.keyColor = static_cast<uint32_t>(col);
        s.hasCustomColor = (col != 0);
        return kResultOk;
    }
    if (strcmp(id, "SetSmartMode") == 0) {
        int64 sl = 0, which = 0, mode = 0;
        attrs->getInt("slot", sl);
        attrs->getInt("which", which);
        attrs->getInt("mode", mode);
        auto& s = engine_.slot(static_cast<int>(sl));
        SmartMode sm = static_cast<SmartMode>(std::clamp<int64>(mode, 0, 2));
        if (which == 0) s.rrMode = sm;
        else if (which == 1) s.velMode = sm;
        else if (which == 2) s.micMode = sm;
        return kResultOk;
    }
    if (strcmp(id, "SetSlotProp") == 0) {
        int64 sl = 0, prop = 0, ival = 0; double fval = 0.0;
        attrs->getInt("slot", sl);
        attrs->getInt("prop", prop);
        attrs->getInt("ival", ival);
        attrs->getFloat("fval", fval);
        auto& s = engine_.slot(static_cast<int>(sl));
        switch (prop) {
            case 0: s.gain = static_cast<float>(fval) * 1.5f; break;
            case 1: s.pitchSemitones = (static_cast<float>(fval) - 0.5f) * 48.0f; break;
            case 2: s.attackMs = static_cast<float>(fval) * 200.0f; break;
            case 3: s.releaseMs = 20.0f + static_cast<float>(fval) * 3980.0f; break;
            case 4: s.xfade = static_cast<float>(fval); break;
            case 5: s.oneShot = (ival != 0); break;
            case 6: s.chokeGroup = static_cast<int>(ival); break;
            case 7: s.reverse = (ival != 0); break;
            case 8: s.purged = (ival != 0); break;
            case 9: s.midiNote = std::clamp(static_cast<int>(ival), 0, 127); break;
            default: break;
        }
        return kResultOk;
    }
#if !WRECKAGE_IS_PLAYER
    if (strcmp(id, "LoadWav") == 0) {
        int64 sl = 0;
        String128 path16{};
        attrs->getInt("slot", sl);
        if (attrs->getString("path", path16, sizeof(path16)) == kResultOk) {
            std::filesystem::path p(reinterpret_cast<const char16_t*>(path16));
            if (engine_.loadSampleIntoSlot(static_cast<int>(sl), p)) {
                broadcastSlotSummaryToUi(static_cast<int>(sl));
            }
        }
        return kResultOk;
    }
    if (strcmp(id, "ExportNm6") == 0) {
        String128 path16{};
        if (attrs->getString("path", path16, sizeof(path16)) == kResultOk) {
            std::filesystem::path p(reinterpret_cast<const char16_t*>(path16));
            engine_.exportKitToNm6(p, p.stem().u8string());
        }
        return kResultOk;
    }
#endif
    return AudioEffect::notify(message);
}

} // namespace NoiseMu6ix

#include "wreckage_controller.h"
#include "wreckage_view_win.h"
#include "base/source/fstreamer.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#ifdef _WIN32
#include <windows.h>
#endif

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace NoiseMu6ix {

static std::string wideToUtf8(const std::wstring& w) {
#ifdef _WIN32
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(std::max(0, n - 1)), '\0');
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &out[0], n, nullptr, nullptr);
    return out;
#else
    return std::string(w.begin(), w.end());
#endif
}

WreckageController::WreckageController() {
    for (int i = 0; i < kTotalSlots; ++i) {
        slots_[i].midiNote = kBaseMidiNote + i;
        slots_[i].name = "EMPTY";
        slots_[i].stageX = 0.0f;
        slots_[i].stageY = 0.0f;
    }
}

tresult PLUGIN_API WreckageController::initialize(FUnknown* context) {
    tresult res = EditControllerEx1::initialize(context);
    if (res != kResultOk) return res;

    auto addRange = [&](ParamID id, const TChar* title, double defVal, int32 stepCount = 0) {
        parameters.addParameter(title, nullptr, stepCount, defVal, ParameterInfo::kCanAutomate, id);
    };

    // 0.75 = 0.0 dB Unity Gain default for all 4 Mic Buses
    addRange(kParamBusCloseGain,   STR16("Bus Close"),    0.75);
    addRange(kParamBusRoomGain,    STR16("Bus Room"),     0.75);
    addRange(kParamBusHallGain,    STR16("Bus Hall"),     0.75);
    addRange(kParamBusSubGain,     STR16("Bus Sub"),      0.75);
    addRange(kParamBusCloseBypass, STR16("Close Bypass"), 0.0, 1);
    addRange(kParamBusRoomBypass,  STR16("Room Bypass"),  0.0, 1);
    addRange(kParamBusHallBypass,  STR16("Hall Bypass"),  0.0, 1);
    addRange(kParamBusSubBypass,   STR16("Sub Bypass"),   0.0, 1);

    addRange(kParamFilterOn,     STR16("Filter On"),     0.0, 1);
    addRange(kParamFilterType,   STR16("Filter Type"),   0.0, 7);
    addRange(kParamFilterCutoff, STR16("Filter Cutoff"), 0.92);
    addRange(kParamFilterRes,    STR16("Filter Res"),    0.15);

    addRange(kParamEqOn,       STR16("EQ On"),        0.0, 1);
    addRange(kParamEqLowFreq,  STR16("EQ Low Freq"),  0.22);
    addRange(kParamEqLowGain,  STR16("EQ Low Gain"),  0.50);
    addRange(kParamEqMidFreq,  STR16("EQ Mid Freq"),  0.23);
    addRange(kParamEqMidGain,  STR16("EQ Mid Gain"),  0.50);
    addRange(kParamEqHighFreq, STR16("EQ High Freq"), 0.37);
    addRange(kParamEqHighGain, STR16("EQ High Gain"), 0.50);

    addRange(kParamCompOn,      STR16("Comp On"),      0.0, 1);
    addRange(kParamCompThresh,  STR16("Comp Thresh"),  0.61);
    addRange(kParamCompRatio,   STR16("Comp Ratio"),   0.27);
    addRange(kParamCompAttack,  STR16("Comp Attack"),  0.12);
    addRange(kParamCompRelease, STR16("Comp Release"), 0.25);
    addRange(kParamCompKnee,    STR16("Comp Knee"),    0.33);
    addRange(kParamCompMakeup,  STR16("Comp Makeup"),  0.50);

    addRange(kParamSatOn,     STR16("Sat On"),     0.0, 1);
    addRange(kParamSatDrive,  STR16("Sat Drive"),  0.25);
    addRange(kParamSatWarmth, STR16("Sat Warmth"), 0.50);
    addRange(kParamSatMix,    STR16("Sat Mix"),    0.00);

    addRange(kParamDelayOn,       STR16("Delay On"),       0.0, 1);
    addRange(kParamDelayMode,     STR16("Delay Mode"),     0.0, 1);
    addRange(kParamDelayTime,     STR16("Delay Time"),     0.28);
    addRange(kParamDelayFeedback, STR16("Delay Feedback"), 0.36);
    addRange(kParamDelayTone,     STR16("Delay Tone"),     0.65);
    addRange(kParamDelayWidth,    STR16("Delay Width"),    0.70);
    addRange(kParamDelayMix,      STR16("Delay Mix"),      0.22);

    addRange(kParamRevOn,       STR16("Reverb On"),      0.0, 1);
    addRange(kParamRevMode,     STR16("Reverb Mode"),    1.0, 2);
    addRange(kParamRevPreDelay, STR16("Reverb PreDelay"),0.15);
    addRange(kParamRevDecay,    STR16("Reverb Decay"),   0.38);
    addRange(kParamRevSize,     STR16("Reverb Size"),    0.65);
    addRange(kParamRevLowCut,   STR16("Reverb LowCut"),  0.21);
    addRange(kParamRevHighCut,  STR16("Reverb HighCut"), 0.42);
    addRange(kParamRevMix,      STR16("Reverb Mix"),     0.30);

    addRange(kParamLimiterOn,     STR16("Output Limiter"),  1.0, 1);
    addRange(kParamMonoBass,      STR16("Mono Bass"),       1.0, 1);
    addRange(kParamVelCurve,       STR16("Velocity Curve"),  0.5, 2);
    addRange(kParamGlobalFakeMic, STR16("Global Fake Mic"), 1.0, 1);

    addRange(kParamPunishOn,     STR16("PUNISH On"),     1.0, 1);
    addRange(kParamPunishAmount, STR16("PUNISH Amount"), 0.30);
    addRange(kParamPunishMode,   STR16("PUNISH Mode"),   0.5, 2);

    addRange(kParamThemeIndex, STR16("UI Theme"), 0.0, 9);
    addRange(kParamUiZoom,     STR16("UI Zoom"),  1.0, 2);

    return kResultOk;
}

tresult PLUGIN_API WreckageController::terminate() { return EditControllerEx1::terminate(); }

tresult PLUGIN_API WreckageController::setComponentState(IBStream* state) {
    if (!state) return kResultFalse;
    IBStreamer s(state, kLittleEndian);
    int32 ver = 0;
    if (!s.readInt32(ver) || ver != 6) return kResultFalse;

    float f = 0.0f; int8 b = 0; int32 iv = 0;
    for (int i = 0; i < 4; ++i) {
        s.readFloat(f);
        float db = 20.0f * std::log10(std::max(1e-6f, f));
        setParamNormalized(kParamBusCloseGain + i, gainDbToNorm(db));
        s.readInt8(b); setParamNormalized(kParamBusCloseBypass + i, b ? 1.0 : 0.0);
    }
    s.readInt8(b); setParamNormalized(kParamGlobalFakeMic, b ? 1.0 : 0.0);

    s.readInt8(b); setParamNormalized(kParamFilterOn, b ? 1.0 : 0.0);
    s.readInt32(iv); setParamNormalized(kParamFilterType, iv / 7.0);
    s.readFloat(f); setParamNormalized(kParamFilterCutoff, std::clamp(std::log(std::max(20.0f, f) / 20.0f) / std::log(1000.0f), 0.0f, 1.0f));
    s.readFloat(f); setParamNormalized(kParamFilterRes, f);

    s.readInt8(b); setParamNormalized(kParamEqOn, b ? 1.0 : 0.0);
    s.readFloat(f); setParamNormalized(kParamEqLowFreq, (f - 30.0f) / 270.0f);
    s.readFloat(f); setParamNormalized(kParamEqLowGain, f / 24.0f + 0.5f);
    s.readFloat(f); setParamNormalized(kParamEqMidFreq, (f - 250.0f) / 3750.0f);
    s.readFloat(f); setParamNormalized(kParamEqMidGain, f / 24.0f + 0.5f);
    s.readFloat(f); setParamNormalized(kParamEqHighFreq, (f - 2500.0f) / 13500.0f);
    s.readFloat(f); setParamNormalized(kParamEqHighGain, f / 24.0f + 0.5f);

    s.readInt8(b); setParamNormalized(kParamCompOn, b ? 1.0 : 0.0);
    s.readFloat(f); setParamNormalized(kParamCompThresh, (f + 36.0f) / 36.0f);
    s.readFloat(f); setParamNormalized(kParamCompRatio, (f - 1.0f) / 11.0f);
    s.readFloat(f); setParamNormalized(kParamCompAttack, (f - 0.5f) / 79.5f);
    s.readFloat(f); setParamNormalized(kParamCompRelease, (f - 15.0f) / 385.0f);
    s.readFloat(f); setParamNormalized(kParamCompKnee, f / 12.0f);
    s.readFloat(f); setParamNormalized(kParamCompMakeup, f / 24.0f + 0.5f);

    s.readInt8(b); setParamNormalized(kParamSatOn, b ? 1.0 : 0.0);
    s.readFloat(f); setParamNormalized(kParamSatDrive, f);
    s.readFloat(f); setParamNormalized(kParamSatWarmth, f);
    s.readFloat(f); setParamNormalized(kParamSatMix, f);

    s.readInt8(b); setParamNormalized(kParamDelayOn, b ? 1.0 : 0.0);
    s.readInt32(iv); setParamNormalized(kParamDelayMode, iv ? 1.0 : 0.0);
    s.readFloat(f); setParamNormalized(kParamDelayTime, (f - 20.0f) / 780.0f);
    s.readFloat(f); setParamNormalized(kParamDelayFeedback, f / 0.88f);
    s.readFloat(f); setParamNormalized(kParamDelayTone, f);
    s.readFloat(f); setParamNormalized(kParamDelayWidth, f);
    s.readFloat(f); setParamNormalized(kParamDelayMix, f);

    s.readInt8(b); setParamNormalized(kParamRevOn, b ? 1.0 : 0.0);
    s.readInt32(iv); setParamNormalized(kParamRevMode, iv * 0.5);
    s.readFloat(f); setParamNormalized(kParamRevPreDelay, f / 120.0f);
    s.readFloat(f); setParamNormalized(kParamRevDecay, (f - 0.2f) / 5.8f);
    s.readFloat(f); setParamNormalized(kParamRevSize, f);
    s.readFloat(f); setParamNormalized(kParamRevLowCut, (f - 20.0f) / 480.0f);
    s.readFloat(f); setParamNormalized(kParamRevHighCut, (f - 1500.0f) / 16500.0f);
    s.readFloat(f); setParamNormalized(kParamRevMix, f);

    s.readInt8(b); setParamNormalized(kParamLimiterOn, b ? 1.0 : 0.0);
    s.readInt8(b); setParamNormalized(kParamMonoBass, b ? 1.0 : 0.0);
    s.readInt32(iv); setParamNormalized(kParamVelCurve, iv * 0.5);
    s.readInt8(b); setParamNormalized(kParamPunishOn, b ? 1.0 : 0.0);
    s.readFloat(f); setParamNormalized(kParamPunishAmount, f);
    s.readInt32(iv); setParamNormalized(kParamPunishMode, iv * 0.5);
    s.readInt32(iv); setParamNormalized(kParamThemeIndex, iv / 9.0);
    s.readInt32(iv); setParamNormalized(kParamUiZoom, iv * 0.5);
    return kResultOk;
}

IPlugView* PLUGIN_API WreckageController::createView(FIDString name) {
#ifdef _WIN32
    if (FIDStringsEqual(name, ViewType::kEditor)) return new WreckageWinView(this);
#endif
    return nullptr;
}

tresult PLUGIN_API WreckageController::notify(IMessage* message) {
    if (!message) return EditControllerEx1::notify(message);
    const char* mid = message->getMessageID();
    auto* attrs = message->getAttributes();
    if (!mid || !attrs) return EditControllerEx1::notify(message);

    if (strcmp(mid, "SlotUpdated") == 0) {
        int64 sl = 0, col = 0, mn = kBaseMidiNote, fr = 1, fv = 1, avl = 1;
        double sx = 0.0, sy = 0.0;
        attrs->getInt("slot", sl);
        attrs->getInt("color", col); attrs->getInt("midiNote", mn);
        attrs->getInt("fakeRR", fr); attrs->getInt("fakeVel", fv);
        attrs->getInt("activeVelLayers", avl);
        attrs->getFloat("x", sx); attrs->getFloat("y", sy);

        auto& m = slotMirror(static_cast<int>(sl));
        m.midiNote = static_cast<int>(mn);
        m.stageX = static_cast<float>(sx);
        m.stageY = static_cast<float>(sy);
        m.fakeRR = (fr != 0);
        m.fakeVel = (fv != 0);
        m.activeVelLayers = std::clamp(static_cast<int>(avl), 1, kMaxVelLayers);
        m.hasCustomColor = (col != 0);
        m.keyColor = static_cast<uint32_t>(col);

        const void* cPtr = nullptr; uint32 cSz = 0;
        if (attrs->getBinary("rrCounts", cPtr, cSz) == kResultOk && cPtr && cSz == sizeof(int) * kMaxVelLayers) {
            std::memcpy(m.rrCounts.data(), cPtr, cSz);
            for (int v = 0; v < kMaxVelLayers; ++v) {
                int cnt = std::clamp(m.rrCounts[v], 0, kMaxRoundRobins);
                if (cnt == 0) m.velLayers[v].rrs.clear();
                else if (static_cast<int>(m.velLayers[v].rrs.size()) != cnt) m.velLayers[v].rrs.resize(static_cast<size_t>(cnt));
            }
        }

        const void* ptr = nullptr; uint32 sz = 0;
        if (attrs->getBinary("nameUtf8", ptr, sz) == kResultOk && ptr && sz > 0)
            m.name.assign(static_cast<const char*>(ptr), sz);
        return kResultOk;
    }
    if (strcmp(mid, "WaveUpdated") == 0) {
        int64 sl = 0, vl = 0, rr = 0;
        double st = 0.0, en = 1.0, fi = 0.0, fo = 0.0;
        attrs->getInt("slot", sl); attrs->getInt("velLayer", vl); attrs->getInt("rr", rr);
        attrs->getFloat("start", st); attrs->getFloat("end", en);
        attrs->getFloat("fadeIn", fi); attrs->getFloat("fadeOut", fo);
        auto& m = slotMirror(static_cast<int>(sl));
        int vIdx = std::clamp(static_cast<int>(vl), 0, kMaxVelLayers - 1);
        int rIdx = static_cast<int>(rr);
        if (rIdx >= 0 && rIdx < kMaxRoundRobins) {
            auto& rrs = m.velLayers[vIdx].rrs;
            if (static_cast<int>(rrs.size()) <= rIdx) rrs.resize(static_cast<size_t>(rIdx + 1));
            auto& wi = rrs[static_cast<size_t>(rIdx)];
            wi.startNorm = static_cast<float>(st); wi.endNorm = static_cast<float>(en);
            wi.fadeInNorm = static_cast<float>(fi); wi.fadeOutNorm = static_cast<float>(fo);
            const void* ptr = nullptr; uint32 sz = 0;
            if (attrs->getBinary("peaks", ptr, sz) == kResultOk && ptr && sz == sizeof(float) * kWavePeakBins) {
                std::memcpy(wi.peaks.data(), ptr, sz);
            }
        }
        return kResultOk;
    }
    if (strcmp(mid, "Telemetry") == 0) {
        int64 v = 0, rb = 0, lo = 0, hi = 0, avl = -1, arr = -1;
        double cpu = 0.0, gr = 0.0, ol = 0.0, ph = -1.0;
        attrs->getInt("voices", v); attrs->getInt("ramBytes", rb);
        attrs->getFloat("cpuPct", cpu);
        attrs->getFloat("grDb", gr);
        attrs->getFloat("outLvl", ol);
        attrs->getFloat("playhead", ph);
        attrs->getInt("actVelLayer", avl);
        attrs->getInt("actRR", arr);
        attrs->getInt("notesLo", lo); attrs->getInt("notesHi", hi);
        liveVoices_ = static_cast<int>(v);
        liveRamBytes_ = static_cast<uint64_t>(rb);
        liveCpuPct_ = static_cast<float>(cpu);
        liveGrDb_ = static_cast<float>(gr);
        liveOutLevel_ = static_cast<float>(ol);
        livePlayheadNorm_ = static_cast<float>(ph);
        liveActiveVelLayer_ = static_cast<int>(avl);
        liveActiveRR_ = static_cast<int>(arr);
        notesLo_ = static_cast<uint64_t>(lo);
        notesHi_ = static_cast<uint64_t>(hi);
        return kResultOk;
    }
    if (strcmp(mid, "SaveDone") == 0) {
        int64 ok = 0; attrs->getInt("ok", ok);
        if (ok) saveFlashTicks_ = 30;
        return kResultOk;
    }
    return EditControllerEx1::notify(message);
}

void WreckageController::uiSetParam(ParamID id, double normVal, bool beginEnd) {
    normVal = std::clamp(normVal, 0.0, 1.0);
    if (beginEnd) beginEdit(id);
    setParamNormalized(id, normVal);
    performEdit(id, normVal);
    if (beginEnd) endEdit(id);
}

void WreckageController::uiTriggerNote(int midiNote, float vel, bool isOn) {
    if (auto* msg = allocateMessage()) {
        msg->setMessageID(isOn ? "NoteOn" : "NoteOff");
        msg->getAttributes()->setInt("note", midiNote);
        msg->getAttributes()->setFloat("vel", vel);
        sendMessage(msg); msg->release();
    }
}

void WreckageController::uiSelectSlot(int slotIdx, int velLayerIdx) {
    if (auto* msg = allocateMessage()) {
        msg->setMessageID("SelectSlot");
        msg->getAttributes()->setInt("slot", slotIdx);
        msg->getAttributes()->setInt("velLayer", velLayerIdx);
        sendMessage(msg); msg->release();
    }
}

void WreckageController::uiPanic() {
    if (auto* msg = allocateMessage()) { msg->setMessageID("Panic"); sendMessage(msg); msg->release(); }
}

void WreckageController::uiResetKit() {
    if (auto* msg = allocateMessage()) { msg->setMessageID("ResetKit"); sendMessage(msg); msg->release(); }
}

void WreckageController::uiClearSlot(int slotIdx) {
    if (auto* msg = allocateMessage()) {
        msg->setMessageID("ClearSlot");
        msg->getAttributes()->setInt("slot", slotIdx);
        sendMessage(msg); msg->release();
    }
}

void WreckageController::uiDeleteWaveItem(int slotIdx, int velLayerIdx, int rrIdx) {
    if (auto* msg = allocateMessage()) {
        msg->setMessageID("DeleteWaveItem");
        msg->getAttributes()->setInt("slot", slotIdx);
        msg->getAttributes()->setInt("velLayer", velLayerIdx);
        msg->getAttributes()->setInt("rr", rrIdx);
        sendMessage(msg); msg->release();
    }
}

void WreckageController::uiResetStage() {
    if (auto* msg = allocateMessage()) { msg->setMessageID("ResetStage"); sendMessage(msg); msg->release(); }
}

void WreckageController::uiSetStageXY(int slotIdx, float x, float y) {
    auto& m = slotMirror(slotIdx);
    m.stageX = std::clamp(x, -1.0f, 1.0f);
    m.stageY = std::clamp(y, 0.0f, 1.0f);
    if (auto* msg = allocateMessage()) {
        msg->setMessageID("SetStageXY");
        msg->getAttributes()->setInt("slot", slotIdx);
        msg->getAttributes()->setFloat("x", m.stageX);
        msg->getAttributes()->setFloat("y", m.stageY);
        sendMessage(msg); msg->release();
    }
}

void WreckageController::uiSetKeyColor(int slotIdx, uint32_t rgb) {
    auto& m = slotMirror(slotIdx);
    m.keyColor = rgb; m.hasCustomColor = (rgb != 0);
    if (auto* msg = allocateMessage()) {
        msg->setMessageID("SetKeyColor");
        msg->getAttributes()->setInt("slot", slotIdx);
        msg->getAttributes()->setInt("color", static_cast<int64>(rgb));
        sendMessage(msg); msg->release();
    }
}

void WreckageController::uiRenameSlot(int slotIdx, const std::string& newName) {
    if (newName.empty()) return;
    auto& m = slotMirror(slotIdx);
    m.name = newName;
    if (auto* msg = allocateMessage()) {
        msg->setMessageID("RenameSlot");
        msg->getAttributes()->setInt("slot", slotIdx);
        msg->getAttributes()->setBinary("nameUtf8", newName.data(), static_cast<uint32>(newName.size()));
        sendMessage(msg); msg->release();
    }
}

void WreckageController::uiSetSlotProp(int slotIdx, int prop, int ival, double fval) {
    auto& m = slotMirror(slotIdx);
    if (prop == 0) m.gainNorm = static_cast<float>(fval);
    else if (prop == 1) m.pitch = static_cast<float>(fval);
    else if (prop == 2) m.attack = static_cast<float>(fval);
    else if (prop == 3) m.release = static_cast<float>(fval);
    else if (prop == 5) m.oneShot = (ival != 0);
    else if (prop == 6) m.chokeGroup = ival;
    else if (prop == 7) m.reverse = (ival != 0);
    else if (prop == 8) m.purged = (ival != 0);
    else if (prop == 12) m.fakeRR = (ival != 0);
    else if (prop == 13) m.fakeVel = (ival != 0);

    if (auto* msg = allocateMessage()) {
        msg->setMessageID("SetSlotProp");
        msg->getAttributes()->setInt("slot", slotIdx);
        msg->getAttributes()->setInt("prop", prop);
        msg->getAttributes()->setInt("ival", ival);
        msg->getAttributes()->setFloat("fval", fval);
        sendMessage(msg); msg->release();
    }
}

void WreckageController::uiUpdateWaveEdit(int slotIdx, int velLayerIdx, int rrIdx) {
    auto& m = slotMirror(slotIdx);
    int vl = std::clamp(velLayerIdx, 0, kMaxVelLayers - 1);
    if (rrIdx < 0 || rrIdx >= static_cast<int>(m.velLayers[vl].rrs.size())) return;
    const auto& wi = m.velLayers[vl].rrs[static_cast<size_t>(rrIdx)];
    if (auto* msg = allocateMessage()) {
        msg->setMessageID("SetWaveEdit");
        msg->getAttributes()->setInt("slot", slotIdx);
        msg->getAttributes()->setInt("velLayer", vl);
        msg->getAttributes()->setInt("rr", rrIdx);
        msg->getAttributes()->setFloat("start", wi.startNorm);
        msg->getAttributes()->setFloat("end", wi.endNorm);
        msg->getAttributes()->setFloat("fadeIn", wi.fadeInNorm);
        msg->getAttributes()->setFloat("fadeOut", wi.fadeOutNorm);
        sendMessage(msg); msg->release();
    }
}

void WreckageController::uiRequestWave(int slotIdx, int velLayerIdx, int rrIdx) {
    if (auto* msg = allocateMessage()) {
        msg->setMessageID("RequestWave");
        msg->getAttributes()->setInt("slot", slotIdx);
        msg->getAttributes()->setInt("velLayer", velLayerIdx);
        msg->getAttributes()->setInt("rr", rrIdx);
        sendMessage(msg); msg->release();
    }
}

void WreckageController::uiLoadWavFile(int slotIdx, const std::wstring& widePath, int prefVelLayer) {
    std::string u8 = wideToUtf8(widePath);
    if (u8.empty()) return;
    if (auto* msg = allocateMessage()) {
        msg->setMessageID("LoadWav");
        msg->getAttributes()->setInt("slot", slotIdx);
        msg->getAttributes()->setInt("prefVelLayer", prefVelLayer);
        msg->getAttributes()->setBinary("pathUtf8", u8.data(), static_cast<uint32>(u8.size()));
        sendMessage(msg); msg->release();
    }
}

void WreckageController::uiExportNm6File(const std::wstring& widePath) {
    std::string u8 = wideToUtf8(widePath);
    if (u8.empty()) return;
    lastSavedNm6Path_ = widePath;
    if (auto* msg = allocateMessage()) {
        msg->setMessageID("ExportNm6");
        msg->getAttributes()->setBinary("pathUtf8", u8.data(), static_cast<uint32>(u8.size()));
        sendMessage(msg); msg->release();
    }
}

void WreckageController::uiLoadNm6File(const std::wstring& widePath) {
    std::string u8 = wideToUtf8(widePath);
    if (u8.empty()) return;
    lastSavedNm6Path_ = widePath;
    if (auto* msg = allocateMessage()) {
        msg->setMessageID("LoadNm6");
        msg->getAttributes()->setBinary("pathUtf8", u8.data(), static_cast<uint32>(u8.size()));
        sendMessage(msg); msg->release();
    }
}

} // namespace NoiseMu6ix

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
    static constexpr float kSpreadX[12] = {
        0.00f, -0.22f, 0.22f, -0.45f, 0.45f, -0.15f,
        0.15f, -0.68f, 0.68f, -0.35f, 0.35f,  0.00f
    };
    static constexpr float kSpreadY[12] = {
        0.14f, 0.22f, 0.22f, 0.34f, 0.34f, 0.44f,
        0.44f, 0.56f, 0.56f, 0.68f, 0.68f, 0.80f
    };
    for (int i = 0; i < kTotalSlots; ++i) {
        slots_[i].midiNote = kBaseMidiNote + i;
        slots_[i].name = "EMPTY";
        slots_[i].stageX = kSpreadX[i % 12];
        slots_[i].stageY = kSpreadY[i % 12];
    }
}

tresult PLUGIN_API WreckageController::initialize(FUnknown* context) {
    tresult res = EditControllerEx1::initialize(context);
    if (res != kResultOk) return res;

    auto addRange = [&](ParamID id, const TChar* title, double defVal, int32 stepCount = 0) {
        parameters.addParameter(title, nullptr, stepCount, defVal, ParameterInfo::kCanAutomate, id);
    };

    addRange(kParamBusCloseGain,   STR16("Bus Close"),        0.68);
    addRange(kParamBusRoomGain,    STR16("Bus Room"),         0.40);
    addRange(kParamBusHallGain,    STR16("Bus Hall"),         0.36);
    addRange(kParamBusSubGain,     STR16("Bus Sub"),          0.44);
    addRange(kParamBusCloseBypass, STR16("Close Bypass"),     0.0, 1);
    addRange(kParamBusRoomBypass,  STR16("Room Bypass"),      0.0, 1);
    addRange(kParamBusHallBypass,  STR16("Hall Bypass"),      0.0, 1);
    addRange(kParamBusSubBypass,   STR16("Sub Bypass"),       0.0, 1);

    addRange(kParamFilterOn,       STR16("Filter Enable"),    1.0, 1);
    addRange(kParamEqOn,           STR16("EQ Enable"),        1.0, 1);
    addRange(kParamCompOn,         STR16("Comp Enable"),      1.0, 1);
    addRange(kParamTransientOn,    STR16("Transient Enable"), 1.0, 1);
    addRange(kParamPunishOn,       STR16("PUNISH Enable"),    1.0, 1);

    addRange(kParamMasterHPF,      STR16("Master HPF"),       0.08);
    addRange(kParamMasterLPF,      STR16("Master LPF"),       0.98);
    addRange(kParamEqLow,          STR16("EQ Low"),           0.50);
    addRange(kParamEqMid,          STR16("EQ Mid"),           0.50);
    addRange(kParamEqHigh,         STR16("EQ High"),          0.50);
    addRange(kParamCompAmount,     STR16("Bus Compressor"),   0.20);
    addRange(kParamTransientPunch, STR16("Transient Punch"),  0.55);
    addRange(kParamStereoWidth,    STR16("Stereo Width"),     0.50);
    addRange(kParamMonoBass,       STR16("Mono Bass"),        1.0, 1);
    addRange(kParamVelCurve,       STR16("Velocity Curve"),   0.5, 2);
    addRange(kParamLimiterOn,      STR16("Output Limiter"),   1.0, 1);
    addRange(kParamMasterOutGain,  STR16("Master Output"),    0.76);

    addRange(kParamPunishAmount,   STR16("PUNISH Amount"),    0.30);
    addRange(kParamPunishMode,     STR16("PUNISH Mode"),      0.5, 2);

    addRange(kParamThemeIndex,       STR16("UI Theme"),         0.0, 9);
    addRange(kParamUiZoom,           STR16("UI Zoom"),          0.5, 2);
    addRange(kParamStageMasterDepth, STR16("Stage Depth"),      0.66);
    addRange(kParamSmartIntensity,   STR16("Smart Intensity"),  0.80);

    return kResultOk;
}

tresult PLUGIN_API WreckageController::terminate() {
    return EditControllerEx1::terminate();
}

tresult PLUGIN_API WreckageController::setComponentState(IBStream* state) {
    if (!state) return kResultFalse;
    IBStreamer s(state, kLittleEndian);
    int32 ver = 0;
    if (!s.readInt32(ver) || ver != 2) return kResultFalse;

    float f = 0.0f; int8 b = 0; int32 iv = 0;
    for (int i = 0; i < 4; ++i) {
        s.readFloat(f); setParamNormalized(kParamBusCloseGain + i, std::clamp(f / 1.25f, 0.0f, 1.0f));
        s.readInt8(b);  setParamNormalized(kParamBusCloseBypass + i, b ? 1.0 : 0.0);
    }
    s.readInt8(b); setParamNormalized(kParamFilterOn, b ? 1.0 : 0.0);
    s.readInt8(b); setParamNormalized(kParamEqOn, b ? 1.0 : 0.0);
    s.readInt8(b); setParamNormalized(kParamCompOn, b ? 1.0 : 0.0);
    s.readInt8(b); setParamNormalized(kParamTransientOn, b ? 1.0 : 0.0);
    s.readInt8(b); setParamNormalized(kParamPunishOn, b ? 1.0 : 0.0);

    s.readFloat(f); setParamNormalized(kParamMasterHPF, std::clamp(std::log(std::max(20.0f, f) / 20.0f) / std::log(20.0f), 0.0f, 1.0f));
    s.readFloat(f); setParamNormalized(kParamMasterLPF, std::clamp(std::log(std::max(2000.0f, f) / 2000.0f) / std::log(10.0f), 0.0f, 1.0f));
    s.readFloat(f); setParamNormalized(kParamEqLow,  std::clamp(f / 24.0f + 0.5f, 0.0f, 1.0f));
    s.readFloat(f); setParamNormalized(kParamEqMid,  std::clamp(f / 24.0f + 0.5f, 0.0f, 1.0f));
    s.readFloat(f); setParamNormalized(kParamEqHigh, std::clamp(f / 24.0f + 0.5f, 0.0f, 1.0f));
    s.readFloat(f); setParamNormalized(kParamCompAmount, f);
    s.readFloat(f); setParamNormalized(kParamTransientPunch, f);
    s.readFloat(f); setParamNormalized(kParamStereoWidth, f);
    s.readInt8(b);  setParamNormalized(kParamMonoBass, b ? 1.0 : 0.0);
    s.readInt8(b);  setParamNormalized(kParamLimiterOn, b ? 1.0 : 0.0);
    s.readInt32(iv); setParamNormalized(kParamVelCurve, iv * 0.5);
    s.readFloat(f); setParamNormalized(kParamMasterOutGain, std::clamp(f / 1.2f, 0.0f, 1.0f));
    s.readFloat(f); setParamNormalized(kParamPunishAmount, f);
    s.readInt32(iv); setParamNormalized(kParamPunishMode, iv * 0.5);
    s.readInt32(iv); setParamNormalized(kParamThemeIndex, iv / 9.0);
    s.readInt32(iv); setParamNormalized(kParamUiZoom, iv * 0.5);

    for (int i = 0; i < kTotalSlots; ++i) {
        auto& sl = slots_[i];
        int32 note = kBaseMidiNote + i; uint32 col = 0;
        int8 hasCol = 0, os = 1, rev = 0, prg = 0;
        int32 cg = 0, rrM = 0, velM = 0, micM = 0;
        float g = 0.85f, pt = 0.0f, tl = 0.0f, th = 0.0f, at = 0.0f, rl = 600.0f, xf = 0.0f, sx = 0.0f, sy = 0.25f;
        s.readInt32(note); s.readInt32u(col); s.readInt8(hasCol);
        s.readFloat(g); s.readFloat(pt); s.readFloat(tl); s.readFloat(th);
        s.readFloat(at); s.readFloat(rl); s.readFloat(xf);
        s.readFloat(sx); s.readFloat(sy);
        s.readInt8(os); s.readInt32(cg); s.readInt8(rev); s.readInt8(prg);
        s.readInt32(rrM); s.readInt32(velM); s.readInt32(micM);
        sl.midiNote = note; sl.keyColor = col; sl.hasCustomColor = (hasCol != 0);
        sl.gain = std::clamp(g / 1.5f, 0.0f, 1.0f);
        sl.pitch = std::clamp(pt / 48.0f + 0.5f, 0.0f, 1.0f);
        sl.toneLow = std::clamp(tl / 24.0f + 0.5f, 0.0f, 1.0f);
        sl.toneHigh = std::clamp(th / 24.0f + 0.5f, 0.0f, 1.0f);
        sl.attack = std::clamp(at / 200.0f, 0.0f, 1.0f);
        sl.release = std::clamp((rl - 20.0f) / 3980.0f, 0.0f, 1.0f);
        sl.xfade = xf; sl.stageX = sx; sl.stageY = sy;
        sl.oneShot = (os != 0); sl.chokeGroup = cg; sl.reverse = (rev != 0); sl.purged = (prg != 0);
        sl.rrMode = rrM; sl.velMode = velM; sl.micMode = micM;
    }
    return kResultOk;
}

IPlugView* PLUGIN_API WreckageController::createView(FIDString name) {
#ifdef _WIN32
    if (FIDStringsEqual(name, ViewType::kEditor)) {
        return new WreckageWinView(this);
    }
#endif
    return nullptr;
}

tresult PLUGIN_API WreckageController::notify(IMessage* message) {
    if (!message) return EditControllerEx1::notify(message);
    const char* mid = message->getMessageID();
    auto* attrs = message->getAttributes();
    if (!mid || !attrs) return EditControllerEx1::notify(message);

    if (strcmp(mid, "SlotUpdated") == 0) {
        int64 sl = 0, vc = 0, rc = 0, col = 0, mn = kBaseMidiNote;
        double sx = 0.0, sy = 0.25;
        attrs->getInt("slot", sl);
        attrs->getInt("velCount", vc);
        attrs->getInt("rrCount", rc);
        attrs->getInt("color", col);
        attrs->getInt("midiNote", mn);
        attrs->getFloat("x", sx);
        attrs->getFloat("y", sy);
        auto& m = slotMirror(static_cast<int>(sl));
        m.velCount = static_cast<int>(vc);
        m.rrCount  = static_cast<int>(rc);
        m.midiNote = static_cast<int>(mn);
        m.stageX   = static_cast<float>(sx);
        m.stageY   = static_cast<float>(sy);
        if (col != 0) { m.keyColor = static_cast<uint32_t>(col); m.hasCustomColor = true; }
        const void* ptr = nullptr; uint32 sz = 0;
        if (attrs->getBinary("nameUtf8", ptr, sz) == kResultOk && ptr && sz > 0) {
            m.name.assign(static_cast<const char*>(ptr), sz);
        }
        return kResultOk;
    }
    if (strcmp(mid, "Telemetry") == 0) {
        int64 v = 0, rb = 0, lo = 0, hi = 0;
        attrs->getInt("voices", v);
        attrs->getInt("ramBytes", rb);
        attrs->getInt("notesLo", lo);
        attrs->getInt("notesHi", hi);
        liveVoices_ = static_cast<int>(v);
        liveRamBytes_ = static_cast<uint64_t>(rb);
        notesLo_ = static_cast<uint64_t>(lo);
        notesHi_ = static_cast<uint64_t>(hi);
        return kResultOk;
    }
    if (strcmp(mid, "SaveDone") == 0) {
        int64 ok = 0;
        attrs->getInt("ok", ok);
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
        sendMessage(msg);
        msg->release();
    }
}

void WreckageController::uiPanic() {
    if (auto* msg = allocateMessage()) {
        msg->setMessageID("Panic");
        sendMessage(msg);
        msg->release();
    }
}

void WreckageController::uiResetStage() {
    if (auto* msg = allocateMessage()) {
        msg->setMessageID("ResetStage");
        sendMessage(msg);
        msg->release();
    }
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
        sendMessage(msg);
        msg->release();
    }
}

void WreckageController::uiSetKeyColor(int slotIdx, uint32_t rgb) {
    auto& m = slotMirror(slotIdx);
    m.keyColor = rgb;
    m.hasCustomColor = (rgb != 0);
    if (auto* msg = allocateMessage()) {
        msg->setMessageID("SetKeyColor");
        msg->getAttributes()->setInt("slot", slotIdx);
        msg->getAttributes()->setInt("color", static_cast<int64>(rgb));
        sendMessage(msg);
        msg->release();
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
        sendMessage(msg);
        msg->release();
    }
}

void WreckageController::uiCycleSmartMode(int slotIdx, int which) {
    auto& m = slotMirror(slotIdx);
    int next = 0;
    if (which == 0) { m.rrMode = (m.rrMode + 1) % 3; next = m.rrMode; }
    else if (which == 1) { m.velMode = (m.velMode + 1) % 3; next = m.velMode; }
    else { m.micMode = (m.micMode + 1) % 3; next = m.micMode; }
    if (auto* msg = allocateMessage()) {
        msg->setMessageID("SetSmartMode");
        msg->getAttributes()->setInt("slot", slotIdx);
        msg->getAttributes()->setInt("which", which);
        msg->getAttributes()->setInt("mode", next);
        sendMessage(msg);
        msg->release();
    }
}

void WreckageController::uiSetSlotProp(int slotIdx, int prop, int ival, double fval) {
    auto& m = slotMirror(slotIdx);
    if (prop == 0) m.gain = static_cast<float>(fval);
    else if (prop == 1) m.pitch = static_cast<float>(fval);
    else if (prop == 2) m.attack = static_cast<float>(fval);
    else if (prop == 3) m.release = static_cast<float>(fval);
    else if (prop == 4) m.xfade = static_cast<float>(fval);
    else if (prop == 5) m.oneShot = (ival != 0);
    else if (prop == 6) m.chokeGroup = ival;
    else if (prop == 7) m.reverse = (ival != 0);
    else if (prop == 8) m.purged = (ival != 0);
    else if (prop == 9) m.midiNote = ival;
    else if (prop == 10) m.toneLow = static_cast<float>(fval);
    else if (prop == 11) m.toneHigh = static_cast<float>(fval);

    if (auto* msg = allocateMessage()) {
        msg->setMessageID("SetSlotProp");
        msg->getAttributes()->setInt("slot", slotIdx);
        msg->getAttributes()->setInt("prop", prop);
        msg->getAttributes()->setInt("ival", ival);
        msg->getAttributes()->setFloat("fval", fval);
        sendMessage(msg);
        msg->release();
    }
}

void WreckageController::uiLoadWavFile(int slotIdx, const std::wstring& widePath) {
    std::string u8 = wideToUtf8(widePath);
    if (u8.empty()) return;
    if (auto* msg = allocateMessage()) {
        msg->setMessageID("LoadWav");
        msg->getAttributes()->setInt("slot", slotIdx);
        msg->getAttributes()->setBinary("pathUtf8", u8.data(), static_cast<uint32>(u8.size()));
        sendMessage(msg);
        msg->release();
    }
}

void WreckageController::uiExportNm6File(const std::wstring& widePath) {
    std::string u8 = wideToUtf8(widePath);
    if (u8.empty()) return;
    lastSavedNm6Path_ = widePath;
    if (auto* msg = allocateMessage()) {
        msg->setMessageID("ExportNm6");
        msg->getAttributes()->setBinary("pathUtf8", u8.data(), static_cast<uint32>(u8.size()));
        sendMessage(msg);
        msg->release();
    }
}

void WreckageController::uiLoadNm6File(const std::wstring& widePath) {
    std::string u8 = wideToUtf8(widePath);
    if (u8.empty()) return;
    lastSavedNm6Path_ = widePath;
    if (auto* msg = allocateMessage()) {
        msg->setMessageID("LoadNm6");
        msg->getAttributes()->setBinary("pathUtf8", u8.data(), static_cast<uint32>(u8.size()));
        sendMessage(msg);
        msg->release();
    }
}

} // namespace NoiseMu6ix

#pragma once
#include "public.sdk/source/vst/vsteditcontroller.h"
#include "../core/wreckage_params.h"
#include "../core/wreckage_themes.h"
#include <array>
#include <cstdint>
#include <string>

namespace NoiseMu6ix {

struct UiSlotMirror {
    std::string name = "EMPTY";
    int midiNote = kBaseMidiNote;
    uint32_t keyColor = 0;
    bool hasCustomColor = false;
    float gain = 0.56f;
    float pitch = 0.5f;
    float toneLow = 0.5f;
    float toneHigh = 0.5f;
    float attack = 0.0f;
    float release = 0.15f;
    float xfade = 0.0f;
    float stageX = 0.0f;
    float stageY = 0.25f;
    bool oneShot = true;
    int chokeGroup = 0;
    bool reverse = false;
    bool purged = false;
    int rrMode = 0;
    int velMode = 0;
    int micMode = 0;
    int velCount = 0;
    int rrCount = 0;
};

class WreckageController : public Steinberg::Vst::EditControllerEx1 {
public:
    WreckageController();
    ~WreckageController() override = default;

    static Steinberg::FUnknown* createInstance(void*) {
        return static_cast<Steinberg::Vst::IEditController*>(new WreckageController());
    }

    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API terminate() SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setComponentState(Steinberg::IBStream* state) SMTG_OVERRIDE;
    Steinberg::IPlugView* PLUGIN_API createView(Steinberg::FIDString name) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API notify(Steinberg::Vst::IMessage* message) SMTG_OVERRIDE;

    void uiSetParam(Steinberg::Vst::ParamID id, double normVal, bool beginEnd = true);
    void uiTriggerNote(int midiNote, float vel, bool isOn);
    void uiPanic();
    void uiResetStage();
    void uiSetStageXY(int slotIdx, float x, float y);
    void uiSetKeyColor(int slotIdx, uint32_t rgb);
    void uiRenameSlot(int slotIdx, const std::string& newName);
    void uiCycleSmartMode(int slotIdx, int which);
    void uiSetSlotProp(int slotIdx, int prop, int ival, double fval);
    void uiLoadWavFile(int slotIdx, const std::wstring& widePath);
    void uiExportNm6File(const std::wstring& widePath);
    void uiLoadNm6File(const std::wstring& widePath);

    UiSlotMirror& slotMirror(int idx) { return slots_[idx % kTotalSlots]; }
    std::wstring& rootLibraryDir() { return rootLibraryDir_; }
    std::wstring& lastSavedNm6Path() { return lastSavedNm6Path_; }
    std::string& activePresetName() { return activePresetName_; }

    int liveVoices() const { return liveVoices_; }
    uint64_t liveRamBytes() const { return liveRamBytes_; }
    bool isMidiNoteLive(int note) const {
        if (note < 0 || note >= 128) return false;
        uint64_t mask = (note < 64) ? notesLo_ : notesHi_;
        return ((mask >> (note & 63)) & 1ULL) != 0;
    }
    int saveFlashTicks() const { return saveFlashTicks_; }
    void decSaveFlash() { if (saveFlashTicks_ > 0) --saveFlashTicks_; }

private:
    std::array<UiSlotMirror, kTotalSlots> slots_;
    std::wstring rootLibraryDir_;
    std::wstring lastSavedNm6Path_;
    std::string activePresetName_ = "EMPTY KIT (LOAD WAV)";
    int liveVoices_ = 0;
    uint64_t liveRamBytes_ = 0;
    uint64_t notesLo_ = 0;
    uint64_t notesHi_ = 0;
    int saveFlashTicks_ = 0;
};

} // namespace NoiseMu6ix

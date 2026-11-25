#pragma once
#include "public.sdk/source/vst/vsteditcontroller.h"
#include "../core/wreckage_params.h"
#include "../core/wreckage_themes.h"
#include <array>
#include <string>

namespace NoiseMu6ix {

struct UiSlotMirror {
    std::string name = "EMPTY";
    int midiNote = kBaseMidiNote;
    uint32_t keyColor = 0;
    bool hasCustomColor = false;
    float gain = 0.56f;
    float pitch = 0.5f;
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
    void uiSetStageXY(int slotIdx, float x, float y);
    void uiSetKeyColor(int slotIdx, uint32_t rgb);
    void uiCycleSmartMode(int slotIdx, int which);
    void uiSetSlotProp(int slotIdx, int prop, int ival, double fval);
    void uiLoadWavFile(int slotIdx, const std::wstring& widePath);
    void uiExportNm6File(const std::wstring& widePath);

    UiSlotMirror& slotMirror(int idx) { return slots_[idx % kTotalSlots]; }
    std::wstring& rootLibraryDir() { return rootLibraryDir_; }
    std::string& activePresetName() { return activePresetName_; }

private:
    std::array<UiSlotMirror, kTotalSlots> slots_;
    std::wstring rootLibraryDir_;
    std::string activePresetName_ = "BUILT-IN EPIC KIT";
};

} // namespace NoiseMu6ix

#pragma once
#include "public.sdk/source/vst/vstaudioeffect.h"
#include "../core/wreckage_engine.h"
#include <string>

namespace NoiseMu6ix {

class WreckageProcessor : public Steinberg::Vst::AudioEffect {
public:
    WreckageProcessor();
    ~WreckageProcessor() override = default;

    static Steinberg::FUnknown* createInstance(void*) {
        return static_cast<Steinberg::Vst::IAudioProcessor*>(new WreckageProcessor());
    }

    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API terminate() SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setActive(Steinberg::TBool state) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setupProcessing(Steinberg::Vst::ProcessSetup& newSetup) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API canProcessSampleSize(Steinberg::int32 symbolicSampleSize) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setBusArrangements(Steinberg::Vst::SpeakerArrangement* inputs, Steinberg::int32 numIns,
                                                     Steinberg::Vst::SpeakerArrangement* outputs, Steinberg::int32 numOuts) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API process(Steinberg::Vst::ProcessData& data) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream* state) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream* state) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API notify(Steinberg::Vst::IMessage* message) SMTG_OVERRIDE;

private:
    void applyNormalizedParam(Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue val);
    void broadcastSlotSummaryToUi(int slotIdx);
    void broadcastWaveDataToUi(int slotIdx, int velLayerIdx, int rrIdx, bool focus = false);
    void broadcastTelemetryToUi();
    void broadcastPresetPathToUi();
    void broadcastParamsSyncToUi();

    WreckageEngine engine_;
    double sampleRate_ = 44100.0;
    float cpuLoadPct_ = 0.0f;
    int selectedUiSlot_ = 0;
    int telemetrySampleAccum_ = 0;
    bool sentIdleTelemetry_ = false;
    int lastMidiNote_ = -1;
    uint32_t midiNoteSeq_ = 0;
    uint64_t midiHitPulseLo_ = 0;
    uint64_t midiHitPulseHi_ = 0;
    std::string lastNm6PathUtf8_;
};

} // namespace NoiseMu6ix

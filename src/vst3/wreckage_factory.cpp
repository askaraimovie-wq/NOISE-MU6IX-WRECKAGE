#include "public.sdk/source/main/pluginfactory.h"
#include "wreckage_uids.h"
#include "wreckage_processor.h"
#include "wreckage_controller.h"
#include "version.h"

using namespace Steinberg;
using namespace Steinberg::Vst;
using namespace NoiseMu6ix;

BEGIN_FACTORY_DEF(WRECKAGE_VENDOR_STR, WRECKAGE_URL_STR, WRECKAGE_EMAIL_STR)

    DEF_CLASS2(INLINE_UID_FROM_FUID(kWreckageProcessorUID),
               PClassInfo::kManyInstances,
               kVstAudioEffectClass,
               WRECKAGE_PLUGIN_NAME,
               Vst::kDistributable,
               Vst::PlugType::kInstrumentDrum,
               WRECKAGE_VERSION_STR,
               kVstVersionString,
               WreckageProcessor::createInstance)

    DEF_CLASS2(INLINE_UID_FROM_FUID(kWreckageControllerUID),
               PClassInfo::kManyInstances,
               kVstComponentControllerClass,
               WRECKAGE_PLUGIN_NAME " Controller",
               0,
               "",
               WRECKAGE_VERSION_STR,
               kVstVersionString,
               WreckageController::createInstance)

END_FACTORY

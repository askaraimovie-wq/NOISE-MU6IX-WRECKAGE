#pragma once
#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/vsttypes.h"

#ifndef WRECKAGE_IS_PLAYER
#define WRECKAGE_IS_PLAYER 0
#endif

#if WRECKAGE_IS_PLAYER
#define WRECKAGE_PLUGIN_NAME "WRECKAGE Player"
#else
#define WRECKAGE_PLUGIN_NAME "WRECKAGE"
#endif

namespace NoiseMu6ix {

#if WRECKAGE_IS_PLAYER
// Distinct UIDs for WRECKAGE Player so it runs side-by-side with WRECKAGE in FL Studio
static const Steinberg::FUID kWreckageProcessorUID (0x4E4D3658, 0x57524B50, 0x504C4159, 0x00010000);
static const Steinberg::FUID kWreckageControllerUID(0x4E4D3658, 0x57524B50, 0x4354524C, 0x00010000);
#else
// Primary UIDs for WRECKAGE (Full Creator Edition)
static const Steinberg::FUID kWreckageProcessorUID (0x4E4D3658, 0x57524B45, 0x50524F43, 0x00010000);
static const Steinberg::FUID kWreckageControllerUID(0x4E4D3658, 0x57524B45, 0x4354524C, 0x00010000);
#endif

} // namespace NoiseMu6ix

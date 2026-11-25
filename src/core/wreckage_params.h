#pragma once
#include <cstdint>

namespace NoiseMu6ix {

enum ParamIDs : uint32_t {
    // 5-Bus Mixer Horizontal Faders (0.0 .. 1.0)
    kParamBusCloseGain   = 100,
    kParamBusRoomGain    = 101,
    kParamBusHallGain    = 102,
    kParamBusLfeGain     = 103,
    kParamBusCrushGain   = 104,

    // 5-Bus Individual Zero-CPU Bypass (0 = Active, 1 = Bypassed)
    kParamBusCloseBypass = 110,
    kParamBusRoomBypass  = 111,
    kParamBusHallBypass  = 112,
    kParamBusLfeBypass   = 113,
    kParamBusCrushBypass = 114,

    // Master FX Rack
    kParamMasterHPF      = 200, // 20 Hz .. 400 Hz
    kParamMasterLPF      = 201, // 2 kHz .. 20 kHz
    kParamEqLow          = 202, // -12 .. +12 dB
    kParamEqMid          = 203, // -12 .. +12 dB
    kParamEqHigh         = 204, // -12 .. +12 dB
    kParamCompAmount     = 205, // 0 .. 1
    kParamTransientPunch = 206, // 0 .. 1 (0.5 neutral)
    kParamStereoWidth    = 207, // 0 .. 1 (0.5 = 100%, 0 = Mono, 1 = 150% Wide)
    kParamMonoBass       = 208, // 0 = Off, 1 = On (< 120 Hz Mono)
    kParamVelCurve       = 209, // 0 = Soft, 0.5 = Linear, 1.0 = Hard
    kParamLimiterOn      = 210, // 0 = Off, 1 = On (-0.3 dB)
    kParamMasterOutGain  = 211, // 0 .. 1

    // Signature PUNISH Bar
    kParamPunishAmount   = 300, // 0 .. 1
    kParamPunishMode     = 301, // 0 = Gently Now, 0.5 = Hurt Me Plenty, 1.0 = Nightmare

    // UI Preferences (saved in state)
    kParamThemeIndex     = 400, // 0..9 normalized
    kParamUiZoom         = 401, // 0 = 50%, 0.5 = 75%, 1.0 = 100%

    // Selected Slot Quick Controls (automatable for live tweaking)
    kParamStageMasterDepth = 500, // Global stage depth multiplier
    kParamSmartIntensity   = 501  // Global Smart Hybrid Fake intensity
};

enum class SmartMode : uint8_t {
    Auto = 0, // 1-hit = Full Fake, 2-4 = Hybrid Boost, 5+ = Pure Real
    ForceOn = 1,
    ForceOff = 2
};

enum class MicBus : uint8_t {
    Close = 0,
    Room  = 1,
    Hall  = 2,
    LFE   = 3,
    Crush = 4,
    Count = 5
};

enum class PunishMode : uint8_t {
    GentlyNow    = 0,
    HurtMePlenty = 1,
    Nightmare    = 2
};

enum class VelocityCurve : uint8_t {
    Soft   = 0,
    Linear = 1,
    Hard   = 2
};

constexpr int kNumBanks        = 3;
constexpr int kSlotsPerBank    = 12;
constexpr int kTotalSlots      = kNumBanks * kSlotsPerBank; // 36 slots (C1..B3)
constexpr int kBaseMidiNote    = 36; // C1
constexpr int kMaxVoices       = 128;
constexpr int kBaseUiSize      = 840; // 1:1 Square Base (50%=420, 75%=630, 100%=840)

} // namespace NoiseMu6ix

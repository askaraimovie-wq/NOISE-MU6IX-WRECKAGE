#pragma once
#include <cstdint>

namespace NoiseMu6ix {

enum ParamIDs : uint32_t {
    // 4-Bus Mixer Horizontal Faders (0.0 .. 1.0)
    kParamBusCloseGain   = 100,
    kParamBusRoomGain    = 101,
    kParamBusHallGain    = 102,
    kParamBusSubGain     = 103,

    // 4-Bus Individual Zero-CPU Bypass (0 = Active, 1 = Bypassed)
    kParamBusCloseBypass = 110,
    kParamBusRoomBypass  = 111,
    kParamBusHallBypass  = 112,
    kParamBusSubBypass   = 113,

    // Master FX Rack + Individual On/Off Bypasses
    kParamMasterHPF      = 200,
    kParamMasterLPF      = 201,
    kParamEqLow          = 202,
    kParamEqMid          = 203,
    kParamEqHigh         = 204,
    kParamCompAmount     = 205,
    kParamTransientPunch = 206,
    kParamStereoWidth    = 207,
    kParamMonoBass       = 208,
    kParamVelCurve       = 209,
    kParamLimiterOn      = 210,
    kParamMasterOutGain  = 211,

    kParamFilterOn       = 220,
    kParamEqOn           = 221,
    kParamCompOn         = 222,
    kParamTransientOn    = 223,
    kParamPunishOn       = 224,

    // Signature PUNISH Bar
    kParamPunishAmount   = 300,
    kParamPunishMode     = 301,

    // UI Preferences
    kParamThemeIndex     = 400,
    kParamUiZoom         = 401,

    // Global Quick Controls
    kParamStageMasterDepth = 500,
    kParamSmartIntensity   = 501
};

enum class SmartMode : uint8_t {
    Auto = 0,
    ForceOn = 1,
    ForceOff = 2
};

enum class MicBus : uint8_t {
    Close = 0,
    Room  = 1,
    Hall  = 2,
    Sub   = 3,
    Count = 4
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
constexpr int kTotalSlots      = kNumBanks * kSlotsPerBank; // 36 slots (C3..B5)
constexpr int kBaseMidiNote    = 48; // C3 (Bank 1: C3-B3, Bank 2: C4-B4, Bank 3: C5-B5)
constexpr int kMaxVoices       = 128;
constexpr int kBaseUiSize      = 840;

} // namespace NoiseMu6ix

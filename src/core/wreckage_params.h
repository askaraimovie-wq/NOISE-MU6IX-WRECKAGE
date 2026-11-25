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

    // Filter
    kParamFilterOn       = 200,
    kParamFilterType     = 201, // 0..7: LP24, LP12, HP24, HP12, BP12, BP24, Peak, Notch
    kParamFilterCutoff   = 202,
    kParamFilterRes      = 203,

    // 3-Band EQ (Pro-Q style)
    kParamEqOn           = 210,
    kParamEqLowFreq      = 211,
    kParamEqLowGain      = 212,
    kParamEqMidFreq      = 213,
    kParamEqMidGain      = 214,
    kParamEqHighFreq     = 215,
    kParamEqHighGain     = 216,

    // Compressor (Pro-C style)
    kParamCompOn         = 220,
    kParamCompThresh     = 221,
    kParamCompRatio      = 222,
    kParamCompAttack     = 223,
    kParamCompRelease    = 224,
    kParamCompKnee       = 225,
    kParamCompMakeup     = 226,

    // Saturator (Saturn style — default Mix = 0.0 / 0% Dry)
    kParamSatOn          = 230,
    kParamSatDrive       = 231,
    kParamSatWarmth      = 232,
    kParamSatMix         = 233,

    // Delay (ValhallaDelay style)
    kParamDelayOn        = 240,
    kParamDelayMode      = 241,
    kParamDelayTime      = 242,
    kParamDelayFeedback  = 243,
    kParamDelayTone      = 244,
    kParamDelayWidth     = 245,
    kParamDelayMix       = 246,

    // Reverb (ValhallaVintageVerb 3D Space style)
    kParamRevOn          = 250,
    kParamRevMode        = 251,
    kParamRevPreDelay    = 252,
    kParamRevDecay       = 253,
    kParamRevSize        = 254,
    kParamRevLowCut      = 255,
    kParamRevHighCut     = 256,
    kParamRevMix         = 257,

    // Global Master & Global Fake Engines
    kParamLimiterOn      = 260,
    kParamMonoBass       = 261,
    kParamVelCurve       = 262,
    kParamGlobalFakeRR   = 263,
    kParamGlobalFakeVel  = 264,
    kParamGlobalFakeMic  = 265,

    // Signature PUNISH Bar
    kParamPunishOn       = 300,
    kParamPunishAmount   = 301,
    kParamPunishMode     = 302,

    // UI Preferences
    kParamThemeIndex     = 400, // Default 0 = PAPER SKETCH
    kParamUiZoom         = 401  // Default 2 = 100%
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
constexpr int kBaseMidiNote    = 48; // C3
constexpr int kMaxVoices       = 128;
constexpr int kBaseUiSize      = 840;
constexpr int kWavePeakBins    = 256;

} // namespace NoiseMu6ix

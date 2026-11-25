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

    // Master FX Rack (FabFilter & Valhalla Inspired Modules)
    // Filter
    kParamFilterOn       = 200,
    kParamFilterType     = 201, // 0..7: LP24, LP12, HP24, HP12, BP12, BP24, Peak, Notch
    kParamFilterCutoff   = 202, // 20 Hz .. 20 kHz
    kParamFilterRes      = 203, // 0.0 .. 1.0

    // 3-Band EQ (Pro-Q style)
    kParamEqOn           = 210,
    kParamEqLowFreq      = 211, // 30 .. 300 Hz
    kParamEqLowGain      = 212, // -12 .. +12 dB
    kParamEqMidFreq      = 213, // 250 .. 4000 Hz
    kParamEqMidGain      = 214, // -12 .. +12 dB
    kParamEqHighFreq     = 215, // 2500 .. 16000 Hz
    kParamEqHighGain     = 216, // -12 .. +12 dB

    // Compressor (Pro-C style)
    kParamCompOn         = 220,
    kParamCompThresh     = 221, // -36 .. 0 dB
    kParamCompRatio      = 222, // 1:1 .. 12:1
    kParamCompAttack     = 223, // 0.5 .. 80 ms
    kParamCompRelease    = 224, // 15 .. 400 ms
    kParamCompKnee       = 225, // 0 .. 12 dB (Shoulder)
    kParamCompMakeup     = 226, // 0 .. +15 dB

    // Saturator (Saturn style)
    kParamSatOn          = 230,
    kParamSatDrive       = 231, // 0 .. 100%
    kParamSatWarmth      = 232, // 0 .. 100% (Tilt/Tone)
    kParamSatMix         = 233, // 0 .. 100%

    // Delay (ValhallaDelay style)
    kParamDelayOn        = 240,
    kParamDelayMode      = 241, // 0: Modern, 1: Vintage
    kParamDelayTime      = 242, // 20 .. 800 ms
    kParamDelayFeedback  = 243, // 0 .. 88%
    kParamDelayTone      = 244, // LP/HP damping
    kParamDelayWidth     = 245, // Ping-pong / 3D stereo spread
    kParamDelayMix       = 246, // 0 .. 100%

    // Reverb (ValhallaVintageVerb 3D Space style)
    kParamRevOn          = 250,
    kParamRevMode        = 251, // 0: Room, 1: Plate, 2: Hall
    kParamRevPreDelay    = 252, // 0 .. 120 ms
    kParamRevDecay       = 253, // 0.2 .. 6.0 s
    kParamRevSize        = 254, // 20 .. 100%
    kParamRevLowCut      = 255, // 20 .. 500 Hz
    kParamRevHighCut     = 256, // 1500 .. 18000 Hz
    kParamRevMix         = 257, // 0 .. 100%

    // Global Master & Global Fake Engines
    kParamLimiterOn      = 260,
    kParamMonoBass       = 261,
    kParamVelCurve       = 262, // 0: Soft, 1: Linear, 2: Hard
    kParamGlobalFakeRR   = 263, // 0: OFF, 1: ON
    kParamGlobalFakeVel  = 264, // 0: OFF, 1: ON
    kParamGlobalFakeMic  = 265, // 0: OFF, 1: ON

    // Signature PUNISH Bar
    kParamPunishOn       = 300,
    kParamPunishAmount   = 301,
    kParamPunishMode     = 302, // 0: Gently Now, 1: Hurt Me Plenty, 2: Nightmare

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
constexpr int kBaseMidiNote    = 48; // C3 (Bank 1: C3-B3, Bank 2: C4-B4, Bank 3: C5-B5)
constexpr int kMaxVoices       = 128;
constexpr int kBaseUiSize      = 840;
constexpr int kWavePeakBins    = 256;

} // namespace NoiseMu6ix

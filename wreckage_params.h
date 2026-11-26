#pragma once
#include <cstdint>
#include <cmath>

namespace NoiseMu6ix {

enum ParamIDs : uint32_t {
    // 4-Bus Mixer Faders (normalized 0..1 mapped to -60.0 dB .. +6.0 dB with 0.0 dB center)
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

    // 3-Band EQ
    kParamEqOn           = 210,
    kParamEqLowFreq      = 211,
    kParamEqLowGain      = 212,
    kParamEqMidFreq      = 213,
    kParamEqMidGain      = 214,
    kParamEqHighFreq     = 215,
    kParamEqHighGain     = 216,

    // Compressor
    kParamCompOn         = 220,
    kParamCompThresh     = 221,
    kParamCompRatio      = 222,
    kParamCompAttack     = 223,
    kParamCompRelease    = 224,
    kParamCompKnee       = 225,
    kParamCompMakeup     = 226,

    // Saturator (default Mix = 0.0 / 0% Dry)
    kParamSatOn          = 230,
    kParamSatDrive       = 231,
    kParamSatWarmth      = 232,
    kParamSatMix         = 233,

    // Delay
    kParamDelayOn        = 240,
    kParamDelayMode      = 241,
    kParamDelayTime      = 242,
    kParamDelayFeedback  = 243,
    kParamDelayTone      = 244,
    kParamDelayWidth     = 245,
    kParamDelayMix       = 246,

    // Reverb
    kParamRevOn          = 250,
    kParamRevMode        = 251,
    kParamRevPreDelay    = 252,
    kParamRevDecay       = 253,
    kParamRevSize        = 254,
    kParamRevLowCut      = 255,
    kParamRevHighCut     = 256,
    kParamRevMix         = 257,

    // Global Master & Global Fake Mic (Fake RR & Fake Vel are Per-Key)
    kParamLimiterOn      = 260,
    kParamMonoBass       = 261,
    kParamVelCurve       = 262,
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
constexpr int kMaxRoundRobins  = 9;  // Hard limit = 9 RRs per key
constexpr int kMaxVelLayers    = 4;  // Hard limit = 4 Velocity Layers per key
constexpr int kBaseUiSize      = 840;
constexpr int kWavePeakBins    = 256;

// dB Gain mapping:
// norm = 0.0 -> -60.0 dB (silent floor)
// norm = 0.75 -> 0.0 dB (Unity gain center, generous minus range below, +6.0 dB boost above)
// norm = 1.0 -> +6.0 dB
inline float gainNormToDb(float norm01) {
    float n = (norm01 < 0.0f) ? 0.0f : ((norm01 > 1.0f) ? 1.0f : norm01);
    if (n <= 0.001f) return -60.0f;
    if (n <= 0.75f) {
        float u = n / 0.75f;
        return -60.0f + u * 60.0f;
    }
    float u = (n - 0.75f) / 0.25f;
    return u * 6.0f;
}

inline float gainDbToNorm(float db) {
    if (db <= -60.0f) return 0.0f;
    if (db <= 0.0f) return ((db + 60.0f) / 60.0f) * 0.75f;
    float u = db / 6.0f;
    if (u > 1.0f) u = 1.0f;
    return 0.75f + u * 0.25f;
}

inline float gainDbToLinear(float db) {
    if (db <= -59.5f) return 0.0f;
    float p = db / 20.0f;
    // Fast 2^(p * log2(10)) without heavy pow overhead
    float x = p * 3.3219280948873626f;
    int xi = static_cast<int>(x);
    if (x < static_cast<float>(xi)) --xi;
    float xf = x - static_cast<float>(xi);
    float poly = 1.0f + xf * (0.69314718f + xf * (0.24022651f + xf * 0.05550411f));
    float scale = 1.0f;
    if (xi >= 0) {
        for (int i = 0; i < xi && i < 16; ++i) scale *= 2.0f;
    } else {
        for (int i = 0; i < -xi && i < 24; ++i) scale *= 0.5f;
    }
    return scale * poly;
}

// Logarithmic Release mapping: 0.0 -> 10 ms, 0.5 -> 200 ms, 1.0 -> 4000 ms
inline float releaseNormToMs(float norm01) {
    float n = (norm01 < 0.0f) ? 0.0f : ((norm01 > 1.0f) ? 1.0f : norm01);
    // 10 * 400^n = 10 * 2^(n * 8.64385619)
    float x = n * 8.64385619f;
    int xi = static_cast<int>(x);
    float xf = x - static_cast<float>(xi);
    float poly = 1.0f + xf * (0.69314718f + xf * (0.24022651f + xf * 0.05550411f));
    float scale = 1.0f;
    for (int i = 0; i < xi && i < 16; ++i) scale *= 2.0f;
    return 10.0f * scale * poly;
}

inline float releaseMsToNorm(float ms) {
    if (ms <= 10.0f) return 0.0f;
    if (ms >= 4000.0f) return 1.0f;
    float n = std::log2(ms / 10.0f) / 8.64385619f;
    return (n < 0.0f) ? 0.0f : ((n > 1.0f) ? 1.0f : n);
}

} // namespace NoiseMu6ix

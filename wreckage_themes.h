#pragma once
#include <cstdint>

namespace NoiseMu6ix {

struct ThemePalette {
    const char* name;
    uint32_t bg;
    uint32_t panelBg;
    uint32_t headerBg;
    uint32_t border;
    uint32_t textPrimary;
    uint32_t textMuted;
    uint32_t accent;
    uint32_t faderTrack;
    uint32_t faderFill;
    uint32_t punishBg;
    bool sketchHatch;
};

static constexpr int kNumThemes = 10;

static constexpr ThemePalette kThemes[kNumThemes] = {
    { "PAPER SKETCH",      0xFAF9F5, 0xFFFFFF, 0xEFECE4, 0x181C22, 0x181C22, 0x5A6068, 0xD94E1F, 0xE6E2D8, 0x181C22, 0x20252C, true  },
    { "CHALKBOARD DARK",   0x1D2226, 0x242A30, 0x161A1E, 0xEAECEF, 0xF4F6F8, 0xA6B0BA, 0xFF914D, 0x353D45, 0xEAECEF, 0x121518, true  },
    { "BLUEPRINT INK",     0x112B4A, 0x16365C, 0x0C213B, 0xDCEBFF, 0xFFFFFF, 0x9BC2EB, 0xFF9F43, 0x214978, 0xDCEBFF, 0x091A2E, true  },
    { "VINTAGE PARCHMENT", 0xEFE5D1, 0xF7EFE0, 0xE2D4B7, 0x4A3525, 0x362418, 0x7A5F49, 0xB8421E, 0xD9C8A9, 0x4A3525, 0x362418, true  },
    { "INDUSTRIAL METAL",  0x1F2226, 0x282C32, 0x181B1F, 0x4C525B, 0xECEFF4, 0x9AA2AC, 0xFF6B1A, 0x14171A, 0xFF8C24, 0x141619, false },
    { "STEALTH CARBON",    0x121316, 0x1A1C21, 0x0D0E10, 0x3A3F4B, 0xE2E6EC, 0x7D8590, 0xC0C8D2, 0x23262E, 0x9BA3B0, 0x0B0C0E, false },
    { "BLOODLINE GRIT",    0x1B191B, 0x242024, 0x141214, 0x5A3A40, 0xF2EAEB, 0xA68F94, 0xE62E42, 0x332A2E, 0xE62E42, 0x120F11, false },
    { "NEON CYBERPUNK",    0x0E101A, 0x151929, 0x090B12, 0x2EE6D6, 0xECFDFB, 0x7CA6B8, 0xFF2E88, 0x1F253D, 0x2EE6D6, 0x080910, false },
    { "STUDIO CONSOLE",    0x2B2E33, 0x34383E, 0x22252A, 0x5C636E, 0xF0F2F5, 0xA4ACB8, 0x39C950, 0x1D2024, 0x39C950, 0x1A1D21, false },
    { "MINIMAL MONO",      0xF5F5F5, 0xFFFFFF, 0xEAEAEA, 0x111111, 0x111111, 0x666666, 0x111111, 0xDDDDDD, 0x111111, 0x111111, false }
};

static constexpr int kNumKeyColors = 12;
static constexpr uint32_t kStudioKeyColors[kNumKeyColors] = {
    0xFF3B30, // 0: Bright Kick Red
    0xFF8C00, // 1: Vivid Tom Orange
    0xFFCC00, // 2: Bright Cymbal Gold
    0x28CD41, // 3: Vivid Percussion Green
    0x00C7BE, // 4: Bright Teal
    0x007AFF, // 5: Vivid Snare Blue
    0x5856D6, // 6: Bright Indigo
    0xAF52DE, // 7: Vivid Purple
    0xFF2D55, // 8: Bright Magenta
    0xA2845E, // 9: Warm Wood
    0x5AC8FA, // 10: Bright Sky Cyan
    0x2C313A  // 11: Dark Ink
};

} // namespace NoiseMu6ix

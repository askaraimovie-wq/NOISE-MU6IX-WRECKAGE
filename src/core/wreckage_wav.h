#pragma once
#include "wreckage_params.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace NoiseMu6ix {

struct AudioBuffer {
    std::vector<float> left;
    std::vector<float> right;
    uint32_t sampleRate = 44100;
    std::array<float, kWavePeakBins> peaks{};
    float maxPeak = 0.0f;

    size_t numFrames() const { return left.size(); }
    bool empty() const { return left.empty(); }

    void rebuildPeaks() {
        peaks.fill(0.0f);
        maxPeak = 0.0f;
        size_t n = left.size();
        if (n == 0) return;
        for (int b = 0; b < kWavePeakBins; ++b) {
            size_t s0 = (static_cast<size_t>(b) * n) / kWavePeakBins;
            size_t s1 = std::max(s0 + 1, (static_cast<size_t>(b + 1) * n) / kWavePeakBins);
            s1 = std::min(s1, n);
            float pk = 0.0f;
            for (size_t i = s0; i < s1; ++i) {
                float v = std::max(std::fabs(left[i]), std::fabs(right[i]));
                if (v > pk) pk = v;
            }
            peaks[static_cast<size_t>(b)] = pk;
            if (pk > maxPeak) maxPeak = pk;
        }
    }
};

class WavLoader {
public:
    static bool loadFromMemory(const uint8_t* data, size_t size, AudioBuffer& out) {
        if (!data || size < 44) return false;
        if (std::memcmp(data, "RIFF", 4) != 0 && std::memcmp(data, "RF64", 4) != 0)
            return false;
        if (std::memcmp(data + 8, "WAVE", 4) != 0)
            return false;

        uint16_t audioFormat = 0;
        uint16_t numChannels = 0;
        uint32_t sampleRate = 44100;
        uint16_t bitsPerSample = 0;
        const uint8_t* pcmData = nullptr;
        uint64_t pcmSize = 0;

        size_t pos = 12;
        while (pos + 8 <= size) {
            char chunkId[5] = {0};
            std::memcpy(chunkId, data + pos, 4);
            uint32_t chunkSize32 = readU32(data + pos + 4);
            uint64_t chunkSize = chunkSize32;
            pos += 8;
            if (pos + chunkSize > size) chunkSize = size - pos;

            if (std::memcmp(chunkId, "fmt ", 4) == 0 && chunkSize >= 14) {
                audioFormat   = readU16(data + pos);
                numChannels   = readU16(data + pos + 2);
                sampleRate    = readU32(data + pos + 4);
                if (chunkSize >= 16) bitsPerSample = readU16(data + pos + 14);
                if (bitsPerSample == 0 && chunkSize >= 8 && numChannels > 0) {
                    uint32_t avgBytesPerSec = readU32(data + pos + 8);
                    if (sampleRate > 0) bitsPerSample = static_cast<uint16_t>((avgBytesPerSec * 8) / (sampleRate * numChannels));
                }
                if (audioFormat == 0xFFFE && chunkSize >= 26) {
                    audioFormat = readU16(data + pos + 24);
                }
            } else if (std::memcmp(chunkId, "data", 4) == 0) {
                pcmData = data + pos;
                pcmSize = chunkSize;
            }
            pos += static_cast<size_t>(chunkSize) + static_cast<size_t>(chunkSize & 1ULL);
        }

        if (!pcmData || pcmSize == 0 || numChannels == 0 || bitsPerSample == 0)
            return false;

        uint32_t bytesPerSample = std::max(1u, static_cast<uint32_t>(bitsPerSample) / 8u);
        uint64_t frameStride = static_cast<uint64_t>(bytesPerSample) * static_cast<uint64_t>(numChannels);
        size_t numFrames = static_cast<size_t>(pcmSize / frameStride);
        if (numFrames == 0) return false;

        out.sampleRate = (sampleRate > 0) ? sampleRate : 44100;
        out.left.resize(numFrames);
        out.right.resize(numFrames);

        auto decodeSample = [&](const uint8_t* p) -> float {
            if (audioFormat == 3 && bitsPerSample == 32) {
                float f = 0.0f;
                std::memcpy(&f, p, 4);
                if (!std::isfinite(f)) return 0.0f;
                return std::clamp(f, -4.0f, 4.0f);
            }
            if (audioFormat == 3 && bitsPerSample == 64) {
                double d = 0.0;
                std::memcpy(&d, p, 8);
                if (!std::isfinite(d)) return 0.0f;
                return static_cast<float>(std::clamp(d, -4.0, 4.0));
            }
            if (audioFormat == 6 && bitsPerSample == 8) {
                uint8_t a = p[0] ^ 0x55;
                int t = (a & 0x0F) << 4;
                int seg = (a & 0x70) >> 4;
                if (seg == 0) t += 8;
                else { t += 0x108; t <<= (seg - 1); }
                int s = (a & 0x80) ? -t : t;
                return static_cast<float>(s) / 32768.0f;
            }
            if (audioFormat == 7 && bitsPerSample == 8) {
                uint8_t u = ~p[0];
                int t = ((u & 0x0F) << 3) + 0x84;
                t <<= ((u & 0x70) >> 4);
                int s = (u & 0x80) ? (0x84 - t) : (t - 0x84);
                return static_cast<float>(s) / 32768.0f;
            }
            if (bitsPerSample == 16) {
                int16_t v = static_cast<int16_t>(readU16(p));
                return static_cast<float>(v) / 32768.0f;
            } else if (bitsPerSample == 24) {
                int32_t v = (static_cast<int32_t>(p[0])) |
                            (static_cast<int32_t>(p[1]) << 8) |
                            (static_cast<int32_t>(static_cast<int8_t>(p[2])) << 16);
                return static_cast<float>(v) / 8388608.0f;
            } else if (bitsPerSample == 32) {
                int32_t v = static_cast<int32_t>(readU32(p));
                return static_cast<float>(static_cast<double>(v) / 2147483648.0);
            } else if (bitsPerSample == 8) {
                return (static_cast<float>(p[0]) - 128.0f) / 128.0f;
            }
            return 0.0f;
        };

        for (size_t i = 0; i < numFrames; ++i) {
            const uint8_t* framePtr = pcmData + static_cast<size_t>(static_cast<uint64_t>(i) * frameStride);
            float l = decodeSample(framePtr);
            float r = (numChannels >= 2) ? decodeSample(framePtr + bytesPerSample) : l;
            out.left[i] = l;
            out.right[i] = r;
        }
        out.rebuildPeaks();
        return true;
    }

    static bool loadFromFile(const std::filesystem::path& path, AudioBuffer& out) {
        std::ifstream f(path, std::ios::binary | std::ios::ate);
        if (!f.is_open()) return false;
        std::streamsize sz = f.tellg();
        if (sz <= 44) return false;
        f.seekg(0, std::ios::beg);
        std::vector<uint8_t> buf(static_cast<size_t>(sz));
        if (!f.read(reinterpret_cast<char*>(buf.data()), sz)) return false;
        return loadFromMemory(buf.data(), buf.size(), out);
    }

private:
    static uint16_t readU16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
    static uint32_t readU32(const uint8_t* p) {
        return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
               (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
    }
};

} // namespace NoiseMu6ix

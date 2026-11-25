#pragma once
#include "wreckage_wav.h"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#ifdef WRECKAGE_USE_FLAC
#include <FLAC/stream_encoder.h>
#include <FLAC/stream_decoder.h>
#endif

namespace NoiseMu6ix {

enum class Nm6Codec : uint8_t {
    RawPCM24 = 0,
    FlacLossless = 1
};

struct Nm6SampleEntry {
    uint64_t hash = 0;
    uint32_t sampleRate = 44100;
    uint32_t numFrames = 0;
    Nm6Codec codec = Nm6Codec::FlacLossless;
    std::vector<uint8_t> encodedBytes;
};

struct Nm6Pack {
    uint32_t formatVersion = 1;
    std::string libraryName = "Noise Mu6ix Library";
    std::string author = "Noise Mu6ix Studio";
    std::string kitMetadataJson;
    std::vector<Nm6SampleEntry> samples;

    static uint64_t fnv1aHash(const AudioBuffer& buf) {
        uint64_t h = 14695981039346656037ULL;
        auto mix = [&](const void* ptr, size_t n) {
            const uint8_t* p = static_cast<const uint8_t*>(ptr);
            for (size_t i = 0; i < n; ++i) {
                h ^= static_cast<uint64_t>(p[i]);
                h *= 1099511628211ULL;
            }
        };
        mix(&buf.sampleRate, sizeof(buf.sampleRate));
        if (!buf.left.empty()) mix(buf.left.data(), buf.left.size() * sizeof(float));
        if (!buf.right.empty()) mix(buf.right.data(), buf.right.size() * sizeof(float));
        return h;
    }

    uint32_t addOrFindSample(const AudioBuffer& buf) {
        uint64_t h = fnv1aHash(buf);
        for (size_t i = 0; i < samples.size(); ++i) {
            if (samples[i].hash == h && samples[i].numFrames == buf.numFrames()) {
                return static_cast<uint32_t>(i);
            }
        }
        Nm6SampleEntry entry;
        entry.hash = h;
        entry.sampleRate = buf.sampleRate;
        entry.numFrames = static_cast<uint32_t>(buf.numFrames());
        encodeAudio(buf, entry);
        samples.push_back(std::move(entry));
        return static_cast<uint32_t>(samples.size() - 1);
    }

    bool decodeSample(uint32_t sampleId, AudioBuffer& out) const {
        if (sampleId >= samples.size()) return false;
        bool ok = decodeAudio(samples[sampleId], out);
        if (ok) out.rebuildPeaks();
        return ok;
    }

    bool saveAtomic(const std::filesystem::path& targetPath) const {
        std::filesystem::path tmpPath = targetPath;
        tmpPath += ".tmp";

        {
            std::ofstream f(tmpPath, std::ios::binary | std::ios::trunc);
            if (!f.is_open()) return false;

            const char magic[8] = { 'N','M','6','P','A','C','K', 0 };
            f.write(magic, 8);
            writeU32(f, formatVersion);
            writeStr(f, libraryName);
            writeStr(f, author);
            writeStr(f, kitMetadataJson);

            uint32_t count = static_cast<uint32_t>(samples.size());
            writeU32(f, count);
            for (const auto& s : samples) {
                writeU64(f, s.hash);
                writeU32(f, s.sampleRate);
                writeU32(f, s.numFrames);
                uint8_t c = static_cast<uint8_t>(s.codec);
                f.write(reinterpret_cast<const char*>(&c), 1);
                uint32_t len = static_cast<uint32_t>(s.encodedBytes.size());
                writeU32(f, len);
                if (len > 0) f.write(reinterpret_cast<const char*>(s.encodedBytes.data()), len);
            }
            f.flush();
            if (!f.good()) return false;
        }

        std::error_code ec;
        std::filesystem::rename(tmpPath, targetPath, ec);
        if (ec) {
            std::filesystem::remove(targetPath, ec);
            ec.clear();
            std::filesystem::rename(tmpPath, targetPath, ec);
            if (ec) return false;
        }
        return true;
    }

    bool loadFromFile(const std::filesystem::path& path) {
        std::ifstream f(path, std::ios::binary);
        if (!f.is_open()) return false;

        char magic[8] = {0};
        f.read(magic, 8);
        if (std::memcmp(magic, "NM6PACK", 7) != 0) return false;

        formatVersion = readU32(f);
        if (formatVersion == 0 || formatVersion > 16) return false;
        libraryName = readStr(f);
        author = readStr(f);
        kitMetadataJson = readStr(f);

        uint32_t count = readU32(f);
        samples.clear();
        samples.reserve(count);
        for (uint32_t i = 0; i < count; ++i) {
            Nm6SampleEntry s;
            s.hash = readU64(f);
            s.sampleRate = readU32(f);
            s.numFrames = readU32(f);
            uint8_t c = 0;
            f.read(reinterpret_cast<char*>(&c), 1);
            s.codec = static_cast<Nm6Codec>(c);
            uint32_t len = readU32(f);
            s.encodedBytes.resize(len);
            if (len > 0) f.read(reinterpret_cast<char*>(s.encodedBytes.data()), len);
            if (!f.good()) return false;
            samples.push_back(std::move(s));
        }
        return true;
    }

private:
    static void encodeAudio(const AudioBuffer& buf, Nm6SampleEntry& entry) {
#ifdef WRECKAGE_USE_FLAC
        struct WriteCtx { std::vector<uint8_t>* out; };
        entry.codec = Nm6Codec::FlacLossless;
        entry.encodedBytes.clear();
        WriteCtx ctx{ &entry.encodedBytes };

        FLAC__StreamEncoder* enc = FLAC__stream_encoder_new();
        if (enc) {
            FLAC__stream_encoder_set_channels(enc, 2);
            FLAC__stream_encoder_set_bits_per_sample(enc, 24);
            FLAC__stream_encoder_set_sample_rate(enc, buf.sampleRate);
            FLAC__stream_encoder_set_compression_level(enc, 5);
            FLAC__stream_encoder_set_blocksize(enc, 4096);
            FLAC__stream_encoder_set_total_samples_estimate(enc, buf.numFrames());

            auto writeCb = [](const FLAC__StreamEncoder*, const FLAC__byte buffer[],
                              size_t bytes, unsigned, unsigned, void* client_data) -> FLAC__StreamEncoderWriteStatus {
                auto* c = static_cast<WriteCtx*>(client_data);
                c->out->insert(c->out->end(), buffer, buffer + bytes);
                return FLAC__STREAM_ENCODER_WRITE_STATUS_OK;
            };

            if (FLAC__stream_encoder_init_stream(enc, writeCb, nullptr, nullptr, nullptr, &ctx)
                == FLAC__STREAM_ENCODER_INIT_STATUS_OK) {
                const size_t n = buf.numFrames();
                std::vector<FLAC__int32> interleaved(n * 2);
                for (size_t i = 0; i < n; ++i) {
                    interleaved[i * 2]     = static_cast<FLAC__int32>(std::clamp(buf.left[i], -1.0f, 1.0f) * 8388607.0f);
                    interleaved[i * 2 + 1] = static_cast<FLAC__int32>(std::clamp(buf.right[i], -1.0f, 1.0f) * 8388607.0f);
                }
                FLAC__stream_encoder_process_interleaved(enc, interleaved.data(), static_cast<uint32_t>(n));
                FLAC__stream_encoder_finish(enc);
                FLAC__stream_encoder_delete(enc);
                if (!entry.encodedBytes.empty()) return;
            } else {
                FLAC__stream_encoder_delete(enc);
            }
        }
#endif
        entry.codec = Nm6Codec::RawPCM24;
        const size_t n = buf.numFrames();
        entry.encodedBytes.resize(n * 6);
        uint8_t* dst = entry.encodedBytes.data();
        for (size_t i = 0; i < n; ++i) {
            int32_t l = static_cast<int32_t>(std::clamp(buf.left[i], -1.0f, 1.0f) * 8388607.0f);
            int32_t r = static_cast<int32_t>(std::clamp(buf.right[i], -1.0f, 1.0f) * 8388607.0f);
            dst[0] = static_cast<uint8_t>(l & 0xFF);
            dst[1] = static_cast<uint8_t>((l >> 8) & 0xFF);
            dst[2] = static_cast<uint8_t>((l >> 16) & 0xFF);
            dst[3] = static_cast<uint8_t>(r & 0xFF);
            dst[4] = static_cast<uint8_t>((r >> 8) & 0xFF);
            dst[5] = static_cast<uint8_t>((r >> 16) & 0xFF);
            dst += 6;
        }
    }

    static bool decodeAudio(const Nm6SampleEntry& entry, AudioBuffer& out) {
        out.sampleRate = entry.sampleRate;
        out.left.clear();
        out.right.clear();

#ifdef WRECKAGE_USE_FLAC
        if (entry.codec == Nm6Codec::FlacLossless) {
            struct ReadCtx {
                const uint8_t* ptr;
                size_t size;
                size_t offset;
                AudioBuffer* out;
            };
            ReadCtx ctx{ entry.encodedBytes.data(), entry.encodedBytes.size(), 0, &out };
            out.left.reserve(entry.numFrames);
            out.right.reserve(entry.numFrames);

            FLAC__StreamDecoder* dec = FLAC__stream_decoder_new();
            if (!dec) return false;

            auto readCb = [](const FLAC__StreamDecoder*, FLAC__byte buffer[], size_t* bytes, void* cd) -> FLAC__StreamDecoderReadStatus {
                auto* c = static_cast<ReadCtx*>(cd);
                if (c->offset >= c->size) { *bytes = 0; return FLAC__STREAM_DECODER_READ_STATUS_END_OF_STREAM; }
                size_t avail = std::min(*bytes, c->size - c->offset);
                std::memcpy(buffer, c->ptr + c->offset, avail);
                c->offset += avail;
                *bytes = avail;
                return FLAC__STREAM_DECODER_READ_STATUS_CONTINUE;
            };
            auto writeCb = [](const FLAC__StreamDecoder*, const FLAC__Frame* frame,
                              const FLAC__int32* const buffer[], void* cd) -> FLAC__StreamDecoderWriteStatus {
                auto* c = static_cast<ReadCtx*>(cd);
                float scale = 1.0f / static_cast<float>(1 << (frame->header.bits_per_sample - 1));
                for (uint32_t i = 0; i < frame->header.blocksize; ++i) {
                    float l = static_cast<float>(buffer[0][i]) * scale;
                    float r = (frame->header.channels >= 2) ? static_cast<float>(buffer[1][i]) * scale : l;
                    c->out->left.push_back(l);
                    c->out->right.push_back(r);
                }
                return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
            };
            auto errCb = [](const FLAC__StreamDecoder*, FLAC__StreamDecoderErrorStatus, void*) {};

            bool ok = false;
            if (FLAC__stream_decoder_init_stream(dec, readCb, nullptr, nullptr, nullptr, nullptr,
                                                 writeCb, nullptr, errCb, &ctx)
                == FLAC__STREAM_DECODER_INIT_STATUS_OK) {
                ok = FLAC__stream_decoder_process_until_end_of_stream(dec) != 0;
                FLAC__stream_decoder_finish(dec);
            }
            FLAC__stream_decoder_delete(dec);
            return ok && !out.empty();
        }
#endif
        if (entry.codec == Nm6Codec::RawPCM24) {
            size_t n = entry.encodedBytes.size() / 6;
            out.left.resize(n);
            out.right.resize(n);
            const uint8_t* src = entry.encodedBytes.data();
            for (size_t i = 0; i < n; ++i) {
                int32_t l = (static_cast<int32_t>(src[0])) | (static_cast<int32_t>(src[1]) << 8) |
                            (static_cast<int32_t>(static_cast<int8_t>(src[2])) << 16);
                int32_t r = (static_cast<int32_t>(src[3])) | (static_cast<int32_t>(src[4]) << 8) |
                            (static_cast<int32_t>(static_cast<int8_t>(src[5])) << 16);
                out.left[i] = static_cast<float>(l) / 8388608.0f;
                out.right[i] = static_cast<float>(r) / 8388608.0f;
                src += 6;
            }
            return true;
        }
        return false;
    }

    static void writeU32(std::ofstream& f, uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); }
    static void writeU64(std::ofstream& f, uint64_t v) { f.write(reinterpret_cast<const char*>(&v), 8); }
    static void writeStr(std::ofstream& f, const std::string& s) {
        uint32_t n = static_cast<uint32_t>(s.size());
        writeU32(f, n);
        if (n) f.write(s.data(), n);
    }
    static uint32_t readU32(std::ifstream& f) { uint32_t v = 0; f.read(reinterpret_cast<char*>(&v), 4); return v; }
    static uint64_t readU64(std::ifstream& f) { uint64_t v = 0; f.read(reinterpret_cast<char*>(&v), 8); return v; }
    static std::string readStr(std::ifstream& f) {
        uint32_t n = readU32(f);
        std::string s(n, '\0');
        if (n) f.read(&s[0], n);
        return s;
    }
};

} // namespace NoiseMu6ix

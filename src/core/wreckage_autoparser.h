#pragma once
#include "wreckage_params.h"
#include <algorithm>
#include <cctype>
#include <regex>
#include <string>

namespace NoiseMu6ix {

struct ParsedSampleMeta {
    int velocityIndex = 0;
    int rrIndex       = 0;
    MicBus mic        = MicBus::Close;
    bool hasExplicitVel = false;
    bool hasExplicitRR  = false;
    bool hasExplicitMic = false;
    std::string cleanInstrumentName;
};

class SampleAutoParser {
public:
    static ParsedSampleMeta parseFilename(const std::string& filenameUtf8) {
        ParsedSampleMeta meta;
        std::string stem = filenameUtf8;
        auto dot = stem.find_last_of('.');
        if (dot != std::string::npos) stem = stem.substr(0, dot);

        std::string lower = stem;
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        std::smatch m;
        static const std::regex velRe(R"((?:^|[\s_\-])(?:vel|v)0*([1-9][0-9]?)(?:$|[\s_\-]))");
        if (std::regex_search(lower, m, velRe)) {
            meta.velocityIndex = std::max(0, std::stoi(m[1].str()) - 1);
            meta.hasExplicitVel = true;
        }

        static const std::regex rrRe(R"((?:^|[\s_\-])(?:rr|robin|r)0*([1-9][0-9]?)(?:$|[\s_\-]))");
        if (std::regex_search(lower, m, rrRe)) {
            meta.rrIndex = std::max(0, std::stoi(m[1].str()) - 1);
            meta.hasExplicitRR = true;
        }

        auto hasToken = [&](const std::regex& re) { return std::regex_search(lower, re); };
        if (hasToken(std::regex(R"((?:^|[\s_\-])(?:room|rm)(?:$|[\s_\-]))"))) {
            meta.mic = MicBus::Room; meta.hasExplicitMic = true;
        } else if (hasToken(std::regex(R"((?:^|[\s_\-])(?:hall|far|amb|ambient)(?:$|[\s_\-]))"))) {
            meta.mic = MicBus::Hall; meta.hasExplicitMic = true;
        } else if (hasToken(std::regex(R"((?:^|[\s_\-])(?:lfe|sub)(?:$|[\s_\-]))"))) {
            meta.mic = MicBus::Sub; meta.hasExplicitMic = true;
        } else if (hasToken(std::regex(R"((?:^|[\s_\-])(?:close|cls|dry|spot)(?:$|[\s_\-]))"))) {
            meta.mic = MicBus::Close; meta.hasExplicitMic = true;
        }

        std::string clean = stem;
        auto cut = clean.find_first_of("_-");
        if (cut != std::string::npos && cut >= 2) clean = clean.substr(0, cut);
        if (clean.size() > 14) clean = clean.substr(0, 14);
        meta.cleanInstrumentName = clean;
        return meta;
    }
};

} // namespace NoiseMu6ix

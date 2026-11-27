#pragma once
#include "wreckage_params.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace NoiseMu6ix {

constexpr float kPi = 3.14159265358979323846f;

inline float fastExp(float x) {
    if (x < -12.0f) return 0.0f;
    if (x > 12.0f) x = 12.0f;
    float y = x * 1.4426950408889634f;
    int yi = static_cast<int>(y);
    if (y < static_cast<float>(yi)) --yi;
    float yf = y - static_cast<float>(yi);
    float poly = 1.0f + yf * (0.69314718f + yf * (0.24022651f + yf * 0.05550411f));
    float sc = 1.0f;
    if (yi >= 0) {
        for (int i = 0; i < yi && i < 16; ++i) sc *= 2.0f;
    } else {
        for (int i = 0; i < -yi && i < 24; ++i) sc *= 0.5f;
    }
    return sc * poly;
}

inline float fastPow10(float p) {
    return fastExp(p * 2.302585092994046f);
}

inline float flushDenormal(float x) {
    return (std::fabs(x) < 1e-20f) ? 0.0f : x;
}

inline float fastTanh(float x) {
    if (x < -3.0f) return -1.0f;
    if (x >  3.0f) return  1.0f;
    float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

struct DCBlocker {
    float x1 = 0.0f, y1 = 0.0f;
    float r = 0.9965f;
    void setSampleRate(float sr) { r = fastExp(-2.0f * kPi * 20.0f / std::max(8000.0f, sr)); }
    void reset() { x1 = y1 = 0.0f; }
    inline float process(float x) {
        float y = flushDenormal(x - x1 + r * y1);
        x1 = x; y1 = y;
        return y;
    }
};

struct Biquad {
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
    float z1 = 0.0f, z2 = 0.0f;

    void reset() { z1 = z2 = 0.0f; }

    void setLowPass(float sr, float freq, float q = 0.7071f) {
        freq = std::clamp(freq, 20.0f, sr * 0.45f);
        float w0 = 2.0f * kPi * freq / sr;
        float c = std::cos(w0), s = std::sin(w0);
        float alpha = s / (2.0f * std::max(0.1f, q));
        float a0 = 1.0f + alpha;
        b0 = ((1.0f - c) * 0.5f) / a0;
        b1 = (1.0f - c) / a0;
        b2 = ((1.0f - c) * 0.5f) / a0;
        a1 = (-2.0f * c) / a0;
        a2 = (1.0f - alpha) / a0;
    }

    void setHighPass(float sr, float freq, float q = 0.7071f) {
        freq = std::clamp(freq, 15.0f, sr * 0.45f);
        float w0 = 2.0f * kPi * freq / sr;
        float c = std::cos(w0), s = std::sin(w0);
        float alpha = s / (2.0f * std::max(0.1f, q));
        float a0 = 1.0f + alpha;
        b0 = ((1.0f + c) * 0.5f) / a0;
        b1 = -(1.0f + c) / a0;
        b2 = ((1.0f + c) * 0.5f) / a0;
        a1 = (-2.0f * c) / a0;
        a2 = (1.0f - alpha) / a0;
    }

    void setBandPass(float sr, float freq, float q = 1.2f) {
        freq = std::clamp(freq, 25.0f, sr * 0.45f);
        float w0 = 2.0f * kPi * freq / sr;
        float c = std::cos(w0), s = std::sin(w0);
        float alpha = s / (2.0f * std::max(0.2f, q));
        float a0 = 1.0f + alpha;
        b0 = alpha / a0;
        b1 = 0.0f;
        b2 = -alpha / a0;
        a1 = (-2.0f * c) / a0;
        a2 = (1.0f - alpha) / a0;
    }

    void setNotch(float sr, float freq, float q = 1.5f) {
        freq = std::clamp(freq, 25.0f, sr * 0.45f);
        float w0 = 2.0f * kPi * freq / sr;
        float c = std::cos(w0), s = std::sin(w0);
        float alpha = s / (2.0f * std::max(0.2f, q));
        float a0 = 1.0f + alpha;
        b0 = 1.0f / a0;
        b1 = (-2.0f * c) / a0;
        b2 = 1.0f / a0;
        a1 = (-2.0f * c) / a0;
        a2 = (1.0f - alpha) / a0;
    }

    void setPeaking(float sr, float freq, float q, float gainDb) {
        freq = std::clamp(freq, 25.0f, sr * 0.45f);
        float A = fastPow10(gainDb / 40.0f);
        float w0 = 2.0f * kPi * freq / sr;
        float c = std::cos(w0), s = std::sin(w0);
        float alpha = s / (2.0f * std::max(0.2f, q));
        float a0 = 1.0f + alpha / A;
        b0 = (1.0f + alpha * A) / a0;
        b1 = (-2.0f * c) / a0;
        b2 = (1.0f - alpha * A) / a0;
        a1 = (-2.0f * c) / a0;
        a2 = (1.0f - alpha / A) / a0;
    }

    inline float process(float x) {
        float y = b0 * x + z1;
        z1 = flushDenormal(b1 * x - a1 * y + z2);
        z2 = flushDenormal(b2 * x - a2 * y);
        return y;
    }
};

// Schroeder All-Pass Diffuser for silky, non-metallic percussion reverb
struct AllPassDiffuser {
    std::vector<float> buf;
    int idx = 0;
    float g = 0.62f;

    void prepare(int samples, float gain) {
        buf.assign(static_cast<size_t>(std::max(8, samples)), 0.0f);
        idx = 0;
        g = gain;
    }
    void reset() {
        std::fill(buf.begin(), buf.end(), 0.0f);
        idx = 0;
    }
    inline float process(float x) {
        float d = buf[idx];
        float v = flushDenormal(x + d * g);
        buf[idx] = v;
        if (++idx >= static_cast<int>(buf.size())) idx = 0;
        return flushDenormal(d - v * g);
    }
};

// 8-Line Modulated FDN + 4-Stage Input Diffuser Reverb (Lush Room / Plate / Hall)
class StereoSpaceBus {
public:
    void prepare(float sampleRate, float preDelayMs, float decaySeconds, float hpHz, float lpHz) {
        sr_ = std::max(8000.0f, sampleRate);
        maxPreSamples_ = std::max(8, static_cast<int>(sr_ * 0.25f));
        preBufL_.assign(static_cast<size_t>(maxPreSamples_), 0.0f);
        preBufR_.assign(static_cast<size_t>(maxPreSamples_), 0.0f);
        preIdx_ = 0;

        static constexpr float diffMsL[4] = { 4.3f, 6.7f, 10.1f, 14.9f };
        static constexpr float diffMsR[4] = { 4.7f, 7.1f, 10.7f, 15.7f };
        static constexpr float diffG[4]   = { 0.66f, 0.64f, 0.60f, 0.56f };
        for (int i = 0; i < 4; ++i) {
            diffL_[i].prepare(static_cast<int>(sr_ * diffMsL[i] * 0.001f), diffG[i]);
            diffR_[i].prepare(static_cast<int>(sr_ * diffMsR[i] * 0.001f), diffG[i]);
        }

        static constexpr float baseMs[8] = { 23.1f, 29.3f, 34.7f, 41.9f, 47.3f, 53.9f, 61.7f, 71.1f };
        for (int i = 0; i < 8; ++i) {
            int maxLen = std::max(128, static_cast<int>(sr_ * baseMs[i] * 2.5f * 0.001f) + 64);
            lines_[i].assign(static_cast<size_t>(maxLen), 0.0f);
            activeLen_[i] = std::max(48, static_cast<int>(sr_ * baseMs[i] * 0.001f));
            idx_[i] = 0;
            dampState_[i] = 0.0f;
        }
        lfoPhase_ = 0.0f;
        lastHp_ = -1.0f; lastLp_ = -1.0f;
        configure(preDelayMs, decaySeconds, 0.65f, hpHz, lpHz);
        hpL_.reset(); hpR_.reset(); lpL_.reset(); lpR_.reset();
    }

    void reset() {
        std::fill(preBufL_.begin(), preBufL_.end(), 0.0f);
        std::fill(preBufR_.begin(), preBufR_.end(), 0.0f);
        preIdx_ = 0;
        for (int i = 0; i < 4; ++i) { diffL_[i].reset(); diffR_[i].reset(); }
        for (int i = 0; i < 8; ++i) {
            std::fill(lines_[i].begin(), lines_[i].end(), 0.0f);
            idx_[i] = 0;
            dampState_[i] = 0.0f;
        }
        hpL_.reset(); hpR_.reset(); lpL_.reset(); lpR_.reset();
    }

    void configure(float preDelayMs, float decaySeconds, float size01, float hpHz, float lpHz) {
        preDelaySamples_ = std::clamp(static_cast<int>(sr_ * preDelayMs * 0.001f), 1, maxPreSamples_ - 1);
        static constexpr float baseMs[8] = { 23.1f, 29.3f, 34.7f, 41.9f, 47.3f, 53.9f, 61.7f, 71.1f };
        float sizeScale = 0.45f + std::clamp(size01, 0.0f, 1.0f) * 1.65f;
        float rt = std::max(0.20f, decaySeconds);
        for (int i = 0; i < 8; ++i) {
            int maxL = static_cast<int>(lines_[i].size()) - 24;
            int len = std::clamp(static_cast<int>(sr_ * baseMs[i] * sizeScale * 0.001f), 40, std::max(40, maxL));
            activeLen_[i] = len;
            if (idx_[i] >= len) idx_[i] = 0;
            fb_[i] = std::clamp(fastPow10((-3.0f * (static_cast<float>(len) / sr_)) / rt), 0.20f, 0.982f);
        }
        dampCoeff_ = std::clamp(1.0f - (lpHz / (sr_ * 0.5f)), 0.08f, 0.86f);
        if (std::fabs(hpHz - lastHp_) > 1.0f) { hpL_.setHighPass(sr_, hpHz); hpR_.setHighPass(sr_, hpHz); lastHp_ = hpHz; }
        if (std::fabs(lpHz - lastLp_) > 5.0f) { lpL_.setLowPass(sr_, lpHz);  lpR_.setLowPass(sr_, lpHz);  lastLp_ = lpHz; }
    }

    inline void process(float inL, float inR, float& outL, float& outR) {
        int readIdx = preIdx_ - preDelaySamples_;
        if (readIdx < 0) readIdx += maxPreSamples_;
        float pdL = preBufL_[readIdx];
        float pdR = preBufR_[readIdx];
        preBufL_[preIdx_] = inL;
        preBufR_[preIdx_] = inR;
        if (++preIdx_ >= maxPreSamples_) preIdx_ = 0;

        // 4-stage stereo all-pass diffusion eliminates sharp metallic transients
        float dfL = pdL, dfR = pdR;
        for (int i = 0; i < 4; ++i) {
            dfL = diffL_[i].process(dfL);
            dfR = diffR_[i].process(dfR);
        }

        // Strictly positive chorus-like LFO modulation (+1.0 .. +12.0 samples) eliminates wrap clicks
        lfoPhase_ += (2.0f * kPi * 0.65f) / sr_;
        if (lfoPhase_ > 2.0f * kPi) lfoPhase_ -= 2.0f * kPi;
        float modA = 6.5f + std::sin(lfoPhase_) * 5.5f;
        float modB = 6.5f + std::cos(lfoPhase_) * 5.5f;

        float d[8];
        for (int i = 0; i < 8; ++i) {
            int len = activeLen_[i];
            float s = 0.0f;
            if (i < 4) {
                float mod = ((i & 1) == 0) ? modA : modB;
                float rPos = static_cast<float>(idx_[i]) + mod;
                while (rPos < 0.0f) rPos += static_cast<float>(len);
                while (rPos >= static_cast<float>(len)) rPos -= static_cast<float>(len);
                int i0 = static_cast<int>(rPos);
                int i1 = (i0 + 1 >= len) ? 0 : (i0 + 1);
                float frac = rPos - static_cast<float>(i0);
                s = lines_[i][i0] + frac * (lines_[i][i1] - lines_[i][i0]);
            } else {
                s = lines_[i][idx_[i]];
            }
            dampState_[i] = flushDenormal(s * (1.0f - dampCoeff_) + dampState_[i] * dampCoeff_);
            d[i] = dampState_[i] * fb_[i];
        }

        // Orthogonal Householder mixing matrix: y_i = d_i - 0.25 * sum(d)
        float sum8 = (d[0] + d[1] + d[2] + d[3] + d[4] + d[5] + d[6] + d[7]) * 0.25f;
        float inj[8] = {
             dfL * 0.55f,  dfR * 0.55f,
             dfL * 0.45f, -dfR * 0.45f,
            -dfL * 0.40f,  dfR * 0.40f,
             dfL * 0.35f, -dfR * 0.35f
        };
        for (int i = 0; i < 8; ++i) {
            lines_[i][idx_[i]] = (d[i] - sum8) + inj[i];
            if (++idx_[i] >= activeLen_[i]) idx_[i] = 0;
        }

        float rawL = 0.62f * (d[0] + d[2] - d[4] + d[6]);
        float rawR = 0.62f * (d[1] - d[3] + d[5] - d[7]);
        outL = lpL_.process(hpL_.process(rawL));
        outR = lpR_.process(hpR_.process(rawR));
    }

private:
    float sr_ = 44100.0f;
    int maxPreSamples_ = 11025;
    int preDelaySamples_ = 352;
    int preIdx_ = 0;
    std::vector<float> preBufL_, preBufR_;
    std::array<AllPassDiffuser, 4> diffL_, diffR_;
    std::array<std::vector<float>, 8> lines_;
    std::array<int, 8> activeLen_{{100,100,100,100,100,100,100,100}};
    std::array<int, 8> idx_{{0,0,0,0,0,0,0,0}};
    std::array<float, 8> fb_{{0.5f,0.5f,0.5f,0.5f,0.5f,0.5f,0.5f,0.5f}};
    std::array<float, 8> dampState_{{0,0,0,0,0,0,0,0}};
    float lfoPhase_ = 0.0f;
    float dampCoeff_ = 0.38f;
    float lastHp_ = -1.0f, lastLp_ = -1.0f;
    Biquad hpL_, hpR_, lpL_, lpR_;
};

// 3-Mode Pro Delay: 0=STEREO, 1=PING-PONG, 2=TAPE (click-free fractional delay smoothing)
class StereoDelayBus {
public:
    void prepare(float sr) {
        sr_ = std::max(8000.0f, sr);
        maxSamples_ = static_cast<int>(sr_ * 1.25f);
        bufL_.assign(static_cast<size_t>(maxSamples_), 0.0f);
        bufR_.assign(static_cast<size_t>(maxSamples_), 0.0f);
        writeIdx_ = 0;
        curDelaySamp_ = sr_ * 0.24f;
        tapePhase_ = 0.0f;
        hpL_.setHighPass(sr_, 120.0f); hpR_.setHighPass(sr_, 120.0f);
        lpL_.setLowPass(sr_, 6500.0f); lpR_.setLowPass(sr_, 6500.0f);
        lastCut_ = 6500.0f;
        reset();
    }

    void reset() {
        std::fill(bufL_.begin(), bufL_.end(), 0.0f);
        std::fill(bufR_.begin(), bufR_.end(), 0.0f);
        writeIdx_ = 0;
        hpL_.reset(); hpR_.reset(); lpL_.reset(); lpR_.reset();
        inL_ = inR_ = 0.0f;
    }

    inline float readFrac(const std::vector<float>& buf, float delaySamp) const {
        float rPos = static_cast<float>(writeIdx_) - delaySamp;
        while (rPos < 0.0f) rPos += static_cast<float>(maxSamples_);
        while (rPos >= static_cast<float>(maxSamples_)) rPos -= static_cast<float>(maxSamples_);
        int i0 = static_cast<int>(rPos);
        int i1 = (i0 + 1 >= maxSamples_) ? 0 : (i0 + 1);
        float frac = rPos - static_cast<float>(i0);
        return buf[i0] + frac * (buf[i1] - buf[i0]);
    }

    inline void process(float inL, float inR, int mode, float timeMs, float feedback, float tone01, float width01, float mix01) {
        float targetSamp = std::clamp(sr_ * timeMs * 0.001f, 16.0f, static_cast<float>(maxSamples_ - 32));
        curDelaySamp_ = curDelaySamp_ * 0.996f + targetSamp * 0.004f;

        float modOffset = 0.0f;
        if (mode == 2) { // TAPE wow & flutter
            tapePhase_ += (2.0f * kPi * 1.15f) / sr_;
            if (tapePhase_ > 2.0f * kPi) tapePhase_ -= 2.0f * kPi;
            modOffset = std::sin(tapePhase_) * 4.5f;
        }

        float delL = std::max(8.0f, curDelaySamp_ + modOffset);
        float delR = (mode == 1)
            ? delL
            : std::max(8.0f, curDelaySamp_ * (1.0f - width01 * 0.14f) - modOffset);

        float dL = readFrac(bufL_, delL);
        float dR = readFrac(bufR_, delR);

        float cutHz = (mode == 2) ? (900.0f + tone01 * 7200.0f) : (1200.0f + tone01 * 13500.0f);
        if (std::fabs(cutHz - lastCut_) > 25.0f) {
            lpL_.setLowPass(sr_, cutHz); lpR_.setLowPass(sr_, cutHz); lastCut_ = cutHz;
        }
        dL = lpL_.process(hpL_.process(dL));
        dR = lpR_.process(hpR_.process(dR));

        if (mode == 2) { // TAPE saturation
            dL = fastTanh(dL * 1.65f) * 0.84f;
            dR = fastTanh(dR * 1.65f) * 0.84f;
        }

        float fb = std::clamp(feedback, 0.0f, 0.88f);
        float nextL = 0.0f, nextR = 0.0f;
        if (mode == 1) { // PING-PONG: mono input enters L, bounces L -> R -> L
            float monoIn = 0.5f * (inL + inR);
            nextL = monoIn + fb * dR;
            nextR = fb * dL;
        } else {
            float cross = width01 * 0.45f;
            nextL = inL + fb * (dL * (1.0f - cross) + dR * cross);
            nextR = inR + fb * (dR * (1.0f - cross) + dL * cross);
        }

        bufL_[writeIdx_] = flushDenormal(nextL);
        bufR_[writeIdx_] = flushDenormal(nextR);
        if (++writeIdx_ >= maxSamples_) writeIdx_ = 0;

        float dryScale = 1.0f - mix01 * 0.30f;
        inL_ = inL * dryScale + dL * mix01;
        inR_ = inR * dryScale + dR * mix01;
    }

    float outL() const { return inL_; }
    float outR() const { return inR_; }

private:
    float sr_ = 44100.0f;
    int maxSamples_ = 56000;
    int writeIdx_ = 0;
    float curDelaySamp_ = 10584.0f;
    float tapePhase_ = 0.0f;
    std::vector<float> bufL_, bufR_;
    Biquad hpL_, hpR_, lpL_, lpR_;
    float lastCut_ = -1.0f;
    float inL_ = 0.0f, inR_ = 0.0f;
};

class SubBassBus {
public:
    void prepare(float sr) {
        lp1_.setLowPass(sr, 95.0f, 0.85f);
        lp2_.setLowPass(sr, 95.0f, 0.85f);
        subPeak_.setPeaking(sr, 55.0f, 1.1f, 6.0f);
        dc_.setSampleRate(sr);
        reset();
    }
    void reset() { lp1_.reset(); lp2_.reset(); subPeak_.reset(); dc_.reset(); }
    inline void process(float inL, float inR, float& outL, float& outR) {
        float mono = 0.5f * (inL + inR);
        float sub = lp2_.process(lp1_.process(mono));
        float boosted = subPeak_.process(sub);
        float shaped = fastTanh(boosted * 1.45f) * 0.92f;
        shaped = dc_.process(shaped);
        outL = shaped; outR = shaped;
    }
private:
    Biquad lp1_, lp2_, subPeak_;
    DCBlocker dc_;
};

struct MasterFxParams {
    bool  filterOn = false;
    int   filterType = 0;
    float filterCutoffHz = 12000.0f;
    float filterRes = 0.15f;

    bool  eqOn = false;
    float eqLowFreq = 90.0f,  eqLowGain = 0.0f;
    float eqMidFreq = 1100.0f, eqMidGain = 0.0f;
    float eqHighFreq = 7500.0f, eqHighGain = 0.0f;

    bool  compOn = false;
    float compThreshDb = -14.0f;
    float compRatio = 4.0f;
    float compAttackMs = 10.0f;
    float compReleaseMs = 110.0f;
    float compKneeDb = 4.0f;
    float compMakeupDb = 0.0f;

    bool  satOn = false;
    int   satMode = 0; // 0:TAPE, 1:TUBE, 2:CRUSH, 3:CLIP
    float satDrive = 0.25f;
    float satWarmth = 0.50f;
    float satMix = 0.0f;

    bool  delayOn = false;
    int   delayMode = 0; // 0:STEREO, 1:PING-PONG, 2:TAPE
    float delayTimeMs = 240.0f;
    float delayFeedback = 0.32f;
    float delayTone = 0.65f;
    float delayWidth = 0.70f;
    float delayMix = 0.22f;

    bool  revOn = false;
    int   revMode = 2; // 0:ROOM, 1:PLATE, 2:HALL
    float revPreDelayMs = 18.0f;
    float revDecaySec = 2.4f;
    float revSize = 0.65f;
    float revLowCutHz = 120.0f;
    float revHighCutHz = 8500.0f;
    float revMix = 0.30f;

    bool  limiterOn = true;
    bool  monoBass = true;

    bool  punishOn = false;
    float punishAmount = 0.0f;
    PunishMode punishMode = PunishMode::HurtMePlenty;
};

class MasterFxRack {
public:
    void prepare(float sr) {
        sr_ = std::max(8000.0f, sr);
        dcL_.setSampleRate(sr_); dcR_.setSampleRate(sr_);
        satDcL_.setSampleRate(sr_); satDcR_.setSampleRate(sr_);
        monoBassL_.setLowPass(sr_, 120.0f); monoBassR_.setLowPass(sr_, 120.0f);
        satTapeLpL_.setLowPass(sr_, 11500.0f); satTapeLpR_.setLowPass(sr_, 11500.0f);
        delayBus_.prepare(sr_);
        masterRevBus_.prepare(sr_, 18.0f, 2.4f, 120.0f, 8500.0f);
        limRel_ = fastExp(-1.0f / (sr_ * 0.045f));
        lastFiltHash_ = -1.0f;
        lastEqHash_ = -1.0f;
        lastRevHash_ = -1.0f;
        lastWarmth_ = -99.0f;
        lastCompAttMs_ = -1.0f;
        lastCompRelMs_ = -1.0f;
        wasDelayOn_ = false;
        wasRevOn_ = false;
        resetTails();
    }

    void resetTails() {
        delayBus_.reset();
        masterRevBus_.reset();
        dcL_.reset(); dcR_.reset();
        satDcL_.reset(); satDcR_.reset();
        compEnv_ = 0.0f;
        lastGrDb_ = 0.0f;
        outEnv_ = 0.0f;
        limGain_ = 1.0f;
    }

    float currentGrDb() const { return lastGrDb_; }
    float currentOutLevel() const { return outEnv_; }

    inline void process(float& l, float& r, const MasterFxParams& p) {
        if (p.filterOn) {
            float fHash = p.filterCutoffHz + p.filterRes * 100000.0f + p.filterType * 1000000.0f;
            if (std::fabs(fHash - lastFiltHash_) > 0.1f) {
                lastFiltHash_ = fHash;
                float q = 0.707f + p.filterRes * 7.5f;
                switch (p.filterType) {
                    case 0: case 1:
                        flt1L_.setLowPass(sr_, p.filterCutoffHz, q); flt1R_.setLowPass(sr_, p.filterCutoffHz, q);
                        flt2L_.setLowPass(sr_, p.filterCutoffHz, 0.707f); flt2R_.setLowPass(sr_, p.filterCutoffHz, 0.707f);
                        break;
                    case 2: case 3:
                        flt1L_.setHighPass(sr_, p.filterCutoffHz, q); flt1R_.setHighPass(sr_, p.filterCutoffHz, q);
                        flt2L_.setHighPass(sr_, p.filterCutoffHz, 0.707f); flt2R_.setHighPass(sr_, p.filterCutoffHz, 0.707f);
                        break;
                    case 4: case 5:
                        flt1L_.setBandPass(sr_, p.filterCutoffHz, q); flt1R_.setBandPass(sr_, p.filterCutoffHz, q);
                        flt2L_.setBandPass(sr_, p.filterCutoffHz, 1.0f); flt2R_.setBandPass(sr_, p.filterCutoffHz, 1.0f);
                        break;
                    case 6:
                        flt1L_.setPeaking(sr_, p.filterCutoffHz, q, 6.0f + p.filterRes * 10.0f);
                        flt1R_.setPeaking(sr_, p.filterCutoffHz, q, 6.0f + p.filterRes * 10.0f);
                        break;
                    case 7:
                        flt1L_.setNotch(sr_, p.filterCutoffHz, q);
                        flt1R_.setNotch(sr_, p.filterCutoffHz, q);
                        break;
                }
            }
            l = flt1L_.process(l); r = flt1R_.process(r);
            if (p.filterType == 0 || p.filterType == 2 || p.filterType == 5) {
                l = flt2L_.process(l); r = flt2R_.process(r);
            }
        }

        if (p.eqOn) {
            float eqHash = p.eqLowFreq + p.eqLowGain * 100.0f + p.eqMidFreq + p.eqMidGain * 200.0f + p.eqHighFreq + p.eqHighGain * 300.0f;
            if (std::fabs(eqHash - lastEqHash_) > 0.05f) {
                lastEqHash_ = eqHash;
                eqLowL_.setPeaking(sr_, p.eqLowFreq, 0.8f, p.eqLowGain);
                eqLowR_.setPeaking(sr_, p.eqLowFreq, 0.8f, p.eqLowGain);
                eqMidL_.setPeaking(sr_, p.eqMidFreq, 0.9f, p.eqMidGain);
                eqMidR_.setPeaking(sr_, p.eqMidFreq, 0.9f, p.eqMidGain);
                eqHighL_.setPeaking(sr_, p.eqHighFreq, 0.8f, p.eqHighGain);
                eqHighR_.setPeaking(sr_, p.eqHighFreq, 0.8f, p.eqHighGain);
            }
            l = eqHighL_.process(eqMidL_.process(eqLowL_.process(l)));
            r = eqHighR_.process(eqMidR_.process(eqLowR_.process(r)));
        }

        if (p.compOn) {
            if (std::fabs(p.compAttackMs - lastCompAttMs_) > 0.01f) {
                lastCompAttMs_ = p.compAttackMs;
                compAttCoeff_ = fastExp(-1.0f / (sr_ * std::max(0.5f, p.compAttackMs) * 0.001f));
            }
            if (std::fabs(p.compReleaseMs - lastCompRelMs_) > 0.05f) {
                lastCompRelMs_ = p.compReleaseMs;
                compRelCoeff_ = fastExp(-1.0f / (sr_ * std::max(10.0f, p.compReleaseMs) * 0.001f));
            }
            float det = std::max(std::fabs(l), std::fabs(r));
            compEnv_ = (det > compEnv_) ? (compAttCoeff_ * compEnv_ + (1.0f - compAttCoeff_) * det)
                                        : (compRelCoeff_ * compEnv_ + (1.0f - compRelCoeff_) * det);
            float envDb = 20.0f * std::log10(std::max(1e-6f, compEnv_));
            float overDb = envDb - p.compThreshDb;
            float grDb = 0.0f;
            float halfKnee = p.compKneeDb * 0.5f;
            if (overDb > -halfKnee) {
                float effOver = (p.compKneeDb > 0.1f && overDb < halfKnee)
                    ? ((overDb + halfKnee) * (overDb + halfKnee) / (2.0f * p.compKneeDb))
                    : overDb;
                grDb = effOver * (1.0f - 1.0f / std::max(1.0f, p.compRatio));
            }
            lastGrDb_ = grDb;
            float totalGain = fastPow10((-grDb + p.compMakeupDb) / 20.0f);
            l *= totalGain; r *= totalGain;
        } else {
            lastGrDb_ = 0.0f;
        }

        // 4-Mode Pro Saturator (0:TAPE, 1:TUBE, 2:CRUSH, 3:CLIP) with Tilt EQ & Auto-Gain
        if (p.satOn && p.satMix > 0.005f) {
            float tiltDb = (p.satWarmth - 0.5f) * 8.0f; // -4 dB (Warm/Dark) .. +4 dB (Bright/Airy)
            if (std::fabs(tiltDb - lastWarmth_) > 0.05f) {
                lastWarmth_ = tiltDb;
                satLowL_.setPeaking(sr_, 160.0f, 0.7f, -tiltDb);
                satLowR_.setPeaking(sr_, 160.0f, 0.7f, -tiltDb);
                satHighL_.setPeaking(sr_, 3400.0f, 0.7f, tiltDb);
                satHighR_.setPeaking(sr_, 3400.0f, 0.7f, tiltDb);
            }
            float wL = satHighL_.process(satLowL_.process(l));
            float wR = satHighR_.process(satLowR_.process(r));

            float sL = wL, sR = wR;
            if (p.satMode == 0) { // TAPE: warm magnetic glue + subtle high rolloff
                float drv = 1.0f + p.satDrive * 4.5f;
                float comp = 1.0f / (1.0f + 0.42f * (drv - 1.0f));
                sL = satTapeLpL_.process(fastTanh(wL * drv) * comp * 1.15f);
                sR = satTapeLpR_.process(fastTanh(wR * drv) * comp * 1.15f);
            } else if (p.satMode == 1) { // TUBE: asymmetric even-harmonic triode warmth
                float drv = 1.0f + p.satDrive * 6.5f;
                float bias = 0.08f + p.satDrive * 0.14f;
                float bOff = fastTanh(bias);
                float comp = 1.0f / (1.0f + 0.45f * (drv - 1.0f));
                sL = satDcL_.process((fastTanh(wL * drv + bias) - bOff) * comp * 1.20f);
                sR = satDcR_.process((fastTanh(wR * drv + bias) - bOff) * comp * 1.20f);
            } else if (p.satMode == 2) { // CRUSH: aggressive transistor overdrive
                float drv = 1.0f + p.satDrive * 11.0f;
                float comp = 1.0f / (1.0f + 0.52f * (drv - 1.0f));
                auto shape = [&](float x) {
                    float a = x * drv;
                    return (a / (1.0f + std::fabs(a) * 0.85f)) * comp * 1.25f;
                };
                sL = shape(wL);
                sR = shape(wR);
            } else { // CLIP: punchy soft-knee drum clipper
                float drv = 1.0f + p.satDrive * 5.0f;
                auto clip = [&](float x) {
                    float a = std::clamp(x * drv, -1.5f, 1.5f);
                    return (a - (a * a * a) / 6.75f) * (1.0f / (1.0f + 0.38f * (drv - 1.0f))) * 1.12f;
                };
                sL = clip(wL);
                sR = clip(wR);
            }
            l = l * (1.0f - p.satMix) + sL * p.satMix;
            r = r * (1.0f - p.satMix) + sR * p.satMix;
        }

        if (p.delayOn && p.delayMix > 0.005f) {
            if (!wasDelayOn_) { delayBus_.reset(); wasDelayOn_ = true; }
            delayBus_.process(l, r, p.delayMode, p.delayTimeMs, p.delayFeedback, p.delayTone, p.delayWidth, p.delayMix);
            l = delayBus_.outL(); r = delayBus_.outR();
        } else {
            wasDelayOn_ = false;
        }

        if (p.revOn && p.revMix > 0.005f) {
            if (!wasRevOn_) { masterRevBus_.reset(); wasRevOn_ = true; }
            float revHash = p.revPreDelayMs + p.revDecaySec * 37.0f + p.revSize * 113.0f
                          + p.revLowCutHz * 0.17f + p.revHighCutHz * 0.013f + p.revMode * 10000.0f;
            if (std::fabs(revHash - lastRevHash_) > 0.01f) {
                lastRevHash_ = revHash;
                float modeScale = (p.revMode == 0) ? 0.55f : ((p.revMode == 1) ? 0.92f : 1.38f);
                float sizeBoost = (p.revMode == 0) ? 0.72f : ((p.revMode == 1) ? 0.95f : 1.15f);
                masterRevBus_.configure(p.revPreDelayMs, p.revDecaySec * modeScale, std::clamp(p.revSize * sizeBoost, 0.0f, 1.0f), p.revLowCutHz, p.revHighCutHz);
            }
            float rL = 0.0f, rR = 0.0f;
            masterRevBus_.process(l, r, rL, rR);
            l = l * (1.0f - p.revMix * 0.25f) + rL * p.revMix * 1.40f;
            r = r * (1.0f - p.revMix * 0.25f) + rR * p.revMix * 1.40f;
        } else {
            wasRevOn_ = false;
        }

        if (p.monoBass) {
            float lowL = monoBassL_.process(l), lowR = monoBassR_.process(r);
            float lowMono = 0.5f * (lowL + lowR);
            l = lowMono + (l - lowL);
            r = lowMono + (r - lowR);
        }

        if (p.punishOn && p.punishAmount > 0.005f) {
            float drive = 1.0f, asym = 0.0f, wetMix = p.punishAmount;
            if (p.punishMode == PunishMode::GentlyNow) {
                drive = 1.0f + p.punishAmount * 2.2f; asym = 0.02f;
            } else if (p.punishMode == PunishMode::HurtMePlenty) {
                drive = 1.0f + p.punishAmount * 5.5f; asym = 0.07f;
            } else {
                drive = 1.0f + p.punishAmount * 11.5f; asym = 0.14f;
            }
            float satL = dcL_.process(fastTanh(l * drive + asym) - fastTanh(asym));
            float satR = dcR_.process(fastTanh(r * drive + asym) - fastTanh(asym));
            float compNorm = 1.0f / (1.0f + 0.35f * (drive - 1.0f));
            l = l * (1.0f - wetMix * 0.85f) + satL * (wetMix * 1.15f * compNorm + 0.15f);
            r = r * (1.0f - wetMix * 0.85f) + satR * (wetMix * 1.15f * compNorm + 0.15f);
        }

        if (p.limiterOn) {
            constexpr float ceil = 0.966f;
            float pk = std::max(std::fabs(l), std::fabs(r));
            float target = (pk > ceil) ? (ceil / pk) : 1.0f;
            limGain_ = (target < limGain_) ? target : (limRel_ * limGain_ + (1.0f - limRel_) * target);
            l = std::clamp(l * limGain_, -ceil, ceil);
            r = std::clamp(r * limGain_, -ceil, ceil);
        }

        float pkOut = std::max(std::fabs(l), std::fabs(r));
        outEnv_ = (pkOut > outEnv_) ? pkOut : (outEnv_ * 0.9992f);
    }

private:
    float sr_ = 44100.0f;
    float lastFiltHash_ = -1.0f, lastEqHash_ = -1.0f, lastWarmth_ = -99.0f, lastRevHash_ = -1.0f;
    float lastCompAttMs_ = -1.0f, lastCompRelMs_ = -1.0f;
    float compAttCoeff_ = 0.99f, compRelCoeff_ = 0.999f;
    bool wasDelayOn_ = false, wasRevOn_ = false;
    float lastGrDb_ = 0.0f;
    float outEnv_ = 0.0f;
    Biquad flt1L_, flt1R_, flt2L_, flt2R_;
    Biquad eqLowL_, eqLowR_, eqMidL_, eqMidR_, eqHighL_, eqHighR_;
    Biquad satLowL_, satLowR_, satHighL_, satHighR_, satTapeLpL_, satTapeLpR_;
    Biquad monoBassL_, monoBassR_;
    DCBlocker dcL_, dcR_, satDcL_, satDcR_;
    StereoDelayBus delayBus_;
    StereoSpaceBus masterRevBus_;
    float compEnv_ = 0.0f;
    float limRel_ = 0.995f, limGain_ = 1.0f;
};

} // namespace NoiseMu6ix

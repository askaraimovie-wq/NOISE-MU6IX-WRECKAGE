#pragma once
#include "wreckage_params.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace NoiseMu6ix {

constexpr float kPi = 3.14159265358979323846f;

inline float fastTanh(float x) {
    if (x < -3.0f) return -1.0f;
    if (x >  3.0f) return  1.0f;
    float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

struct DCBlocker {
    float x1 = 0.0f, y1 = 0.0f;
    float r = 0.9965f;
    void setSampleRate(float sr) { r = std::exp(-2.0f * kPi * 20.0f / std::max(8000.0f, sr)); }
    void reset() { x1 = y1 = 0.0f; }
    inline float process(float x) {
        float y = x - x1 + r * y1;
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
        float A = std::pow(10.0f, gainDb / 40.0f);
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
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};

class StereoSpaceBus {
public:
    void prepare(float sampleRate, float preDelayMs, float decaySeconds, float hpHz, float lpHz) {
        sr_ = std::max(8000.0f, sampleRate);
        maxPreSamples_ = std::max(4, static_cast<int>(sr_ * 0.25f));
        preBufL_.assign(static_cast<size_t>(maxPreSamples_), 0.0f);
        preBufR_.assign(static_cast<size_t>(maxPreSamples_), 0.0f);
        preIdx_ = 0;

        static constexpr float baseMs[4] = { 23.7f, 31.3f, 41.9f, 53.1f };
        for (int i = 0; i < 4; ++i) {
            int maxLen = std::max(64, static_cast<int>(sr_ * baseMs[i] * 2.4f * 0.001f));
            lines_[i].assign(static_cast<size_t>(maxLen), 0.0f);
            activeLen_[i] = std::max(32, static_cast<int>(sr_ * baseMs[i] * 1.1f * 0.001f));
            idx_[i] = 0;
            dampState_[i] = 0.0f;
        }
        configure(preDelayMs, decaySeconds, 0.65f, hpHz, lpHz);
        hpL_.reset(); hpR_.reset(); lpL_.reset(); lpR_.reset();
    }

    void configure(float preDelayMs, float decaySeconds, float size01, float hpHz, float lpHz) {
        preDelaySamples_ = std::clamp(static_cast<int>(sr_ * preDelayMs * 0.001f), 1, maxPreSamples_ - 1);
        static constexpr float baseMs[4] = { 23.7f, 31.3f, 41.9f, 53.1f };
        float sizeScale = 0.45f + std::clamp(size01, 0.0f, 1.0f) * 1.65f;
        float rt = std::max(0.15f, decaySeconds);
        for (int i = 0; i < 4; ++i) {
            int maxL = static_cast<int>(lines_[i].size());
            int len = std::clamp(static_cast<int>(sr_ * baseMs[i] * sizeScale * 0.001f), 32, maxL);
            activeLen_[i] = len;
            if (idx_[i] >= len) idx_[i] = 0;
            fb_[i] = std::clamp(std::pow(10.0f, (-3.0f * (static_cast<float>(len) / sr_)) / rt), 0.15f, 0.968f);
        }
        dampCoeff_ = std::clamp(1.0f - (lpHz / (sr_ * 0.5f)), 0.12f, 0.88f);
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

        float d[4];
        for (int i = 0; i < 4; ++i) {
            float s = lines_[i][idx_[i]];
            dampState_[i] = s * (1.0f - dampCoeff_) + dampState_[i] * dampCoeff_;
            d[i] = dampState_[i] * fb_[i];
        }

        float h0 = 0.5f * (d[0] + d[1] + d[2] + d[3]);
        float h1 = 0.5f * (d[0] - d[1] + d[2] - d[3]);
        float h2 = 0.5f * (d[0] + d[1] - d[2] - d[3]);
        float h3 = 0.5f * (d[0] - d[1] - d[2] + d[3]);

        float inVals[4] = { h0 + pdL * 0.65f, h1 + pdR * 0.65f, h2 + pdL * 0.45f, h3 - pdR * 0.45f };
        for (int i = 0; i < 4; ++i) {
            lines_[i][idx_[i]] = inVals[i];
            if (++idx_[i] >= activeLen_[i]) idx_[i] = 0;
        }

        outL = lpL_.process(hpL_.process(0.85f * (d[0] + d[2])));
        outR = lpR_.process(hpR_.process(0.85f * (d[1] + d[3])));
    }

private:
    float sr_ = 44100.0f;
    int maxPreSamples_ = 11025;
    int preDelaySamples_ = 352;
    int preIdx_ = 0;
    std::vector<float> preBufL_, preBufR_;
    std::array<std::vector<float>, 4> lines_;
    std::array<int, 4> activeLen_{{100,100,100,100}};
    std::array<int, 4> idx_{{0,0,0,0}};
    std::array<float, 4> fb_{{0.5f,0.5f,0.5f,0.5f}};
    std::array<float, 4> dampState_{{0,0,0,0}};
    float dampCoeff_ = 0.4f;
    float lastHp_ = -1.0f, lastLp_ = -1.0f;
    Biquad hpL_, hpR_, lpL_, lpR_;
};

class StereoDelayBus {
public:
    void prepare(float sr) {
        sr_ = std::max(8000.0f, sr);
        maxSamples_ = static_cast<int>(sr_ * 1.2f);
        bufL_.assign(static_cast<size_t>(maxSamples_), 0.0f);
        bufR_.assign(static_cast<size_t>(maxSamples_), 0.0f);
        writeIdx_ = 0;
        hpL_.setHighPass(sr_, 130.0f); hpR_.setHighPass(sr_, 130.0f);
        lpL_.setLowPass(sr_, 6500.0f); lpR_.setLowPass(sr_, 6500.0f);
        hpL_.reset(); hpR_.reset(); lpL_.reset(); lpR_.reset();
    }

    inline void process(float inL, float inR, bool vintage, float timeMs, float feedback, float tone01, float width01, float mix01) {
        int delaySamp = std::clamp(static_cast<int>(sr_ * timeMs * 0.001f), 8, maxSamples_ - 2);
        int readL = writeIdx_ - delaySamp;
        if (readL < 0) readL += maxSamples_;
        int readR = writeIdx_ - static_cast<int>(delaySamp * (0.85f + 0.15f * (1.0f - width01)));
        if (readR < 0) readR += maxSamples_;

        float dL = bufL_[readL];
        float dR = bufR_[readR];

        float cutHz = 1200.0f + tone01 * 13000.0f;
        if (std::fabs(cutHz - lastCut_) > 25.0f) {
            lpL_.setLowPass(sr_, cutHz); lpR_.setLowPass(sr_, cutHz); lastCut_ = cutHz;
        }
        dL = lpL_.process(hpL_.process(dL));
        dR = lpR_.process(hpR_.process(dR));
        if (vintage) {
            dL = fastTanh(dL * 1.45f) * 0.85f;
            dR = fastTanh(dR * 1.45f) * 0.85f;
        }

        float fb = std::clamp(feedback, 0.0f, 0.88f);
        float cross = width01 * 0.65f;
        float nextL = inL + fb * (dL * (1.0f - cross) + dR * cross);
        float nextR = inR + fb * (dR * (1.0f - cross) + dL * cross);

        bufL_[writeIdx_] = nextL;
        bufR_[writeIdx_] = nextR;
        if (++writeIdx_ >= maxSamples_) writeIdx_ = 0;

        inL_ = inL * (1.0f - mix01 * 0.4f) + dL * mix01;
        inR_ = inR * (1.0f - mix01 * 0.4f) + dR * mix01;
    }

    float outL() const { return inL_; }
    float outR() const { return inR_; }

private:
    float sr_ = 44100.0f;
    int maxSamples_ = 48000;
    int writeIdx_ = 0;
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
        lp1_.reset(); lp2_.reset(); subPeak_.reset(); dc_.reset();
    }
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
    float compMakeupDb = 0.0f; // Default 0.0 dB

    bool  satOn = false;
    float satDrive = 0.25f;
    float satWarmth = 0.50f;
    float satMix = 0.0f; // Default 0.0 (100% Dry / 0% Wet)

    bool  delayOn = false;
    int   delayMode = 0;
    float delayTimeMs = 240.0f;
    float delayFeedback = 0.32f;
    float delayTone = 0.65f;
    float delayWidth = 0.70f;
    float delayMix = 0.22f;

    bool  revOn = false;
    int   revMode = 2;
    float revPreDelayMs = 18.0f;
    float revDecaySec = 2.2f;
    float revSize = 0.65f;
    float revLowCutHz = 140.0f;
    float revHighCutHz = 7500.0f;
    float revMix = 0.24f;

    bool  limiterOn = true;
    bool  monoBass = true;

    bool  punishOn = true;
    float punishAmount = 0.30f;
    PunishMode punishMode = PunishMode::HurtMePlenty;
};

class MasterFxRack {
public:
    void prepare(float sr) {
        sr_ = std::max(8000.0f, sr);
        dcL_.setSampleRate(sr_); dcR_.setSampleRate(sr_);
        monoBassL_.setLowPass(sr_, 120.0f); monoBassR_.setLowPass(sr_, 120.0f);
        delayBus_.prepare(sr_);
        masterRevBus_.prepare(sr_, 18.0f, 2.2f, 140.0f, 7500.0f);
        limRel_ = std::exp(-1.0f / (sr_ * 0.045f));
        lastFiltHash_ = -1.0f;
        lastEqHash_ = -1.0f;
    }

    float currentGrDb() const { return lastGrDb_; }

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
            float att = std::exp(-1.0f / (sr_ * std::max(0.5f, p.compAttackMs) * 0.001f));
            float rel = std::exp(-1.0f / (sr_ * std::max(10.0f, p.compReleaseMs) * 0.001f));
            float det = std::max(std::fabs(l), std::fabs(r));
            compEnv_ = (det > compEnv_) ? (att * compEnv_ + (1.0f - att) * det)
                                        : (rel * compEnv_ + (1.0f - rel) * det);
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
            float totalGain = std::pow(10.0f, (-grDb + p.compMakeupDb) / 20.0f);
            l *= totalGain; r *= totalGain;
        } else {
            lastGrDb_ = 0.0f;
        }

        if (p.satOn && p.satMix > 0.005f) {
            float drv = 1.0f + p.satDrive * 7.5f;
            float warmTilt = (p.satWarmth - 0.5f) * 6.0f;
            if (std::fabs(warmTilt - lastWarmth_) > 0.1f) {
                satWarmL_.setPeaking(sr_, 160.0f, 0.7f, warmTilt);
                satWarmR_.setPeaking(sr_, 160.0f, 0.7f, warmTilt);
                lastWarmth_ = warmTilt;
            }
            float wL = satWarmL_.process(l);
            float wR = satWarmR_.process(r);
            float sL = fastTanh(wL * drv) / (1.0f + 0.35f * (drv - 1.0f));
            float sR = fastTanh(wR * drv) / (1.0f + 0.35f * (drv - 1.0f));
            l = l * (1.0f - p.satMix) + sL * p.satMix;
            r = r * (1.0f - p.satMix) + sR * p.satMix;
        }

        if (p.delayOn && p.delayMix > 0.005f) {
            delayBus_.process(l, r, p.delayMode == 1, p.delayTimeMs, p.delayFeedback, p.delayTone, p.delayWidth, p.delayMix);
            l = delayBus_.outL(); r = delayBus_.outR();
        }

        if (p.revOn && p.revMix > 0.005f) {
            float modeScale = (p.revMode == 0) ? 0.55f : ((p.revMode == 1) ? 0.85f : 1.25f);
            masterRevBus_.configure(p.revPreDelayMs, p.revDecaySec * modeScale, p.revSize, p.revLowCutHz, p.revHighCutHz);
            float rL = 0.0f, rR = 0.0f;
            masterRevBus_.process(l, r, rL, rR);
            l = l * (1.0f - p.revMix * 0.35f) + rL * p.revMix;
            r = r * (1.0f - p.revMix * 0.35f) + rR * p.revMix;
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
    }

private:
    float sr_ = 44100.0f;
    float lastFiltHash_ = -1.0f, lastEqHash_ = -1.0f, lastWarmth_ = -99.0f;
    float lastGrDb_ = 0.0f;
    Biquad flt1L_, flt1R_, flt2L_, flt2R_;
    Biquad eqLowL_, eqLowR_, eqMidL_, eqMidR_, eqHighL_, eqHighR_;
    Biquad satWarmL_, satWarmR_;
    Biquad monoBassL_, monoBassR_;
    DCBlocker dcL_, dcR_;
    StereoDelayBus delayBus_;
    StereoSpaceBus masterRevBus_;
    float compEnv_ = 0.0f;
    float limRel_ = 0.995f, limGain_ = 1.0f;
};

} // namespace NoiseMu6ix

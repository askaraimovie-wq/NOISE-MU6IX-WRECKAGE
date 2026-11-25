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
    void setSampleRate(float sr) {
        r = std::exp(-2.0f * kPi * 20.0f / std::max(8000.0f, sr));
    }
    void reset() { x1 = y1 = 0.0f; }
    inline float process(float x) {
        float y = x - x1 + r * y1;
        x1 = x;
        y1 = y;
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
        preDelaySamples_ = std::max(1, static_cast<int>(sr_ * preDelayMs * 0.001f));
        preBufL_.assign(static_cast<size_t>(preDelaySamples_ + 4), 0.0f);
        preBufR_.assign(static_cast<size_t>(preDelaySamples_ + 4), 0.0f);
        preIdx_ = 0;

        static constexpr float baseMs[4] = { 23.7f, 31.3f, 41.9f, 53.1f };
        float scale = (decaySeconds > 1.2f) ? 1.65f : 0.78f;
        for (int i = 0; i < 4; ++i) {
            int len = std::max(32, static_cast<int>(sr_ * baseMs[i] * scale * 0.001f));
            lines_[i].assign(static_cast<size_t>(len), 0.0f);
            idx_[i] = 0;
            dampState_[i] = 0.0f;
            float rt = std::max(0.15f, decaySeconds);
            fb_[i] = std::pow(10.0f, (-3.0f * (static_cast<float>(len) / sr_)) / rt);
            fb_[i] = std::clamp(fb_[i], 0.20f, 0.965f);
        }
        dampCoeff_ = std::clamp(1.0f - (lpHz / (sr_ * 0.5f)), 0.15f, 0.85f);
        hpL_.setHighPass(sr_, hpHz); hpR_.setHighPass(sr_, hpHz);
        lpL_.setLowPass(sr_, lpHz);  lpR_.setLowPass(sr_, lpHz);
        hpL_.reset(); hpR_.reset(); lpL_.reset(); lpR_.reset();
    }

    inline void process(float inL, float inR, float& outL, float& outR) {
        float pdL = preBufL_[preIdx_];
        float pdR = preBufR_[preIdx_];
        preBufL_[preIdx_] = inL;
        preBufR_[preIdx_] = inR;
        if (++preIdx_ >= preDelaySamples_) preIdx_ = 0;

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
            if (++idx_[i] >= static_cast<int>(lines_[i].size())) idx_[i] = 0;
        }

        outL = lpL_.process(hpL_.process(0.85f * (d[0] + d[2])));
        outR = lpR_.process(hpR_.process(0.85f * (d[1] + d[3])));
    }

private:
    float sr_ = 44100.0f;
    int preDelaySamples_ = 352;
    int preIdx_ = 0;
    std::vector<float> preBufL_, preBufR_;
    std::array<std::vector<float>, 4> lines_;
    std::array<int, 4> idx_{{0,0,0,0}};
    std::array<float, 4> fb_{{0.5f,0.5f,0.5f,0.5f}};
    std::array<float, 4> dampState_{{0,0,0,0}};
    float dampCoeff_ = 0.4f;
    Biquad hpL_, hpR_, lpL_, lpR_;
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
        outL = shaped;
        outR = shaped;
    }
private:
    Biquad lp1_, lp2_, subPeak_;
    DCBlocker dc_;
};

class MasterFxRack {
public:
    void prepare(float sr) {
        sr_ = std::max(8000.0f, sr);
        dcL_.setSampleRate(sr_); dcR_.setSampleRate(sr_);
        monoBassL_.setLowPass(sr_, 120.0f); monoBassR_.setLowPass(sr_, 120.0f);
        fastAtt_ = std::exp(-1.0f / (sr_ * 0.002f));
        fastRel_ = std::exp(-1.0f / (sr_ * 0.035f));
        slowAtt_ = std::exp(-1.0f / (sr_ * 0.025f));
        slowRel_ = std::exp(-1.0f / (sr_ * 0.140f));
        compAtt_ = std::exp(-1.0f / (sr_ * 0.008f));
        compRel_ = std::exp(-1.0f / (sr_ * 0.095f));
        limRel_  = std::exp(-1.0f / (sr_ * 0.045f));
        updateFilters(20.0f, 20000.0f, 0.0f, 0.0f, 0.0f);
    }

    void updateFilters(float hpfHz, float lpfHz, float lowDb, float midDb, float highDb) {
        if (std::fabs(hpfHz - lastHpf_) > 0.5f) {
            hpfL_.setHighPass(sr_, hpfHz); hpfR_.setHighPass(sr_, hpfHz); lastHpf_ = hpfHz;
        }
        if (std::fabs(lpfHz - lastLpf_) > 5.0f) {
            lpfL_.setLowPass(sr_, lpfHz); lpfR_.setLowPass(sr_, lpfHz); lastLpf_ = lpfHz;
        }
        if (std::fabs(lowDb - lastLow_) > 0.05f) {
            eqLowL_.setPeaking(sr_, 90.0f, 0.8f, lowDb); eqLowR_.setPeaking(sr_, 90.0f, 0.8f, lowDb); lastLow_ = lowDb;
        }
        if (std::fabs(midDb - lastMid_) > 0.05f) {
            eqMidL_.setPeaking(sr_, 1100.0f, 0.9f, midDb); eqMidR_.setPeaking(sr_, 1100.0f, 0.9f, midDb); lastMid_ = midDb;
        }
        if (std::fabs(highDb - lastHigh_) > 0.05f) {
            eqHighL_.setPeaking(sr_, 7500.0f, 0.8f, highDb); eqHighR_.setPeaking(sr_, 7500.0f, 0.8f, highDb); lastHigh_ = highDb;
        }
    }

    inline void process(float& l, float& r,
                        bool filterOn, bool eqOn, bool compOn, bool transientOn, bool punishOn,
                        float compAmt, float transientPunch, float stereoWidth, bool monoBass,
                        float punishAmt, PunishMode punishMode, bool limiterOn, float outGain) {
        if (filterOn) {
            l = lpfL_.process(hpfL_.process(l));
            r = lpfR_.process(hpfR_.process(r));
        }
        if (eqOn) {
            l = eqHighL_.process(eqMidL_.process(eqLowL_.process(l)));
            r = eqHighR_.process(eqMidR_.process(eqLowR_.process(r)));
        }

        if (transientOn) {
            float punchDelta = (transientPunch - 0.5f) * 2.0f;
            if (std::fabs(punchDelta) > 0.01f) {
                float det = std::max(std::fabs(l), std::fabs(r));
                envFast_ = (det > envFast_) ? (fastAtt_ * envFast_ + (1.0f - fastAtt_) * det)
                                            : (fastRel_ * envFast_ + (1.0f - fastRel_) * det);
                envSlow_ = (det > envSlow_) ? (slowAtt_ * envSlow_ + (1.0f - slowAtt_) * det)
                                            : (slowRel_ * envSlow_ + (1.0f - slowRel_) * det);
                float trans = std::max(0.0f, envFast_ - envSlow_);
                float gain = std::clamp(1.0f + punchDelta * trans * 3.2f, 0.35f, 2.4f);
                l *= gain; r *= gain;
            }
        }

        if (compOn && compAmt > 0.01f) {
            float det = std::max(std::fabs(l), std::fabs(r));
            compEnv_ = (det > compEnv_) ? (compAtt_ * compEnv_ + (1.0f - compAtt_) * det)
                                        : (compRel_ * compEnv_ + (1.0f - compRel_) * det);
            float thresh = 0.85f - compAmt * 0.65f;
            float gr = 1.0f;
            if (compEnv_ > thresh) {
                float over = compEnv_ - thresh;
                gr = (thresh + over * (1.0f - compAmt * 0.72f)) / std::max(1e-6f, compEnv_);
            }
            float makeup = 1.0f + compAmt * 0.55f;
            l *= gr * makeup; r *= gr * makeup;
        }

        float w = (stereoWidth <= 0.5f) ? (stereoWidth * 2.0f) : (1.0f + (stereoWidth - 0.5f));
        if (monoBass) {
            float lowL = monoBassL_.process(l), lowR = monoBassR_.process(r);
            float lowMono = 0.5f * (lowL + lowR);
            float hiL = l - lowL, hiR = r - lowR;
            float mid = 0.5f * (hiL + hiR), side = 0.5f * (hiL - hiR) * w;
            l = lowMono + mid + side;
            r = lowMono + mid - side;
        } else {
            float mid = 0.5f * (l + r), side = 0.5f * (l - r) * w;
            l = mid + side; r = mid - side;
        }

        if (punishOn && punishAmt > 0.005f) {
            float drive = 1.0f;
            float asym = 0.0f;
            float wetMix = punishAmt;
            if (punishMode == PunishMode::GentlyNow) {
                drive = 1.0f + punishAmt * 2.2f;
                asym = 0.02f;
            } else if (punishMode == PunishMode::HurtMePlenty) {
                drive = 1.0f + punishAmt * 5.5f;
                asym = 0.07f;
            } else {
                drive = 1.0f + punishAmt * 11.5f;
                asym = 0.14f;
            }
            float satL = dcL_.process(fastTanh(l * drive + asym) - fastTanh(asym));
            float satR = dcR_.process(fastTanh(r * drive + asym) - fastTanh(asym));
            float compNorm = 1.0f / (1.0f + 0.35f * (drive - 1.0f));
            l = l * (1.0f - wetMix * 0.85f) + satL * (wetMix * 1.15f * compNorm + 0.15f);
            r = r * (1.0f - wetMix * 0.85f) + satR * (wetMix * 1.15f * compNorm + 0.15f);
        }

        l *= outGain; r *= outGain;
        if (limiterOn) {
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
    float lastHpf_ = -1.0f, lastLpf_ = -1.0f, lastLow_ = -99.0f, lastMid_ = -99.0f, lastHigh_ = -99.0f;
    Biquad hpfL_, hpfR_, lpfL_, lpfR_, eqLowL_, eqLowR_, eqMidL_, eqMidR_, eqHighL_, eqHighR_;
    Biquad monoBassL_, monoBassR_;
    DCBlocker dcL_, dcR_;
    float fastAtt_ = 0.9f, fastRel_ = 0.99f, slowAtt_ = 0.98f, slowRel_ = 0.999f;
    float envFast_ = 0.0f, envSlow_ = 0.0f;
    float compAtt_ = 0.95f, compRel_ = 0.998f, compEnv_ = 0.0f;
    float limRel_ = 0.995f, limGain_ = 1.0f;
};

} // namespace NoiseMu6ix

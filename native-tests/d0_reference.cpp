// D1 harness only — FROZEN copy of app/src/main/cpp/PitchCorrector.cpp as of
// the D0 baseline commit (a6020ae). Renamed to namespace d0ref so it can link
// next to the live detector. Do not modify; see d0_reference.h.

#include "d0_reference.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace d0ref {
namespace {
constexpr float kPi = 3.14159265358979323846f;
constexpr float kA4 = 440.0f;

int mod12(int n) noexcept { return (n % 12 + 12) % 12; }
float clampf(float x, float lo, float hi) noexcept { return std::max(lo, std::min(hi, x)); }
float fract(float x) noexcept { return x - std::floor(x); }
}  // namespace

PitchCorrectorD0::PitchCorrectorD0(int sampleRate) { reset(sampleRate); }

void PitchCorrectorD0::reset(int sampleRate) noexcept {
    sampleRate_ = std::max(8000, sampleRate);
    history_.fill(0.0f);
    echoA_.fill(0.0f);
    echoB_.fill(0.0f);
    totalSamples_ = 0;
    frameSinceDetection_ = 0;
    reverbIndexA_ = 0;
    reverbIndexB_ = 0;
    phase_ = 0.0f;
    smoothedRatio_ = 1.0f;
    lastTargetRatio_ = 1.0f;
    lastFrequencyHz_ = 0.0f;
    lastTargetHz_ = 0.0f;
    stableCount_ = 0;
    detectedHz_.store(0.0f);
    targetHz_.store(0.0f);
    cents_.store(0.0f);
    voiced_.store(false);
}

void PitchCorrectorD0::setParameters(int root, int scale, float amount, float retuneMs, float reverb) noexcept {
    root_.store(mod12(root), std::memory_order_relaxed);
    scale_.store(std::max(0, std::min(scale, 2)), std::memory_order_relaxed);
    amount_.store(clampf(amount, 0.0f, 1.0f), std::memory_order_relaxed);
    retuneMs_.store(clampf(retuneMs, 5.0f, 150.0f), std::memory_order_relaxed);
    reverb_.store(clampf(reverb, 0.0f, 0.25f), std::memory_order_relaxed);
}

float PitchCorrectorD0::nearestScaleFrequency(float hz, int root, int scale) noexcept {
    if (!std::isfinite(hz) || hz < 40.0f || hz > 1800.0f) return 0.0f;
    const float note = 69.0f + 12.0f * std::log2(hz / kA4);
    static constexpr bool major[12] = {true,false,true,false,true,true,false,true,false,true,false,true};
    static constexpr bool minor[12] = {true,false,true,true,false,true,false,true,true,false,true,false};
    const bool* mask = scale == 1 ? minor : major;
    const int low = static_cast<int>(std::floor(note)) - 3;
    const int high = static_cast<int>(std::ceil(note)) + 3;
    float bestNote = std::round(note);
    float bestDistance = std::numeric_limits<float>::max();
    for (int i = low; i <= high; ++i) {
        if (scale != 2 && !mask[mod12(i - root)]) continue;
        const float distance = std::abs(note - static_cast<float>(i));
        if (distance < bestDistance) {
            bestDistance = distance;
            bestNote = static_cast<float>(i);
        }
    }
    return kA4 * std::pow(2.0f, (bestNote - 69.0f) / 12.0f);
}

PitchResult PitchCorrectorD0::detect() const noexcept {
    constexpr int kCompare = 256;
    std::array<float, kDetectWindow> samples{};
    float squared = 0.0f;
    const std::uint64_t first = totalSamples_ - kDetectWindow * kDownsample;
    for (int i = 0; i < kDetectWindow; ++i) {
        // Lightweight decimator; pitch detector is not the sound path.
        const float s = history_[(first + i * kDownsample) & (kHistorySize - 1)];
        samples[i] = s;
        squared += s * s;
    }
    if (squared / static_cast<float>(kDetectWindow) < 0.000040f) return {};

    const float reducedRate = static_cast<float>(sampleRate_) / kDownsample;
    const int minLag = std::max(2, static_cast<int>(reducedRate / 1000.0f));
    const int maxLag = std::min(200, static_cast<int>(reducedRate / 80.0f));
    float running = 0.0f;
    int bestLag = 0;
    float previous = 1.0f;
    bool enteredTrough = false;
    std::array<float, 201> norm{};
    norm[0] = 1.0f;
    for (int lag = 1; lag <= maxLag; ++lag) {
        float difference = 0.0f;
        for (int i = 0; i < kCompare; ++i) {
            const float d = samples[i] - samples[i + lag];
            difference += d * d;
        }
        running += difference;
        const float cmnd = running > 1e-9f ? difference * lag / running : 1.0f;
        norm[lag] = cmnd;
        if (lag >= minLag && cmnd < 0.16f && !enteredTrough) enteredTrough = true;
        // Select the FIRST local trough, not the global minimum (which can be an octave below).
        if (enteredTrough && lag > minLag && cmnd > previous) {
            bestLag = lag - 1;
            break;
        }
        previous = cmnd;
    }
    if (!enteredTrough || bestLag < minLag || norm[bestLag] > 0.27f) return {};

    float refined = static_cast<float>(bestLag);
    if (bestLag > minLag && bestLag + 1 <= maxLag && norm[bestLag + 1] > 0.0f) {
        const float left = norm[bestLag - 1];
        const float middle = norm[bestLag];
        const float right = norm[bestLag + 1];
        const float denominator = left - 2.0f * middle + right;
        if (std::abs(denominator) > 1e-6f) {
            refined += clampf(0.5f * (left - right) / denominator, -0.5f, 0.5f);
        }
    }
    const float hz = reducedRate / refined;
    if (hz < 80.0f || hz > 1000.0f || !std::isfinite(hz)) return {};
    return {hz, clampf(1.0f - norm[bestLag], 0.0f, 1.0f)};
}

float PitchCorrectorD0::delayed(float lagSamples) const noexcept {
    // Mask BEFORE float conversion: absolute frame indexes lose single-sample precision after ~6 min.
    float pos = static_cast<float>((totalSamples_ - 1) & (kHistorySize - 1)) - lagSamples;
    const float base = std::floor(pos);
    const float alpha = pos - base;
    const std::int64_t i = static_cast<std::int64_t>(base);
    const float a = history_[static_cast<std::uint64_t>(i) & (kHistorySize - 1)];
    const float b = history_[static_cast<std::uint64_t>(i + 1) & (kHistorySize - 1)];
    return a + (b - a) * alpha;
}

float PitchCorrectorD0::shift(float ratio) noexcept {
    // Two Hann-windowed read heads. Their grain boundaries occur at zero weight.
    phase_ = fract(phase_ + (ratio - 1.0f) / kShiftWindow);
    const float second = fract(phase_ + 0.5f);
    const float a = delayed(kBaseDelay + kShiftWindow * (1.0f - phase_));
    const float b = delayed(kBaseDelay + kShiftWindow * (1.0f - second));
    const float weight = 0.5f - 0.5f * std::cos(2.0f * kPi * phase_);
    return a * weight + b * (1.0f - weight);
}

void PitchCorrectorD0::updateTarget() noexcept {
    const PitchResult detected = detect();
    const float hz = detected.frequencyHz;
    if (hz <= 0.0f || detected.confidence < 0.73f) {
        stableCount_ = 0;
        lastFrequencyHz_ = 0.0f;
        lastTargetHz_ = 0.0f;
        lastTargetRatio_ = 1.0f;
        detectedHz_.store(0.0f);
        targetHz_.store(0.0f);
        cents_.store(0.0f);
        voiced_.store(false);
        return;
    }
    // Require two agreeing adjacent estimates; reject wild 1-octave jumps.
    if (lastFrequencyHz_ > 0.0f && std::abs(12.0f * std::log2(hz / lastFrequencyHz_)) < 1.7f) {
        stableCount_ = std::min(stableCount_ + 1, 10);
    } else {
        stableCount_ = 0;
    }
    lastFrequencyHz_ = hz;
    detectedHz_.store(hz);
    if (stableCount_ < 1) {
        lastTargetRatio_ = 1.0f;
        voiced_.store(false);
        return;
    }
    const float target = nearestScaleFrequency(hz, root_.load(), scale_.load());
    if (target <= 0.0f) return;
    lastTargetHz_ = target;
    const float correction = 1200.0f * std::log2(target / hz);
    const float fraction = amount_.load(std::memory_order_relaxed);
    lastTargetRatio_ = std::pow(2.0f, clampf(correction, -300.0f, 300.0f) * fraction / 1200.0f);
    targetHz_.store(target);
    cents_.store(correction * fraction);
    voiced_.store(true);
}

void PitchCorrectorD0::process(const float* input, float* output, int frames, int outChannels) noexcept {
    if (!input || !output || frames <= 0 || outChannels <= 0) return;
    const float amount = amount_.load(std::memory_order_relaxed);
    const float reverb = reverb_.load(std::memory_order_relaxed);
    const float retune = retuneMs_.load(std::memory_order_relaxed);
    const float alpha = 1.0f - std::exp(-1.0f / (sampleRate_ * (retune / 1000.0f)));
    for (int frame = 0; frame < frames; ++frame) {
        const float in = clampf(std::isfinite(input[frame]) ? input[frame] : 0.0f, -1.0f, 1.0f);
        history_[totalSamples_ & (kHistorySize - 1)] = in;
        ++totalSamples_;
        if (totalSamples_ >= kDetectWindow * kDownsample && ++frameSinceDetection_ >= kDetectHop) {
            frameSinceDetection_ = 0;
            updateTarget();
        }
        smoothedRatio_ += alpha * (lastTargetRatio_ - smoothedRatio_);
        const float processed = amount < 0.001f ? in : shift(smoothedRatio_);
        const float revA = echoA_[reverbIndexA_];
        const float revB = echoB_[reverbIndexB_];
        echoA_[reverbIndexA_] = processed + revA * 0.32f;
        echoB_[reverbIndexB_] = processed + revB * 0.26f;
        reverbIndexA_ = (reverbIndexA_ + 1) % kReverbSizeA;
        reverbIndexB_ = (reverbIndexB_ + 1) % kReverbSizeB;
        const float mixed = processed * (1.0f - reverb) + (revA + revB) * (0.5f * reverb);
        // Protect headphone output against occasional overshoots.
        const float limited = clampf(mixed / (1.0f + 0.20f * std::abs(mixed)), -0.88f, 0.88f);
        for (int c = 0; c < outChannels; ++c) output[frame * outChannels + c] = limited;
    }
}

}  // namespace d0ref

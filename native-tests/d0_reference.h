#pragma once

// D1 harness only — FROZEN copy of the PitchCorrector public/private shape as
// of the D0 baseline commit (a6020ae, detector unchanged since). It exists so
// test_d1_improvement can run the old and new detectors side by side on the
// same corpus in one binary. Do not "improve" this file — if the production
// detector changes, this copy stays put so the D0 column stays honest.

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace d0ref {

struct PitchResult {
    float frequencyHz = 0.0f;
    float confidence = 0.0f;
};

class PitchCorrectorD0 final {
public:
    explicit PitchCorrectorD0(int sampleRate = 48000);
    void reset(int sampleRate) noexcept;
    void setParameters(int root, int scale, float amount, float retuneMs, float reverb) noexcept;
    void process(const float* input, float* interleavedOutput, int frames, int outChannels) noexcept;

    float detectedHz() const noexcept { return detectedHz_.load(std::memory_order_relaxed); }
    float targetHz() const noexcept { return targetHz_.load(std::memory_order_relaxed); }
    float correctionCents() const noexcept { return cents_.load(std::memory_order_relaxed); }
    bool voiced() const noexcept { return voiced_.load(std::memory_order_relaxed); }

    static float nearestScaleFrequency(float hz, int root, int scale) noexcept;

private:
    static constexpr int kHistorySize = 16384;  // Power of two.
    static constexpr int kDetectWindow = 512;
    static constexpr int kDownsample = 4;
    static constexpr int kDetectHop = 512;
    static constexpr int kShiftWindow = 1024;
    static constexpr int kBaseDelay = 384;
    static constexpr int kReverbSizeA = 1631;
    static constexpr int kReverbSizeB = 2111;

    PitchResult detect() const noexcept;
    float delayed(float lagSamples) const noexcept;
    float shift(float ratio) noexcept;
    void updateTarget() noexcept;

    int sampleRate_ = 48000;
    std::array<float, kHistorySize> history_{};
    std::array<float, kReverbSizeA> echoA_{};
    std::array<float, kReverbSizeB> echoB_{};
    std::uint64_t totalSamples_ = 0;
    int frameSinceDetection_ = 0;
    std::size_t reverbIndexA_ = 0;
    std::size_t reverbIndexB_ = 0;
    float phase_ = 0.0f;
    float smoothedRatio_ = 1.0f;
    float lastTargetRatio_ = 1.0f;
    float lastFrequencyHz_ = 0.0f;
    float lastTargetHz_ = 0.0f;
    int stableCount_ = 0;

    std::atomic<int> root_{0};
    std::atomic<int> scale_{0};
    std::atomic<float> amount_{0.9f};
    std::atomic<float> retuneMs_{35.0f};
    std::atomic<float> reverb_{0.0f};

    std::atomic<float> detectedHz_{0.0f};
    std::atomic<float> targetHz_{0.0f};
    std::atomic<float> cents_{0.0f};
    std::atomic<bool> voiced_{false};
};

}  // namespace d0ref

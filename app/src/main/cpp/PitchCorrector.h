#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace dautotune {

struct PitchResult {
    float frequencyHz = 0.0f;
    float confidence = 0.0f;
};

// Zero heap allocations in process(); designed for a single real-time audio thread.
class PitchCorrector final {
public:
    explicit PitchCorrector(int sampleRate = 48000);
    void reset(int sampleRate) noexcept;
    void setParameters(int root, int scale, float amount, float retuneMs, float reverb) noexcept;
    void process(const float* input, float* interleavedOutput, int frames, int outChannels) noexcept;

    // These methods are safe to read on a UI thread.
    float detectedHz() const noexcept { return detectedHz_.load(std::memory_order_relaxed); }
    float targetHz() const noexcept { return targetHz_.load(std::memory_order_relaxed); }
    float correctionCents() const noexcept { return cents_.load(std::memory_order_relaxed); }
    bool voiced() const noexcept { return voiced_.load(std::memory_order_relaxed); }

    // Exposed for deterministic, offline unit tests, not used by the audio callback.
    static float nearestScaleFrequency(float hz, int root, int scale) noexcept;
    // D2 test seam: pushes one detector output through the tracking state
    // machine exactly as updateTarget() does, minus detect() itself (so the
    // energy/armed bookkeeping is untouched). Unit tests only.
    void updateTargetForTest(float hz, float conf, bool relaxed = false,
                             bool provisional = false, float leadSec = 0.0f) noexcept;
    // D3 test seam: pins the shifter's applied F0 and correction ratio so the
    // render path can be exercised at shifts the +/-300-cent scale-snap law
    // cannot produce (the D3 formant gate needs +500 cents). Pin while
    // appliedHz > 0 and ratio > 0; appliedHz <= 0 releases it. Unit tests only.
    void psolaDriveForTest(float appliedHz, float ratio) noexcept;

private:
    static constexpr int kHistorySize = 16384;  // Power of two.
    static constexpr int kDetectWindow = 512;
    static constexpr int kDownsample = 4;
    static constexpr int kDetectHop = 512;
    static constexpr int kReverbSizeA = 1631;
    static constexpr int kReverbSizeB = 2111;
    // D3 TD-PSOLA bounds. The shifter follows the detector's 80..1000 Hz F0
    // range; each grain spans 2 periods of the applied F0.
    static constexpr int kPsolaMaxGrains = 6;
    static constexpr float kPsolaMinF0 = 80.0f;
    static constexpr float kPsolaMaxF0 = 1000.0f;
    // V/UV crossfade ~5 ms, and applied-F0 movement above ~60 cents (note
    // change / tracker reseed) re-anchors the grain grid.
    static constexpr float kUvXfadeSec = 0.005f;
    static constexpr float kReseedCents = 60.0f;
    // Transient guard: a >4x jump over the running peak envelope routes the
    // output through the delay-aligned passthrough for 2.5 applied periods,
    // so a click is emitted once (marks would otherwise re-read it for r>1).
    static constexpr float kTransRatio = 4.0f;
    static constexpr float kTransFloor = 0.05f;
    static constexpr float kTransHold = 2.5f;   // in applied periods
    static constexpr float kTransDecay = 0.998f; // ~20 ms envelope tail @48k

    // Internal detector output (D1): richer than the public PitchResult —
    // carries the analysis staleness and pass provenance used by updateTarget.
    struct DetOut {
        float hz = 0.0f;        // raw estimate, 0 = no periodic candidate
        float conf = 0.0f;      // 1 - CMND trough depth of the winning pass
        float leadSec = 0.0f;   // effective staleness of the estimate, seconds
        bool relaxed = false;   // admitted only below the strict thresholds
        bool provisional = false; // fresh short-window pass: confirm before report
    };

    DetOut detect() noexcept;
    float delayed(float lagSamples) const noexcept;
    float histAt(double absoluteIndex) const noexcept;
    float psolaShift(float ratio) noexcept;
    void psolaResync(float period, std::uint64_t now) noexcept;
    void updateTarget() noexcept;
    // D2: shared post-detection logic for updateTarget() and the test seam.
    void applyDetection(const DetOut& r) noexcept;
    void reportUnvoiced(bool hard) noexcept;
    float trackMedian() const noexcept;
    void pushTrack(float hz) noexcept;
    void reseedTrack(float hz) noexcept;

    int sampleRate_ = 48000;
    std::array<float, kHistorySize> history_{};
    std::array<float, kReverbSizeA> echoA_{};
    std::array<float, kReverbSizeB> echoB_{};
    std::uint64_t totalSamples_ = 0;
    int frameSinceDetection_ = 0;
    std::size_t reverbIndexA_ = 0;
    std::size_t reverbIndexB_ = 0;

    // D3 TD-PSOLA shifter state (fixed-size, allocation-free; ~200 bytes).
    // Analysis marks ride a grid phase-locked to the applied F0; synthesis
    // marks are laid at the output period P/r; each output grain is a
    // 2-period Hann window read verbatim from the newest analysis mark at or
    // before its synthesis center. Marks repeat (r > 1) or skip (r < 1) —
    // the mark-index mapping is what changes pitch without resampling the
    // grains, so the spectral envelope (formants) does not move.
    struct Grain {
        double outCenter;  // synthesis mark, absolute output index
        double inCenter;   // analysis mark, absolute input index
        float halfLen;     // applied period in samples at lay time (span 2x)
    };
    std::array<Grain, kPsolaMaxGrains> grains_{};
    int grainCount_ = 0;
    double markOut_ = 0.0;       // next synthesis mark, absolute output index
    double gridNext_ = 0.0;      // newest analysis mark, absolute input index
    float gridPeriod_ = 0.0f;    // analysis grid spacing, tracks applied F0
    float lastPeriod_ = 0.0f;    // last applied period (UV holdover/alignment)
    float lastAppliedHz_ = 0.0f; // applied-F0 jump detector for grid reseeds
    float uvGain_ = 0.0f;        // 0 = aligned passthrough .. 1 = PSOLA
    float uvSlew_ = 0.0f;        // per-sample crossfade step (1/xfade samples)
    float driveHz_ = 0.0f;       // test pin: applied F0 (0 = follow tracker)
    float driveRatio_ = 1.0f;    // test pin: correction ratio
    float transientEnv_ = 0.0f;  // decaying |x| peak envelope for onsets
    int transientHold_ = 0;      // samples left routing transients to passthrough
    bool grainGridValid_ = false;

    float smoothedRatio_ = 1.0f;
    float lastTargetRatio_ = 1.0f;
    float lastFrequencyHz_ = 0.0f;
    float lastTargetHz_ = 0.0f;
    int stableCount_ = 0;

    // D1 detector state. recentE_ is a ring of the last few per-update window
    // energies (running-min noise-floor proxy); armedLeft_ counts down the
    // "armed" window opened by a short-time energy rise (voiced onset).
    std::array<float, 8> recentE_{};
    int recentIdx_ = 0;
    int armedLeft_ = 0;

    // D2 tracking state (all fixed-size, allocation-free). trackHz_/trackAt_
    // hold the last <=3 accepted F0 estimates (oldest -> newest) and the update
    // index at which each was accepted; appliedHz_ is the tracker output —
    // the median of that ring — which drives the correction target.
    // pendingHz_/pendingCount_ implement the jump gate: a candidate >300 cents
    // off the running median must repeat within 150 cents twice in a row
    // before it is accepted. holdFrames_ bounds consecutive non-reportable
    // voiced holds; gapFrames_ counts unvoiced updates so a breath shorter
    // than breathHops_ (~150 ms) keeps the track alive.
    std::array<float, 3> trackHz_{};
    std::array<int, 3> trackAt_{};
    int trackCount_ = 0;
    float appliedHz_ = 0.0f;
    float pendingHz_ = 0.0f;
    int pendingCount_ = 0;
    int holdFrames_ = 0;
    int gapFrames_ = 0;
    int breathHops_ = 14;
    int updateIndex_ = 0;

    std::atomic<int> root_{0};   // C = 0, C# = 1 ... B = 11
    std::atomic<int> scale_{0};  // 0 major, 1 minor, 2 chromatic
    std::atomic<float> amount_{0.9f};
    std::atomic<float> retuneMs_{35.0f};
    std::atomic<float> reverb_{0.0f};

    std::atomic<float> detectedHz_{0.0f};
    std::atomic<float> targetHz_{0.0f};
    std::atomic<float> cents_{0.0f};
    std::atomic<bool> voiced_{false};
};

}  // namespace dautotune

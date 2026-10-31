#pragma once
#include "PingMath.h"
#include "PitchCorrector.h"
#include <oboe/Oboe.h>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace dautotune {
class AudioEngine final : public oboe::AudioStreamDataCallback {
public:
    AudioEngine();
    ~AudioEngine();
    bool start();
    void stop();
    void setParameters(int root, int scale, float amount, float retuneMs, float reverb) noexcept;
    // [detectedHz, targetHz, correctionCents, voiced, sampleRate, missingFrames,
    //  processedFrames, running, xruns, retuneMs, cbMaxMs, cbAvgMs,
    //  cbP99Ms, xrunValid, missingFramesPerSec, inputShortReadsPerSec]
    // retuneMs is the control smoothing parameter, NOT a latency measurement.
    // xrunValid: 1 when the output stream reports an xrun count, 0 when unsupported.
    std::array<double, 16> snapshot() const noexcept;
    // Granted (not requested) stream properties, read after start: [out, in] x
    // [sampleRate, bufferCapacityFrames, framesPerBurst, sharingMode, performanceMode].
    // sharingMode: 0=Shared, 1=Exclusive, 2=other. performanceMode: 0=LowLatency,
    // 1=PowerSaving, 2=other. Entries are -1 while the stream is not open.
    std::array<int32_t, 10> streamInfo() const;
    // The single process-wide engine instance (registered in the constructor);
    // lets the JNI export live in this translation unit.
    static AudioEngine* instance() noexcept { return sInstance_; }
    std::string lastError() const;
    oboe::DataCallbackResult onAudioReady(oboe::AudioStream* stream, void* data, int32_t frames) override;

    // ---- DC-003 REV2: diagnostic acoustic loopback (measurement CLASS 1) ----
    // Debug-only mode: emits a low-amplitude chirp on the OUTPUT and captures
    // input at the same callback's input_->read() point, then detects it via
    // normalized cross-correlation on a single stream pair / clock domain.
    // Result = speaker->mic round-trip diagnostic. It is NEVER a
    // mic->DSP->headphone latency claim (that is class 2, fixture-gated).
    enum class MeasState : int32_t { kIdle = 0, kArmed = 1, kPlaying = 2,
                                     kCaptureReady = 3, kComplete = 4 };
    // Arm `cycles` chirps (clamped [1,500]); chirp peak clamped <= 0.30 FS;
    // NCC validity threshold clamped [0.10,0.95]; search window `maxLagMs`
    // clamped [20,500]. Requires running streams (same pair = one time base).
    // Returns 0 ok / 1 engine-not-running / 3 bad arguments. Non-RT thread only.
    int32_t armMeasurement(int32_t cycles, float amplitudeCap,
                           float nccThreshold, int32_t maxLagMs);
    // Measurement-layer driver, called from ONE non-callback thread (JNI poll):
    // runs the NCC scan on a finished capture, enforces the per-cycle timeout,
    // records results, re-arms or completes. Returns the current MeasState.
    int32_t pollMeasurement();
    // Abort the session (state -> Idle); keeps results for post-mortem reads.
    void stopMeasurement();
    // [medianMs, p95Ms, validCycles, completedCycles, requestedCycles,
    //  sampleRateHz, minMs, maxMs]. lag->ms uses the GRANTED sample rate; all
    //  stats are over valid cycles only, NaN when none validated.
    std::array<double, 8> measurementSummary() const;
    // Per completed cycle: {lagSamples (-1 = none), nccPeak, flags} triplets.
    // flags: bit0 valid, bit1 clipped-at-window-edge, bit2 timeout, bit3
    // NCC below threshold. Invalid attempts stay visible, never hidden.
    std::vector<float> measurementCycles() const;
    // Class 2 authoritative end-to-end latency requires a calibrated hardware
    // loopback fixture sharing a reference clock. Absent in this environment:
    // MEASUREMENT_UNAVAILABLE (quiet NaN), fail-closed - class 1 is never
    // substituted for it.
    double authoritativeLatencyMs() const noexcept;

private:
    static constexpr int kChunk = 512;
    static constexpr int kCbHistBuckets = 16;  // 2 ms per bucket, covering 0..32 ms
    static constexpr uint64_t kCbHistBucketUs = 2000;
    mutable std::mutex controlMutex_;
    std::shared_ptr<oboe::AudioStream> input_;
    std::shared_ptr<oboe::AudioStream> output_;
    PitchCorrector dsp_;
    std::string lastError_;
    std::atomic<bool> running_{false};
    std::atomic<int> sampleRate_{0};
    std::atomic<int> channels_{0};
    std::atomic<uint64_t> missingFrames_{0};
    std::atomic<uint64_t> inputShortReads_{0};
    std::atomic<uint64_t> framesProcessed_{0};
    // Last xrun count reported by the output stream; written by snapshot() (UI thread).
    mutable std::atomic<uint64_t> xruns_{0};
    std::atomic<uint64_t> cbMaxUs_{0};
    std::atomic<uint64_t> cbAvgUs_{0};
    std::atomic<uint64_t> cbCount_{0};
    // Callback-duration histogram; single writer (callback thread), relaxed atomics.
    std::array<std::atomic<uint64_t>, kCbHistBuckets> cbHist_{};
    // Written only on the audio callback thread; start() clears it before any callback can run.
    uint64_t cbTotalUs_ = 0;
    std::atomic<float> lastRetuneMs_{35.0f};
    // Rate computation state, written only by snapshot() (UI thread, holding controlMutex_).
    mutable uint64_t prevMissingFrames_ = 0;
    mutable uint64_t prevShortReads_ = 0;
    mutable std::chrono::steady_clock::time_point prevRateAt_{};

    // ---- DC-003 REV2 measurement internals ----
    static constexpr int32_t kMeasStimMs = 200;
    static constexpr float kMeasF0Hz = 1000.0f;
    static constexpr float kMeasF1Hz = 4000.0f;
    static constexpr float kMeasMaxAmplitude = 0.30f;
    static constexpr int32_t kMeasMaxCycles = 500;
    static constexpr int32_t kMeasMaxLagMsCap = 500;
    static constexpr double kMeasTimeoutMarginSec = 1.0;
    static constexpr int32_t kMeasFlagValid = 1;
    static constexpr int32_t kMeasFlagClipped = 2;
    static constexpr int32_t kMeasFlagTimeout = 4;
    static constexpr int32_t kMeasFlagLowNcc = 8;
    // Control thread: force Idle, then wait out any callback inside the
    // measurement path before mutating measurement buffers.
    void quiesceMeasurement();
    // Callback thread helpers; fixed work only - no alloc, no locks, no I/O.
    bool measEnter();
    void measChunk(const float* capture, float* out, int32_t n, int32_t channels);
    void measExit();
    // Poll thread helpers; measMutex_ is held by the callers.
    void recordCycle(int32_t lag, float ncc, int32_t flags);
    void finalizeMeasurement();

    std::atomic<int32_t> measState_{0};
    // Handshake flag so quiesceMeasurement() knows the callback is inside the
    // measurement section (see measEnter for the seq_cst ordering argument).
    std::atomic<bool> measInside_{false};
    // Owned by the control thread at arm time; read-only for the callback
    // while a session is active (single state machine hands ownership over).
    std::vector<float> measStim_;
    std::vector<float> measCap_;
    int32_t measStimLen_ = 0;
    int32_t measCapLen_ = 0;
    float measNccThreshold_ = 0.6f;
    double measCycleSec_ = 0.0;
    // Single writer = callback thread.
    int32_t measStimPos_ = 0;
    int32_t measCapPos_ = 0;
    // Results/config live behind this mutex; the audio callback never takes it.
    mutable std::mutex measMutex_;
    std::vector<std::array<float, 3>> measResults_;  // {lag, ncc, flags}
    int32_t measCyclesTotal_ = 0;
    int32_t measCyclesDone_ = 0;
    int32_t measValidCount_ = 0;
    double measMedianMs_ = 0.0, measP95Ms_ = 0.0, measMinMs_ = 0.0, measMaxMs_ = 0.0;
    std::chrono::steady_clock::time_point measDeadline_{};
    static AudioEngine* sInstance_;
};
}

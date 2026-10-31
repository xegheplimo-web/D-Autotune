#pragma once
#include "PitchCorrector.h"
#include <oboe/Oboe.h>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

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
    std::atomic<uint64_t> xruns_{0};
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
    static AudioEngine* sInstance_;
};
}

#pragma once
#include "PitchCorrector.h"
#include <oboe/Oboe.h>
#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>

namespace dautotune {
class AudioEngine final : public oboe::AudioStreamDataCallback {
public:
    ~AudioEngine();
    bool start();
    void stop();
    void setParameters(int root, int scale, float amount, float retuneMs, float reverb) noexcept;
    std::array<double, 12> snapshot() const noexcept;
    std::string lastError() const;
    oboe::DataCallbackResult onAudioReady(oboe::AudioStream* stream, void* data, int32_t frames) override;

private:
    static constexpr int kChunk = 512;
    void updateAlgoLatency() noexcept;
    mutable std::mutex controlMutex_;
    std::shared_ptr<oboe::AudioStream> input_;
    std::shared_ptr<oboe::AudioStream> output_;
    PitchCorrector dsp_;
    std::string lastError_;
    std::atomic<bool> running_{false};
    std::atomic<int> sampleRate_{0};
    std::atomic<int> channels_{0};
    std::atomic<uint64_t> missingFrames_{0};
    std::atomic<uint64_t> framesProcessed_{0};
    std::atomic<uint64_t> xruns_{0};
    std::atomic<uint64_t> cbMaxUs_{0};
    std::atomic<uint64_t> cbAvgUs_{0};
    std::atomic<uint64_t> cbCount_{0};
    // Written only on the audio callback thread; start() clears it before any callback can run.
    uint64_t cbTotalUs_ = 0;
    std::atomic<float> lastRetuneMs_{35.0f};
    std::atomic<double> algoLatencyMs_{0.0};
};
}

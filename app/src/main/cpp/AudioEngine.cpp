#include "AudioEngine.h"
#include <algorithm>
#include <chrono>
#include <cstring>

namespace dautotune {
AudioEngine::~AudioEngine() { stop(); }

bool AudioEngine::start() {
    std::lock_guard<std::mutex> lock(controlMutex_);
    if (running_.load()) return true;
    lastError_.clear();
    missingFrames_.store(0);
    framesProcessed_.store(0);
    xruns_.store(0);
    cbMaxUs_.store(0);
    cbAvgUs_.store(0);
    cbCount_.store(0);
    cbTotalUs_ = 0;

    oboe::AudioStreamBuilder outputBuilder;
    outputBuilder.setDirection(oboe::Direction::Output);
    outputBuilder.setPerformanceMode(oboe::PerformanceMode::LowLatency);
    outputBuilder.setSharingMode(oboe::SharingMode::Shared);
    outputBuilder.setFormat(oboe::AudioFormat::Float);
    outputBuilder.setChannelCount(2);
    outputBuilder.setUsage(oboe::Usage::Media);
    outputBuilder.setContentType(oboe::ContentType::Music);
    outputBuilder.setDataCallback(this);
    auto result = outputBuilder.openStream(output_);
    if (result != oboe::Result::OK) {
        lastError_ = std::string("Output: ") + oboe::convertToText(result);
        return false;
    }

    oboe::AudioStreamBuilder inputBuilder;
    inputBuilder.setDirection(oboe::Direction::Input);
    inputBuilder.setPerformanceMode(oboe::PerformanceMode::LowLatency);
    inputBuilder.setSharingMode(oboe::SharingMode::Shared);
    inputBuilder.setFormat(oboe::AudioFormat::Float);
    inputBuilder.setChannelCount(1);
    inputBuilder.setSampleRate(output_->getSampleRate());
    inputBuilder.setInputPreset(oboe::InputPreset::VoiceRecognition);
    result = inputBuilder.openStream(input_);
    if (result != oboe::Result::OK) {
        lastError_ = std::string("Input: ") + oboe::convertToText(result);
        output_->close(); output_.reset();
        return false;
    }

    if (input_->getFormat() != oboe::AudioFormat::Float || output_->getFormat() != oboe::AudioFormat::Float ||
        input_->getChannelCount() != 1 || input_->getSampleRate() != output_->getSampleRate()) {
        lastError_ = "Unsupported audio format/channel/sample-rate configuration";
        input_->close(); input_.reset(); output_->close(); output_.reset();
        return false;
    }
    dsp_.reset(output_->getSampleRate());
    sampleRate_.store(output_->getSampleRate());
    channels_.store(output_->getChannelCount());
    updateAlgoLatency();
    result = input_->requestStart();
    if (result == oboe::Result::OK) {
        running_.store(true, std::memory_order_release);
        result = output_->requestStart();
    }
    if (result != oboe::Result::OK) {
        lastError_ = std::string("Stream start: ") + oboe::convertToText(result);
        running_.store(false);
        input_->requestStop(); input_->close(); input_.reset();
        output_->requestStop(); output_->close(); output_.reset();
        return false;
    }
    return true;
}

void AudioEngine::stop() {
    std::lock_guard<std::mutex> lock(controlMutex_);
    running_.store(false, std::memory_order_release);
    // Stop the output callback before closing the input it reads from.
    if (output_) { output_->requestStop(); output_->close(); output_.reset(); }
    if (input_) { input_->requestStop(); input_->close(); input_.reset(); }
}

void AudioEngine::setParameters(int root, int scale, float amount, float retuneMs, float reverb) noexcept {
    lastRetuneMs_.store(retuneMs, std::memory_order_relaxed);
    updateAlgoLatency();
    dsp_.setParameters(root, scale, amount, retuneMs, reverb);
}

void AudioEngine::updateAlgoLatency() noexcept {
    const int rate = sampleRate_.load(std::memory_order_relaxed);
    if (rate > 0)
        algoLatencyMs_.store(static_cast<double>(kChunk) * 1000.0 / rate +
                             lastRetuneMs_.load(std::memory_order_relaxed),
                             std::memory_order_relaxed);
}

std::array<double, 12> AudioEngine::snapshot() const noexcept {
    return {static_cast<double>(dsp_.detectedHz()), static_cast<double>(dsp_.targetHz()),
            static_cast<double>(dsp_.correctionCents()), dsp_.voiced() ? 1.0 : 0.0,
            static_cast<double>(sampleRate_.load()), static_cast<double>(missingFrames_.load()),
            static_cast<double>(framesProcessed_.load()), running_.load() ? 1.0 : 0.0,
            static_cast<double>(xruns_.load(std::memory_order_relaxed)),
            algoLatencyMs_.load(std::memory_order_relaxed),
            static_cast<double>(cbMaxUs_.load(std::memory_order_relaxed)) / 1000.0,
            static_cast<double>(cbAvgUs_.load(std::memory_order_relaxed)) / 1000.0};
}
std::string AudioEngine::lastError() const {
    std::lock_guard<std::mutex> lock(controlMutex_);
    return lastError_;
}

oboe::DataCallbackResult AudioEngine::onAudioReady(oboe::AudioStream* stream, void* data, int32_t frames) {
    const auto cbStart = std::chrono::steady_clock::now();
    if (!running_.load(std::memory_order_acquire) || !input_ || !data)
        return oboe::DataCallbackResult::Stop;
    auto* out = static_cast<float*>(data);
    const int channels = stream->getChannelCount();
    int offset = 0;
    while (offset < frames) {
        const int request = std::min(kChunk, static_cast<int>(frames) - offset);
        float capture[kChunk] = {};
        const auto read = input_->read(capture, request, 0);
        const int received = read ? std::max(0, std::min(request, read.value())) : 0;
        if (received < request) missingFrames_.fetch_add(request - received, std::memory_order_relaxed);
        dsp_.process(capture, out + offset * channels, request, channels);
        offset += request;
    }
    framesProcessed_.fetch_add(frames, std::memory_order_relaxed);
    const auto xr = stream->getXRunCount();
    if (xr) xruns_.store(static_cast<uint64_t>(xr.value()), std::memory_order_relaxed);
    const auto us = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - cbStart).count());
    cbTotalUs_ += us;
    cbAvgUs_.store(cbTotalUs_ / (cbCount_.fetch_add(1, std::memory_order_relaxed) + 1),
                   std::memory_order_relaxed);
    if (us > cbMaxUs_.load(std::memory_order_relaxed))
        cbMaxUs_.store(us, std::memory_order_relaxed);
    return oboe::DataCallbackResult::Continue;
}
}  // namespace dautotune

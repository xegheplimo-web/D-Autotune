#include "AudioEngine.h"
#include <jni.h>
#include <algorithm>
#include <chrono>
#include <cstring>

namespace dautotune {
AudioEngine* AudioEngine::sInstance_ = nullptr;

AudioEngine::AudioEngine() { sInstance_ = this; }
AudioEngine::~AudioEngine() {
    if (sInstance_ == this) sInstance_ = nullptr;
    stop();
}

bool AudioEngine::start() {
    std::lock_guard<std::mutex> lock(controlMutex_);
    if (running_.load()) return true;
    lastError_.clear();
    missingFrames_.store(0);
    inputShortReads_.store(0);
    framesProcessed_.store(0);
    xruns_.store(0);
    cbMaxUs_.store(0);
    cbAvgUs_.store(0);
    cbCount_.store(0);
    cbTotalUs_ = 0;
    for (auto& bucket : cbHist_) bucket.store(0, std::memory_order_relaxed);
    prevMissingFrames_ = 0;
    prevShortReads_ = 0;
    prevRateAt_ = std::chrono::steady_clock::now();

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
    dsp_.setParameters(root, scale, amount, retuneMs, reverb);
}

namespace {
int32_t encodeSharingMode(oboe::SharingMode mode) {
    if (mode == oboe::SharingMode::Shared) return 0;
    if (mode == oboe::SharingMode::Exclusive) return 1;
    return 2;
}
int32_t encodePerformanceMode(oboe::PerformanceMode mode) {
    if (mode == oboe::PerformanceMode::LowLatency) return 0;
    if (mode == oboe::PerformanceMode::PowerSaving) return 1;
    return 2;
}
}  // namespace

std::array<int32_t, 10> AudioEngine::streamInfo() const {
    std::array<int32_t, 10> info;
    info.fill(-1);
    std::lock_guard<std::mutex> lock(controlMutex_);
    oboe::AudioStream* streams[2] = {output_.get(), input_.get()};
    for (int i = 0; i < 2; ++i) {
        if (!streams[i]) continue;
        const size_t base = static_cast<size_t>(i) * 5;
        info[base + 0] = streams[i]->getSampleRate();
        info[base + 1] = streams[i]->getBufferCapacityInFrames();
        info[base + 2] = streams[i]->getFramesPerBurst();
        info[base + 3] = encodeSharingMode(streams[i]->getSharingMode());
        info[base + 4] = encodePerformanceMode(streams[i]->getPerformanceMode());
    }
    return info;
}

std::array<double, 16> AudioEngine::snapshot() const noexcept {
    // Serializes with start()/stop() so the xrun query cannot race stream teardown.
    // The audio callback never takes this mutex.
    std::lock_guard<std::mutex> lock(controlMutex_);

    // getXRunCount() may take internal stream locks, so it is queried here on the
    // UI thread and never inside the real-time callback. A failed query means the
    // stream cannot report xruns (distinct from "0 xruns").
    double xruns = static_cast<double>(xruns_.load(std::memory_order_relaxed));
    double xrunValid = 0.0;
    if (output_) {
        const auto xr = output_->getXRunCount();
        if (xr && xr.value() >= 0) {
            xruns = static_cast<double>(xr.value());
            xruns_.store(static_cast<uint64_t>(xr.value()), std::memory_order_relaxed);
            xrunValid = 1.0;
        }
    }

    // p99 from the callback-duration histogram. Reported value is the upper edge
    // of the first bucket reaching 99% (conservative: "99% of callbacks under").
    uint64_t hist[kCbHistBuckets] = {};
    uint64_t histTotal = 0;
    for (int i = 0; i < kCbHistBuckets; ++i) {
        hist[i] = cbHist_[static_cast<size_t>(i)].load(std::memory_order_relaxed);
        histTotal += hist[i];
    }
    double cbP99Ms = 0.0;
    if (histTotal > 0) {
        const uint64_t need = (histTotal * 99 + 99) / 100;
        uint64_t acc = 0;
        for (int i = 0; i < kCbHistBuckets; ++i) {
            acc += hist[i];
            if (acc >= need) {
                cbP99Ms = static_cast<double>(static_cast<uint64_t>(i + 1) * kCbHistBucketUs) / 1000.0;
                break;
            }
        }
    }

    // Delta-based rates over the interval between snapshot() calls.
    const auto now = std::chrono::steady_clock::now();
    const double dt = std::chrono::duration<double>(now - prevRateAt_).count();
    const uint64_t missing = missingFrames_.load(std::memory_order_relaxed);
    const uint64_t shortReads = inputShortReads_.load(std::memory_order_relaxed);
    double missingRate = 0.0, shortReadRate = 0.0;
    if (dt > 0.0) {
        missingRate = static_cast<double>(missing - prevMissingFrames_) / dt;
        shortReadRate = static_cast<double>(shortReads - prevShortReads_) / dt;
    }
    prevMissingFrames_ = missing;
    prevShortReads_ = shortReads;
    prevRateAt_ = now;

    return {static_cast<double>(dsp_.detectedHz()), static_cast<double>(dsp_.targetHz()),
            static_cast<double>(dsp_.correctionCents()), dsp_.voiced() ? 1.0 : 0.0,
            static_cast<double>(sampleRate_.load()), static_cast<double>(missing),
            static_cast<double>(framesProcessed_.load()), running_.load() ? 1.0 : 0.0,
            xruns,
            static_cast<double>(lastRetuneMs_.load(std::memory_order_relaxed)),
            static_cast<double>(cbMaxUs_.load(std::memory_order_relaxed)) / 1000.0,
            static_cast<double>(cbAvgUs_.load(std::memory_order_relaxed)) / 1000.0,
            cbP99Ms, xrunValid, missingRate, shortReadRate};
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
        if (received < request) {
            missingFrames_.fetch_add(request - received, std::memory_order_relaxed);
            inputShortReads_.fetch_add(1, std::memory_order_relaxed);
        }
        dsp_.process(capture, out + offset * channels, request, channels);
        offset += request;
    }
    framesProcessed_.fetch_add(frames, std::memory_order_relaxed);
    const auto us = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - cbStart).count());
    cbTotalUs_ += us;
    cbAvgUs_.store(cbTotalUs_ / (cbCount_.fetch_add(1, std::memory_order_relaxed) + 1),
                   std::memory_order_relaxed);
    if (us > cbMaxUs_.load(std::memory_order_relaxed))
        cbMaxUs_.store(us, std::memory_order_relaxed);
    cbHist_[static_cast<size_t>(std::min<uint64_t>(us / kCbHistBucketUs, kCbHistBuckets - 1))]
        .fetch_add(1, std::memory_order_relaxed);
    return oboe::DataCallbackResult::Continue;
}
}  // namespace dautotune

// JNI export lives here because native-lib.cpp is outside this card's scope and
// its engine instance has internal linkage; AudioEngine::instance() reaches it.
extern "C" JNIEXPORT jintArray JNICALL
Java_com_dautotune_app_NativeAudio_getStreamInfo(JNIEnv* env, jobject) {
    std::array<int32_t, 10> info;
    info.fill(-1);
    if (auto* engine = dautotune::AudioEngine::instance()) info = engine->streamInfo();
    jint values[10];
    for (size_t i = 0; i < info.size(); ++i) values[i] = static_cast<jint>(info[i]);
    jintArray result = env->NewIntArray(static_cast<jsize>(info.size()));
    if (result) env->SetIntArrayRegion(result, 0, static_cast<jsize>(info.size()), values);
    return result;
}

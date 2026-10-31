#pragma once
// DC-003 REV2: pure stimulus/correlation math for the diagnostic acoustic
// loopback measurement. No Oboe/JNI dependencies so the host ctest target can
// exercise the exact code that runs on-device. All functions are allocation-
// and lock-free: safe to call from the measurement layer (never needed inside
// the real-time callback, which only taps buffers).
#include <cstdint>

namespace dautotune::ping {

struct ScanResult {
    int32_t lag = -1;      // best lag in samples; -1 when no usable candidate
    float ncc = 0.0f;      // normalized cross-correlation at `lag`, [-1..1]
    bool clipped = false;  // best lag sits on a search boundary -> the real
                           // delay may lie outside the scanned window
};

// Linear chirp sweeping f0Hz -> f1Hz over `frames` samples, raised-cosine edge
// tapers (no click), peak amplitude `amplitude` (callers cap it <= 0.3 FS).
// Deterministic; writes exactly `frames` samples into `dst`.
void generateChirp(float* dst, int32_t frames, int32_t sampleRate,
                   float f0Hz, float f1Hz, float amplitude) noexcept;

// Normalized cross-correlation (mean-subtracted) scan of `cap[0..capLen)` for
// `ref[0..refLen)` over all lags [0, capLen-refLen]. Returns the argmax and its
// NCC. Amplitude-invariant: speaker->mic attenuation does not shift the peak;
// only noise/decorrelation lowers it. O(refLen * lags); fixed-size loops only.
ScanResult findLag(const float* ref, int32_t refLen,
                   const float* cap, int32_t capLen) noexcept;

// Nearest-rank index into a SORTED array of `count` values for `percentile`
// (0..100]. -1 when count <= 0. Used for median/p95 over valid cycle lags.
int32_t nearestRankIndex(int32_t count, double percentile) noexcept;

}  // namespace dautotune::ping

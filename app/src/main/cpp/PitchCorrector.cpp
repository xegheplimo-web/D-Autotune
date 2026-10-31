#include "PitchCorrector.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace dautotune {
namespace {
constexpr float kPi = 3.14159265358979323846f;
constexpr float kA4 = 440.0f;

int mod12(int n) noexcept { return (n % 12 + 12) % 12; }
float clampf(float x, float lo, float hi) noexcept { return std::max(lo, std::min(hi, x)); }

// D1: one YIN-style cumulative-mean-normalized-difference scan (written
// clean-room from the algorithm description). `x` points at a contiguous
// region of decimated samples; the region must extend at least
// compare + maxLag elements. Returns the lag of the FIRST local CMND trough
// entered after cmnd < tEnter (lag >= minLag), or 0 if none. Fills
// norm[1..maxLag] with the CMND values (norm[0] = 1).
int yinTrough(const float* x, int compare, int minLag, int maxLag,
              float tEnter, float* norm) noexcept {
    float running = 0.0f;
    float previous = 1.0f;
    bool entered = false;
    norm[0] = 1.0f;
    for (int lag = 1; lag <= maxLag; ++lag) {
        float difference = 0.0f;
        for (int i = 0; i < compare; ++i) {
            const float d = x[i] - x[i + lag];
            difference += d * d;
        }
        running += difference;
        const float cmnd = running > 1e-9f ? difference * static_cast<float>(lag) / running
                                           : 1.0f;
        norm[lag] = cmnd;
        if (!entered && lag >= minLag && cmnd < tEnter) entered = true;
        // First local trough, not the global minimum (octave protection).
        if (entered && lag > minLag && cmnd > previous) return lag - 1;
        previous = cmnd;
    }
    return 0;
}

// Parabolic interpolation of the lag around a CMND trough.
float refineLag(const float* norm, int lag, int minLag, int maxLag) noexcept {
    float refined = static_cast<float>(lag);
    if (lag > minLag && lag < maxLag) {
        const float left = norm[lag - 1];
        const float middle = norm[lag];
        const float right = norm[lag + 1];
        const float denominator = left - 2.0f * middle + right;
        if (std::abs(denominator) > 1e-6f) {
            refined += clampf(0.5f * (left - right) / denominator, -0.5f, 0.5f);
        }
    }
    return refined;
}
}  // namespace

PitchCorrector::PitchCorrector(int sampleRate) { reset(sampleRate); }

void PitchCorrector::reset(int sampleRate) noexcept {
    sampleRate_ = std::max(8000, sampleRate);
    history_.fill(0.0f);
    echoA_.fill(0.0f);
    echoB_.fill(0.0f);
    totalSamples_ = 0;
    frameSinceDetection_ = 0;
    reverbIndexA_ = 0;
    reverbIndexB_ = 0;
    grainCount_ = 0;
    markOut_ = 0.0;
    gridNext_ = 0.0;
    gridPeriod_ = 0.0f;
    lastPeriod_ = static_cast<float>(sampleRate_) / 220.0f;
    lastAppliedHz_ = 0.0f;
    uvGain_ = 0.0f;
    uvSlew_ = 1.0f / std::max(1.0f, kUvXfadeSec * static_cast<float>(sampleRate_));
    driveHz_ = 0.0f;
    driveRatio_ = 1.0f;
    transientEnv_ = 0.0f;
    transientHold_ = 0;
    grainGridValid_ = false;
    smoothedRatio_ = 1.0f;
    lastTargetRatio_ = 1.0f;
    lastFrequencyHz_ = 0.0f;
    lastTargetHz_ = 0.0f;
    stableCount_ = 0;
    recentE_.fill(1e30f);
    recentIdx_ = 0;
    armedLeft_ = 0;
    trackHz_.fill(0.0f);
    trackAt_.fill(0);
    trackCount_ = 0;
    appliedHz_ = 0.0f;
    pendingHz_ = 0.0f;
    pendingCount_ = 0;
    holdFrames_ = 0;
    gapFrames_ = 0;
    breathHops_ = std::max(1, static_cast<int>(0.150f * static_cast<float>(sampleRate_) /
                                             static_cast<float>(kDetectHop)));
    updateIndex_ = 0;
    detectedHz_.store(0.0f);
    targetHz_.store(0.0f);
    cents_.store(0.0f);
    voiced_.store(false);
}

void PitchCorrector::setParameters(int root, int scale, float amount, float retuneMs, float reverb) noexcept {
    root_.store(mod12(root), std::memory_order_relaxed);
    scale_.store(std::max(0, std::min(scale, 2)), std::memory_order_relaxed);
    amount_.store(clampf(amount, 0.0f, 1.0f), std::memory_order_relaxed);
    retuneMs_.store(clampf(retuneMs, 5.0f, 150.0f), std::memory_order_relaxed);
    reverb_.store(clampf(reverb, 0.0f, 0.25f), std::memory_order_relaxed);
}

float PitchCorrector::nearestScaleFrequency(float hz, int root, int scale) noexcept {
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

PitchCorrector::DetOut PitchCorrector::detect() noexcept {
    constexpr int kCompare = 256;
    // Pass A (fresh window): only the newest kShortLen decimated samples are
    // analysed, so the estimate's effective center sits much closer to "now"
    // than the full-window pass (tracking-lag reduction). Thresholds are
    // deeper than the long window's and the result is treated as provisional:
    // updateTarget confirms it before reporting.
    constexpr int kShortCompare = 128;
    constexpr int kShortLen = 288;                      // 288*4 = 1152 native ~24ms @48k
    constexpr int kShortBase = kDetectWindow - kShortLen; // tail of the buffer
    // Relaxed fallback thresholds (pass C): reached only when the strict scan
    // fails — i.e. when periodicity is buried. Reporting is confirm-gated.
    constexpr float kTenterRelax = 0.35f;
    constexpr float kTacceptRelax = 0.55f;

    DetOut out;
    std::array<float, kDetectWindow> samples{};
    float squared = 0.0f;
    const std::uint64_t first = totalSamples_ - kDetectWindow * kDownsample;
    for (int i = 0; i < kDetectWindow; ++i) {
        // Lightweight decimator; pitch detector is not the sound path.
        const float s = history_[(first + i * kDownsample) & (kHistorySize - 1)];
        samples[i] = s;
        squared += s * s;
    }
    const float energy = squared / static_cast<float>(kDetectWindow);

    // Energy-rise "armed" tracker: recentE_ holds the per-update window
    // energies of the last ~85ms; its minimum is a running noise-floor proxy.
    // A >= 3x jump over that floor opens a ~64ms window in which a confident
    // estimate may voice immediately (faster voiced onsets). Updated on every
    // detection update, including silent frames, so a silence->voice edge arms.
    float recentMin = recentE_[0];
    for (const float v : recentE_) recentMin = std::min(recentMin, v);
    if (armedLeft_ > 0) --armedLeft_;
    if (recentMin < 1e29f && energy > 3.0f * recentMin) armedLeft_ = 6;
    recentE_[recentIdx_] = energy;
    recentIdx_ = (recentIdx_ + 1) & 7;

    if (energy < 0.000040f) return out;

    const float reducedRate = static_cast<float>(sampleRate_) / kDownsample;
    const int minLag = std::max(2, static_cast<int>(reducedRate / 1000.0f));
    const int maxLag = std::min(200, static_cast<int>(reducedRate / 80.0f));
    std::array<float, 201> norm{};

    // Effective staleness of an estimate produced from buffer offset `base`
    // with `compare` terms at the winning `lag`: the used span ends
    // (kDetectWindow - (base+compare+lag)) decimated samples before "now", so
    // its center sits ~half of (compare+lag) behind the newest sample.
    // updateTarget extrapolates the raw estimate forward by this amount.
    const float invSr = 1.0f / static_cast<float>(sampleRate_);
    const auto leadFor = [&](int base, int cmp, int lag) {
        const float used = static_cast<float>(kDetectWindow - base);
        return (used - 0.5f * static_cast<float>(cmp + lag)) *
               static_cast<float>(kDownsample) * invSr;
    };

    int bestLag;
    int base;
    int compare;
    // --- Pass A: fresh short window, strict-deep trough ----------------------
    const int maxLagA = std::min(maxLag, kShortLen - kShortCompare);
    bestLag = yinTrough(samples.data() + kShortBase, kShortCompare, minLag, maxLagA,
                        0.12f, norm.data());
    if (bestLag >= minLag && norm[bestLag] <= 0.20f) {
        out.provisional = true;
        base = kShortBase;
        compare = kShortCompare;
    } else {
        // --- Pass B: long window, original strict thresholds (D0 parity) ----
        bestLag = yinTrough(samples.data(), kCompare, minLag, maxLag, 0.16f,
                            norm.data());
        if (bestLag >= minLag && norm[bestLag] <= 0.27f) {
            base = 0;
            compare = kCompare;
        } else {
            // --- Pass C: same scan, relaxed thresholds; confirm-gated --------
            bestLag = yinTrough(samples.data(), kCompare, minLag, maxLag,
                                kTenterRelax, norm.data());
            if (bestLag < minLag || norm[bestLag] > kTacceptRelax) return out;
            out.relaxed = true;
            base = 0;
            compare = kCompare;
        }
    }

    float hz = reducedRate / refineLag(norm.data(), bestLag, minLag, maxLag);

    // Native-rate refinement for high-F0 candidates: below lag 34 (decimated)
    // the 4x grid costs ~0.7% F0 per step. Re-scan +/-4 native lags around
    // the winner on the freshest samples with an energy-normalized difference;
    // adopt only if a clean dip exists near the decimated answer.
    if (!out.relaxed && bestLag <= 34) {
        constexpr int kRefineCompare = 256;
        constexpr int kRefineSpan = 400;  // newest native samples, covers lag+compare
        const std::uint64_t n0 = totalSamples_ - kRefineSpan;
        float ea = 0.0f;
        for (int i = 0; i < kRefineCompare; ++i) {
            const float a = history_[(n0 + i) & (kHistorySize - 1)];
            ea += a * a;
        }
        const int center = 4 * bestLag;
        std::array<float, 9> depth{};
        int bestK = -1;
        float bestDepth = 1.0f;
        for (int k = 0; k < 9; ++k) {
            const int lag = center - 4 + k;
            float diff = 0.0f;
            float eb = 0.0f;
            for (int i = 0; i < kRefineCompare; ++i) {
                const float a = history_[(n0 + i) & (kHistorySize - 1)];
                const float b = history_[(n0 + i + lag) & (kHistorySize - 1)];
                const float d = a - b;
                diff += d * d;
                eb += b * b;
            }
            depth[k] = (ea + eb) > 1e-9f ? diff / (ea + eb) : 1.0f;
            if (depth[k] < bestDepth) {
                bestDepth = depth[k];
                bestK = k;
            }
        }
        if (bestK >= 0 && bestDepth < 0.25f) {
            float refined = static_cast<float>(center - 4 + bestK);
            if (bestK > 0 && bestK < 8) {
                const float l = depth[bestK - 1];
                const float m = depth[bestK];
                const float r = depth[bestK + 1];
                const float den = l - 2.0f * m + r;
                if (std::abs(den) > 1e-6f) {
                    refined += clampf(0.5f * (l - r) / den, -0.5f, 0.5f);
                }
            }
            const float hzNative = static_cast<float>(sampleRate_) / refined;
            if (hz <= 0.0f ||
                std::abs(12.0f * std::log2(hzNative / hz)) < 1.5f) {
                hz = hzNative;
            }
        }
    }

    if (hz < 80.0f || hz > 1000.0f || !std::isfinite(hz)) return out;
    out.hz = hz;
    out.conf = clampf(1.0f - norm[bestLag], 0.0f, 1.0f);
    out.leadSec = leadFor(base, compare, bestLag);
    return out;
}

float PitchCorrector::histAt(double absoluteIndex) const noexcept {
    // Absolute-index read with linear interpolation. Doubles keep
    // single-sample precision for the life of the stream (the 16384-deep
    // power-of-two ring makes unwritten reads land on zeroed history).
    const double base = std::floor(absoluteIndex);
    const std::int64_t i = static_cast<std::int64_t>(base);
    const float a = history_[static_cast<std::uint64_t>(i) & (kHistorySize - 1)];
    const float b = history_[static_cast<std::uint64_t>(i + 1) & (kHistorySize - 1)];
    return a + static_cast<float>(absoluteIndex - base) * (b - a);
}

float PitchCorrector::delayed(float lagSamples) const noexcept {
    return histAt(static_cast<double>(totalSamples_ - 1) - lagSamples);
}

void PitchCorrector::psolaResync(float period, std::uint64_t now) noexcept {
    // No true glottal marks are detected: the analysis grid only needs a
    // consistent phase within the note. The strongest positive excursion in
    // the last ~1.2 periods is that anchor (an epoch proxy); a bounded O(P)
    // scan that runs only on note changes / grid invalidation, not per frame.
    const std::uint64_t span = static_cast<std::uint64_t>(1.2f * period) + 1;
    const std::uint64_t from = now > span ? now - span : 0;
    std::uint64_t best = from;
    float peak = std::numeric_limits<float>::lowest();
    for (std::uint64_t i = from; i <= now; ++i) {
        const float v = history_[i & (kHistorySize - 1)];
        if (v > peak) {
            peak = v;
            best = i;
        }
    }
    gridNext_ = static_cast<double>(best);
    gridPeriod_ = period;
    markOut_ = static_cast<double>(now) + period;
    grainGridValid_ = true;
}

float PitchCorrector::psolaShift(float ratio) noexcept {
    const std::uint64_t now = totalSamples_ - 1;
    const bool driven = driveHz_ > 0.0f;
    const float f0 = driven ? driveHz_
                            : (voiced_.load(std::memory_order_relaxed) ? appliedHz_ : 0.0f);
    const float r = driven ? driveRatio_ : ratio;
    const bool active = f0 > 0.0f && std::isfinite(f0) && std::isfinite(r) && r > 0.0f;

    if (active) {
        const float period = clampf(static_cast<float>(sampleRate_) / f0,
                                    static_cast<float>(sampleRate_) / kPsolaMaxF0,
                                    static_cast<float>(sampleRate_) / kPsolaMinF0);
        lastPeriod_ = period;
        // Applied-F0 movement above ~60 cents — note changes, tracker
        // reseeds, fast portamento — re-anchors the grain grid so the first
        // grain of the new note starts at a period boundary. Slow drift
        // (vibrato) only retunes the grid spacing, keeping the marks
        // phase-locked to the input waveform.
        const float move = (lastAppliedHz_ > 0.0f && grainGridValid_)
                               ? std::abs(1200.0f * std::log2(f0 / lastAppliedHz_))
                               : std::numeric_limits<float>::max();
        if (move > kReseedCents) psolaResync(period, now);
        lastAppliedHz_ = f0;
        gridPeriod_ = period;
    } else {
        lastAppliedHz_ = 0.0f;
        grainGridValid_ = false;
    }

    // Lay grains whose window start has reached the output position. Each
    // synthesis mark at s takes its content from the newest analysis mark at
    // or before s - 1, so the read lag stays inside (0, P + 1] samples —
    // the entire algorithmic delay of the shifter.
    if (grainGridValid_ && gridPeriod_ > 0.0f) {
        const double pOut = static_cast<double>(lastPeriod_) / r;
        const double nowD = static_cast<double>(now);
        while (markOut_ - static_cast<double>(lastPeriod_) <= nowD) {
            while (gridNext_ + static_cast<double>(gridPeriod_) <= markOut_ - 1.0)
                gridNext_ += gridPeriod_;
            const Grain g{markOut_, gridNext_, lastPeriod_};
            if (grainCount_ < kPsolaMaxGrains) {
                grains_[grainCount_++] = g;
            } else {
                int oldest = 0;
                for (int i = 1; i < grainCount_; ++i)
                    if (grains_[i].outCenter < grains_[oldest].outCenter) oldest = i;
                grains_[oldest] = g;
            }
            markOut_ += pOut;
        }
    }

    float sum = 0.0f;
    float wsum = 0.0f;
    int alive = 0;
    for (int i = 0; i < grainCount_; ++i) {
        const double off = static_cast<double>(now) - grains_[i].outCenter;
        const float h = grains_[i].halfLen;
        if (off <= -static_cast<double>(h) || off >= static_cast<double>(h)) continue;
        grains_[alive++] = grains_[i];
        const float w = 0.5f + 0.5f * std::cos(kPi * static_cast<float>(off) / h);
        sum += w * histAt(grains_[i].inCenter + off);
        wsum += w;
    }
    grainCount_ = alive;

    // Transient guard: a sample jumping past 4x the decaying peak envelope is
    // an onset. Marks would re-read it for r > 1 (one echo per repeated
    // analysis mark), so the output routes through the delay-aligned
    // passthrough for 2.5 periods — one copy, timing preserved. The switch is
    // instant: a fast slew would still let the first grain emissions leak at
    // partial amplitude, and the arriving transient masks the route change.
    const float ax = std::fabs(history_[now & (kHistorySize - 1)]);
    if (transientHold_ > 0) {
        --transientHold_;
    } else if (ax > kTransFloor && ax > kTransRatio * transientEnv_) {
        transientHold_ = static_cast<int>(kTransHold * lastPeriod_);
        uvGain_ = 0.0f;
    }
    transientEnv_ = std::max(ax, transientEnv_ * kTransDecay);

    // Soft-normalized overlap-add: dividing by (wsum + eps) fades grain tails
    // in/out smoothly at coverage edges — a hard normalize by wsum would
    // amplify a just-born grain's tail to full amplitude in one sample.
    const float psola = sum / (wsum + 0.02f);

    // Unvoiced/edge path: passthrough aligned by half a period so the V/UV
    // crossfade is coherent with the PSOLA path's ~P/2 average lag.
    const float pass = delayed(0.5f * lastPeriod_);
    const float target = (active && transientHold_ <= 0) ? 1.0f : 0.0f;
    if (transientHold_ <= 0)
        uvGain_ += uvSlew_ * (target > uvGain_ ? 1.0f : -1.0f);
    uvGain_ = clampf(uvGain_, 0.0f, 1.0f);
    return pass + uvGain_ * (psola - pass);
}

void PitchCorrector::psolaDriveForTest(float appliedHz, float ratio) noexcept {
    driveHz_ = (appliedHz > 0.0f && ratio > 0.0f) ? appliedHz : 0.0f;
    driveRatio_ = ratio > 0.0f ? ratio : 1.0f;
}

void PitchCorrector::updateTarget() noexcept {
    applyDetection(detect());
}

float PitchCorrector::trackMedian() const noexcept {
    if (trackCount_ <= 0) return 0.0f;
    if (trackCount_ == 1) return trackHz_[2];
    if (trackCount_ == 2) return 0.5f * (trackHz_[1] + trackHz_[2]);
    const float a = trackHz_[0], b = trackHz_[1], c = trackHz_[2];
    return std::max(std::min(a, b), std::min(std::max(a, b), c));
}

void PitchCorrector::pushTrack(float hz) noexcept {
    trackHz_[0] = trackHz_[1];
    trackHz_[1] = trackHz_[2];
    trackHz_[2] = hz;
    trackAt_[0] = trackAt_[1];
    trackAt_[1] = trackAt_[2];
    trackAt_[2] = updateIndex_;
    trackCount_ = std::min(trackCount_ + 1, 3);
}

void PitchCorrector::reseedTrack(float hz) noexcept {
    trackHz_.fill(hz);
    trackAt_.fill(updateIndex_);
    trackCount_ = 3;
}

void PitchCorrector::reportUnvoiced(bool hard) noexcept {
    // hard = a true no-estimate update (silence): agreement restarts from
    // scratch. Soft = an admitted estimate that was not reportable and not
    // holdable: the D1 bookkeeping it just did stays valid.
    if (hard) {
        stableCount_ = 0;
        lastFrequencyHz_ = 0.0f;
    }
    lastTargetHz_ = 0.0f;
    lastTargetRatio_ = 1.0f;
    holdFrames_ = 0;
    ++gapFrames_;
    if (gapFrames_ > breathHops_) {
        // Gap longer than the breath window: the note context is gone.
        trackCount_ = 0;
        pendingCount_ = 0;
        appliedHz_ = 0.0f;
    }
    detectedHz_.store(0.0f);
    targetHz_.store(0.0f);
    cents_.store(0.0f);
    voiced_.store(false);
}

void PitchCorrector::applyDetection(const DetOut& r) noexcept {
    // --- D2 thresholds ------------------------------------------------------
    // A candidate farther than this from the running median of the accepted
    // estimates is a jump, not a track update.
    constexpr float kTrackJumpCents = 300.0f;
    // A pending jump is accepted when the next estimate lands within this of
    // it — i.e. two consecutive agreeing detections.
    constexpr float kPendingAgreeCents = 150.0f;
    // Schmitt band on the voiced flag: a non-reportable estimate holds voiced
    // when it is a strong-pass frame (>= kHoldConf; glitches and real jumps
    // live here) or a weak one still on the tracked note (>= kExitConf).
    // Below kExitConf the flag releases. Entering voiced keeps the D1 rule
    // (confirm/agreement gate), so enter and exit conditions genuinely differ.
    constexpr float kHoldConf = 0.70f;
    constexpr float kExitConf = 0.35f;
    // Bound on consecutive non-reportable holds (~43 ms @48k): a transition
    // the confirmer cannot see must still end.
    constexpr int kHoldMax = 4;

    ++updateIndex_;
    // Relaxed-pass estimates are weaker by construction (they exist only
    // where the strict scan failed); their acceptance bar is 1 - tAcceptRelax.
    const float gate = r.relaxed ? 0.44f : 0.73f;
    const bool hadTrack = trackCount_ > 0;

    if (r.hz <= 0.0f || r.conf < gate) {
        if (r.hz > 0.0f && hadTrack && r.conf >= kExitConf && holdFrames_ < kHoldMax &&
            std::abs(1200.0f * std::log2(r.hz / trackMedian())) <= kTrackJumpCents) {
            // Sub-gate estimate still on the tracked note — a weak patch, not
            // a phrase end. Hold voiced instead of flapping. (Unreachable for
            // the D1 passes, whose estimates always clear `gate`; the test
            // seam exercises this band directly.)
            ++holdFrames_;
            gapFrames_ = 0;
            // D1 reporting rule per class: tentative frames showed 0 Hz until
            // confirmed; strict frames showed their raw Hz. Hold mirrors it so
            // the reported estimate stream is unchanged — only the flag holds.
            detectedHz_.store((r.relaxed || r.provisional) ? 0.0f : r.hz);
            voiced_.store(true);
            return;
        }
        reportUnvoiced(r.hz <= 0.0f);
        return;
    }

    // Agreement is tracked on every admitted estimate (raw values only), even
    // ones not yet reported — that is what lets buried-F0 estimates confirm.
    // Tentative passes (provisional/relaxed) agree on a tighter window:
    // noise troughs rarely repeat within a semitone twice running.
    const float prev = lastFrequencyHz_;
    const float agreeSt = (r.relaxed || r.provisional) ? 1.0f : 1.7f;
    const bool agree =
        prev > 0.0f && std::abs(12.0f * std::log2(r.hz / prev)) < agreeSt;
    stableCount_ = agree ? std::min(stableCount_ + 1, 10) : 0;
    lastFrequencyHz_ = r.hz;
    const bool confirmed = stableCount_ >= 1;
    // Armed onsets accept one confident estimate immediately.
    const bool armedHit = armedLeft_ > 0 && r.conf >= 0.78f;
    const bool reportable = confirmed || armedHit;

    float exHz = r.hz;
    if (confirmed && !r.relaxed && prev > 0.0f) {
        // Lag compensation: linearly project the last raw estimate forward by
        // the analysis window's effective staleness. Bounded to +/-50 cents
        // and disabled across real jumps (> 1 semitone per hop), so steady
        // tones are untouched (dCents ~ 0) and vibrato/portamento lag shrinks.
        const float dCents = 1200.0f * std::log2(r.hz / prev);
        if (std::abs(dCents) <= 120.0f) {
            const float hopSec = static_cast<float>(kDetectHop) /
                                 static_cast<float>(sampleRate_);
            const float move = clampf(dCents * r.leadSec / hopSec, -50.0f, 50.0f);
            exHz = r.hz * std::pow(2.0f, move / 1200.0f);
        }
    }

    // --- D2 tracker ingest ---------------------------------------------------
    // Only reportable or strong estimates touch the ring: weak unconfirmed
    // (relaxed) detections are the noise-suspect class and never enter it.
    bool accepted = false;
    if (reportable || r.conf >= kHoldConf) {
        if (!hadTrack) {
            pushTrack(exHz);
            accepted = true;
        } else {
            const float med = trackMedian();
            const float dCents = std::abs(1200.0f * std::log2(exHz / med));
            if (dCents <= kTrackJumpCents) {
                pushTrack(exHz);
                accepted = true;
                pendingCount_ = 0;  // a track-agreeing frame resolves any jump
            } else if (pendingCount_ > 0 &&
                       std::abs(1200.0f * std::log2(exHz / pendingHz_)) <=
                           kPendingAgreeCents) {
                if (++pendingCount_ >= 2) {
                    reseedTrack(exHz);
                    accepted = true;
                    pendingCount_ = 0;
                }
            } else {
                pendingHz_ = exHz;
                pendingCount_ = 1;
            }
        }
        if (accepted) appliedHz_ = trackMedian();
    }

    if (reportable) {
        holdFrames_ = 0;
        gapFrames_ = 0;
        // Telemetry stays the detector's own estimate (D1 parity); the
        // median-of-3 track drives the correction target, not the report.
        detectedHz_.store(exHz);
        voiced_.store(true);
        if (!accepted) return;  // jump still confirming: flag on, target frozen
        const float target = nearestScaleFrequency(appliedHz_, root_.load(), scale_.load());
        if (target <= 0.0f) return;
        lastTargetHz_ = target;
        const float correction = 1200.0f * std::log2(target / appliedHz_);
        const float fraction = amount_.load(std::memory_order_relaxed);
        lastTargetRatio_ = std::pow(2.0f, clampf(correction, -300.0f, 300.0f) * fraction / 1200.0f);
        targetHz_.store(target);
        cents_.store(correction * fraction);
        return;
    }

    // Non-reportable admitted estimate: hold the voiced flag through strong
    // frames (a disagreeing one is a jump candidate being confirmed) and
    // through weak estimates still on the tracked note; drop everything else
    // exactly like D1. The target chain stays frozen, so the correction does
    // not sag toward unity while a glitch is absorbed.
    const float med = trackMedian();
    const bool onTrack = med > 0.0f &&
        std::abs(1200.0f * std::log2(r.hz / med)) <= kTrackJumpCents;
    if (hadTrack && holdFrames_ < kHoldMax &&
        (r.conf >= kHoldConf || (r.conf >= kExitConf && onTrack))) {
        ++holdFrames_;
        gapFrames_ = 0;
        detectedHz_.store((r.relaxed || r.provisional) ? 0.0f : r.hz);
        voiced_.store(true);
        return;
    }
    reportUnvoiced(false);
}

void PitchCorrector::updateTargetForTest(float hz, float conf, bool relaxed,
                                         bool provisional, float leadSec) noexcept {
    DetOut r;
    r.hz = hz;
    r.conf = conf;
    r.leadSec = leadSec;
    r.relaxed = relaxed;
    r.provisional = provisional;
    applyDetection(r);
}

void PitchCorrector::process(const float* input, float* output, int frames, int outChannels) noexcept {
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
        const float processed = amount < 0.001f ? in : psolaShift(smoothedRatio_);
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

}  // namespace dautotune

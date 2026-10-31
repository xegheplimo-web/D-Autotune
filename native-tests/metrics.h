#pragma once

// D0 metrics: F0 cents error, octave-error rate, voiced/unvoiced confusion,
// and per-call execution time. Test-only code — allocation is allowed.

#include <cstddef>
#include <vector>

namespace metrics {

struct Stats {
    double median = 0.0;
    double p95 = 0.0;
    std::size_t n = 0;
};

// Median and p95 via linear interpolation on the sorted values.
Stats summarize(std::vector<double> values);

// One evaluated chunk: detector output vs. ground truth.
struct Frame {
    float estHz = 0.0f;    // PitchCorrector::detectedHz(); 0 = no estimate
    bool estVoiced = false; // PitchCorrector::voiced()
    float gtHz = 0.0f;     // ground-truth F0; 0 = unvoiced
};

struct Confusion {
    std::size_t tvEv = 0;  // true voiced / est voiced
    std::size_t tvEu = 0;  // true voiced / est unvoiced  (missed voicing, FVo)
    std::size_t tuEv = 0;  // true unvoiced / est voiced (false voicing, FUV)
    std::size_t tuEu = 0;  // true unvoiced / est unvoiced
    std::size_t total() const { return tvEv + tvEu + tuEv + tuEu; }
    double accuracy() const;
};

struct ItemMetrics {
    // |1200*log2(est/gt)| over frames with gtHz > 0 AND a valid estimate
    // (estHz > 0). Frames with no estimate are counted in detectionRate,
    // not here — cents error is undefined without an estimate.
    Stats centsErr;
    // Fraction of cents-scored frames whose |error| lands within
    // octaveBandCents of 1200 or 600 cents (octave / half-octave class errors).
    double octaveRate = 0.0;
    Confusion vuv;
    std::size_t voicedFrames = 0;    // scored frames with gtHz > 0
    std::size_t detectedFrames = 0;  // of those, frames with estHz > 0
    Stats callUs;                    // process() wall time, microseconds per call
    double detectionRate() const { return voicedFrames ? static_cast<double>(detectedFrames) /
                                                            static_cast<double>(voicedFrames)
                                                       : 0.0; }
};

ItemMetrics evaluate(const std::vector<Frame>& frames, const std::vector<double>& callUs,
                     double octaveBandCents);

}  // namespace metrics

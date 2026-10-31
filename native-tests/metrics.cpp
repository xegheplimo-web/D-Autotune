#include "metrics.h"

#include <algorithm>
#include <cmath>

namespace metrics {
namespace {

double quantile(const std::vector<double>& sorted, double q) {
    if (sorted.empty()) return 0.0;
    const double pos = q * static_cast<double>(sorted.size() - 1);
    const std::size_t lo = static_cast<std::size_t>(pos);
    const std::size_t hi = std::min(lo + 1, sorted.size() - 1);
    return sorted[lo] + (sorted[hi] - sorted[lo]) * (pos - static_cast<double>(lo));
}

}  // namespace

Stats summarize(std::vector<double> values) {
    Stats s;
    s.n = values.size();
    if (values.empty()) return s;
    std::sort(values.begin(), values.end());
    s.median = quantile(values, 0.5);
    s.p95 = quantile(values, 0.95);
    return s;
}

double Confusion::accuracy() const {
    const std::size_t t = total();
    return t ? static_cast<double>(tvEv + tuEu) / static_cast<double>(t) : 0.0;
}

ItemMetrics evaluate(const std::vector<Frame>& frames, const std::vector<double>& callUs,
                     double octaveBandCents) {
    ItemMetrics m;
    std::vector<double> errs;
    errs.reserve(frames.size());
    std::size_t octaveLike = 0;
    for (const Frame& f : frames) {
        const bool gtVoiced = f.gtHz > 0.0f;
        if (gtVoiced) {
            ++m.voicedFrames;
            if (f.estVoiced) ++m.vuv.tvEv; else ++m.vuv.tvEu;
            if (f.estHz > 0.0f) {
                ++m.detectedFrames;
                const double err = std::abs(1200.0 * std::log2(f.estHz / f.gtHz));
                errs.push_back(err);
                const bool octaveBand = std::abs(err - 1200.0) <= octaveBandCents;
                const bool halfOctaveBand = std::abs(err - 600.0) <= octaveBandCents;
                if (octaveBand || halfOctaveBand) ++octaveLike;
            }
        } else {
            if (f.estVoiced) ++m.vuv.tuEv; else ++m.vuv.tuEu;
        }
    }
    m.centsErr = summarize(std::move(errs));
    m.octaveRate = m.centsErr.n ? static_cast<double>(octaveLike) / static_cast<double>(m.centsErr.n)
                                : 0.0;
    m.callUs = summarize(std::vector<double>(callUs));
    return m;
}

}  // namespace metrics

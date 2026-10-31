#include "PingMath.h"
#include <algorithm>
#include <cmath>

namespace dautotune::ping {
namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = 2.0 * kPi;
// Windows below this centered energy are treated as silence: no denominator,
// no fabricated correlation.
constexpr double kMinEnergy = 1e-12;
}  // namespace

void generateChirp(float* dst, int32_t frames, int32_t sampleRate,
                   float f0Hz, float f1Hz, float amplitude) noexcept {
    if (!dst || frames <= 0 || sampleRate <= 0) return;
    const double duration = static_cast<double>(frames) / sampleRate;
    const double sweep = (static_cast<double>(f1Hz) - f0Hz) / duration;  // Hz/s
    const int32_t edge = std::max<int32_t>(1, frames / 40);              // 2.5% taper
    for (int32_t n = 0; n < frames; ++n) {
        const double t = static_cast<double>(n) / sampleRate;
        const double phase = kTwoPi * (static_cast<double>(f0Hz) * t + 0.5 * sweep * t * t);
        double env = 1.0;
        if (n < edge)
            env = 0.5 - 0.5 * std::cos(kPi * static_cast<double>(n) / edge);
        else if (n >= frames - edge)
            env = 0.5 - 0.5 * std::cos(kPi * static_cast<double>(frames - n) / edge);
        dst[n] = static_cast<float>(amplitude * env * std::sin(phase));
    }
}

ScanResult findLag(const float* ref, int32_t refLen,
                   const float* cap, int32_t capLen) noexcept {
    ScanResult best{};
    if (!ref || !cap || refLen <= 0 || capLen < refLen) return best;
    const int32_t maxLag = capLen - refLen;
    const double n = static_cast<double>(refLen);

    double refSum = 0.0;
    for (int32_t i = 0; i < refLen; ++i) refSum += ref[i];
    const double refMean = refSum / n;
    double refE = 0.0;
    for (int32_t i = 0; i < refLen; ++i) {
        const double d = static_cast<double>(ref[i]) - refMean;
        refE += d * d;
    }
    if (!(refE > kMinEnergy)) return best;  // also rejects NaN energy

    // Sliding window sum/energy keep the normalization O(1) per lag; only the
    // dot product itself is O(refLen) - a flat, vectorizable MAC loop.
    double capSum = 0.0, capE = 0.0;
    for (int32_t i = 0; i < refLen; ++i) {
        capSum += cap[i];
        capE += static_cast<double>(cap[i]) * cap[i];
    }

    double bestNcc = -2.0;  // below the NCC floor so the first candidate wins
    for (int32_t lag = 0; lag <= maxLag; ++lag) {
        const double capMean = capSum / n;
        const double capVar = capE - capSum * capMean;  // centered energy
        float ncc = 0.0f;
        if (capVar > kMinEnergy) {
            float dot = 0.0f;
            for (int32_t i = 0; i < refLen; ++i) dot += ref[i] * cap[lag + i];
            // sum((ref-rm)(cap-cm)) = dot - rm*sumCap - cm*sumRef + n*rm*cm
            const double cdot = static_cast<double>(dot) - refMean * capSum
                                - capMean * refSum + n * refMean * capMean;
            ncc = std::clamp(static_cast<float>(cdot / std::sqrt(refE * capVar)),
                             -1.0f, 1.0f);
        }
        if (ncc > bestNcc) {
            bestNcc = ncc;
            best.lag = lag;
            best.ncc = ncc;
        }
        if (lag < maxLag) {
            const float leaving = cap[lag];
            const float entering = cap[lag + refLen];
            capSum += static_cast<double>(entering) - leaving;
            capE += static_cast<double>(entering) * entering
                    - static_cast<double>(leaving) * leaving;
        }
    }
    if (best.lag >= 0) best.clipped = (best.lag == 0 || best.lag == maxLag);
    return best;
}

int32_t nearestRankIndex(int32_t count, double percentile) noexcept {
    if (count <= 0) return -1;
    const double rank = std::ceil(percentile / 100.0 * static_cast<double>(count));
    return std::clamp(static_cast<int32_t>(rank) - 1, 0, count - 1);
}

}  // namespace dautotune::ping

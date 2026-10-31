// D1 vs D0 improvement harness.
//
// Runs the SAME golden corpus through the frozen D0 reference detector
// (d0_reference.cpp, detector as of commit a6020ae) and the current (D1)
// PitchCorrector side by side, scores both with the same metrics module, and
// prints the mandated D0-vs-D1 comparison table. Both columns are measured
// live in this binary — the D0 column should reproduce
// docs/baseline/D0_BASELINE.md within timing noise.
//
// Unlike d0_baseline, this test DOES assert improvement gates (see checks at
// the bottom): D1 must beat D0 on overall median cents, V/UV accuracy and the
// 5 dB SNR cliff, without growing octave errors or false voicing.

#include "PitchCorrector.h"
#include "d0_reference.h"
#include "corpus_gen.h"
#include "metrics.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace {

constexpr int kChunk = 512;
constexpr int kOutChannels = 2;
constexpr std::uint64_t kSeed = 0xD0BA5EEDull;
constexpr int kWarmupChunks = 5;
constexpr double kOctaveBandCents = 150.0;

int gFailures = 0;

void check(bool ok, const std::string& what) {
    if (ok) {
        std::cout << "PASS: " << what << "\n";
    } else {
        ++gFailures;
        std::cout << "FAIL: " << what << "\n";
    }
}

struct Run {
    std::vector<metrics::Frame> frames;
    std::vector<double> us;
    bool typeOk = true;
    bool outOk = true;
};

// Identical feed for both detectors: same chunking, params and scoring as
// test_d0_baseline (one corrector per item, read flags after every call).
template <typename Dsp>
Run runItem(const corpus::Item& item) {
    Dsp dsp(item.sampleRate);
    dsp.setParameters(0, 0, 0.9f, 35.0f, 0.1f);
    const std::size_t nChunks = item.samples.size() / kChunk;
    std::vector<float> out(static_cast<std::size_t>(kChunk) * kOutChannels);
    Run r;
    r.frames.reserve(nChunks);
    r.us.reserve(nChunks);
    using clk = std::chrono::steady_clock;
    for (std::size_t c = 0; c < nChunks; ++c) {
        const float* in = item.samples.data() + c * kChunk;
        const auto t0 = clk::now();
        dsp.process(in, out.data(), kChunk, kOutChannels);
        const auto t1 = clk::now();
        const float est = dsp.detectedHz();
        if (!(std::isfinite(est) && est >= 0.0f)) r.typeOk = false;
        if (!std::all_of(out.begin(), out.end(),
                         [](float v) { return std::isfinite(v); }))
            r.outOk = false;
        if (static_cast<int>(c) >= kWarmupChunks) {
            r.us.push_back(static_cast<double>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count()) /
                1000.0);
            r.frames.push_back({est, dsp.voiced(), item.gtF0Hz[c]});
        }
    }
    return r;
}

void printCmpHeader() {
    std::printf("| item                     | D0 med c | D1 med c | D0 p95 | D1 p95 | D0 oct%% | D1 oct%% | D0 vuv%% | D1 vuv%% | D0 us | D1 us |\n");
    std::printf("|--------------------------|---------:|---------:|-------:|-------:|-------:|-------:|-------:|-------:|------:|------:|\n");
}

void printCmpRow(const char* name, const metrics::ItemMetrics& d0,
                 const metrics::ItemMetrics& d1) {
    std::printf("| %-24s | %8.1f | %8.1f | %6.1f | %6.1f | %6.2f | %6.2f | %7.1f | %7.1f | %5.1f | %5.1f |\n",
                name, d0.centsErr.median, d1.centsErr.median, d0.centsErr.p95,
                d1.centsErr.p95, 100.0 * d0.octaveRate, 100.0 * d1.octaveRate,
                100.0 * d0.vuv.accuracy(), 100.0 * d1.vuv.accuracy(),
                d0.callUs.median, d1.callUs.median);
}

void printDetHeader() {
    std::printf("| item                     | D0 det%% | D1 det%% | D0 tvEu | D1 tvEu | D0 tuEv | D1 tuEv |\n");
    std::printf("|--------------------------|--------:|--------:|--------:|--------:|--------:|--------:|\n");
}

void printDetRow(const char* name, const metrics::ItemMetrics& d0,
                 const metrics::ItemMetrics& d1) {
    std::printf("| %-24s | %7.1f | %7.1f | %7zu | %7zu | %7zu | %7zu |\n",
                name, 100.0 * d0.detectionRate(), 100.0 * d1.detectionRate(),
                d0.vuv.tvEu, d1.vuv.tvEu, d0.vuv.tuEv, d1.vuv.tuEv);
}

}  // namespace

int main() {
    std::vector<metrics::Frame> grandFrames0, grandFrames1;
    std::vector<double> grandUs0, grandUs1;
    std::map<std::string, metrics::ItemMetrics> d1ByName;

    std::printf("D1 improvement harness — live A/B: frozen D0 reference vs current detector\n");
    std::printf("config: root=0 scale=major amount=0.9 retune=35ms reverb=0.1; "
                "chunk=%d out=%dch warmup=%d chunks; seed=0x%llx; octave band=+/-%.0f cents\n\n",
                kChunk, kOutChannels, kWarmupChunks,
                static_cast<unsigned long long>(kSeed), kOctaveBandCents);

    for (const int sr : {48000, 44100}) {
        const std::vector<corpus::Item> corpusA = corpus::build(sr, kChunk, kSeed);
        const std::vector<corpus::Item> corpusB = corpus::build(sr, kChunk, kSeed);
        check(!corpusA.empty(), "corpus built @ " + std::to_string(sr));

        std::uint64_t corpusHash = 1469598103934665603ull;
        bool deterministic = true;
        const std::size_t nItems = std::min(corpusA.size(), corpusB.size());
        for (std::size_t i = 0; i < nItems; ++i) {
            const std::uint64_t h = corpus::hash(corpusA[i]);
            corpusHash ^= h;
            corpusHash *= 1099511628211ull;
            if (corpus::hash(corpusB[i]) != h) deterministic = false;
        }
        check(deterministic, "corpus deterministic across 2 builds @ " + std::to_string(sr));
        std::printf("sr=%d  items=%zu  corpus_hash=0x%016llx\n\n", sr, corpusA.size(),
                    static_cast<unsigned long long>(corpusHash));

        printCmpHeader();
        std::vector<metrics::Frame> srFrames0, srFrames1;
        std::vector<double> srUs0, srUs1;
        std::vector<std::pair<std::string, std::pair<metrics::ItemMetrics, metrics::ItemMetrics>>>
            detRows;
        std::string badType, badOut, badShape;
        for (const corpus::Item& item : corpusA) {
            const std::size_t nChunks = item.samples.size() / kChunk;
            if (nChunks != item.gtF0Hz.size() || static_cast<int>(nChunks) <= kWarmupChunks)
                badShape += " " + item.name;

            const Run r0 = runItem<d0ref::PitchCorrectorD0>(item);
            const Run r1 = runItem<dautotune::PitchCorrector>(item);
            if (!r0.typeOk || !r1.typeOk) badType += " " + item.name;
            if (!r0.outOk || !r1.outOk) badOut += " " + item.name;

            const metrics::ItemMetrics m0 = metrics::evaluate(r0.frames, r0.us, kOctaveBandCents);
            const metrics::ItemMetrics m1 = metrics::evaluate(r1.frames, r1.us, kOctaveBandCents);
            printCmpRow(item.name.c_str(), m0, m1);
            detRows.push_back({item.name, {m0, m1}});
            d1ByName[item.name + "@" + std::to_string(sr)] = m1;
            srFrames0.insert(srFrames0.end(), r0.frames.begin(), r0.frames.end());
            srFrames1.insert(srFrames1.end(), r1.frames.begin(), r1.frames.end());
            srUs0.insert(srUs0.end(), r0.us.begin(), r0.us.end());
            srUs1.insert(srUs1.end(), r1.us.begin(), r1.us.end());
        }
        const metrics::ItemMetrics all0 = metrics::evaluate(srFrames0, srUs0, kOctaveBandCents);
        const metrics::ItemMetrics all1 = metrics::evaluate(srFrames1, srUs1, kOctaveBandCents);
        printCmpRow("** ALL @ sr **", all0, all1);
        check(badShape.empty(), "corpus items well-formed @ " + std::to_string(sr) + badShape);
        check(badType.empty(), "detectedHz finite & >= 0 on all chunks @ " + std::to_string(sr) + badType);
        check(badOut.empty(), "process output finite on all chunks @ " + std::to_string(sr) + badOut);

        std::printf("\ndet%% / confusion detail @%d\n", sr);
        printDetHeader();
        for (const auto& row : detRows)
            printDetRow(row.first.c_str(), row.second.first, row.second.second);
        printDetRow("** ALL @ sr **", all0, all1);
        std::printf("\nconfusion @%d: D0 tvEv=%zu tvEu=%zu tuEv=%zu tuEu=%zu | "
                    "D1 tvEv=%zu tvEu=%zu tuEv=%zu tuEu=%zu\n",
                    sr, all0.vuv.tvEv, all0.vuv.tvEu, all0.vuv.tuEv, all0.vuv.tuEu,
                    all1.vuv.tvEv, all1.vuv.tvEu, all1.vuv.tuEv, all1.vuv.tuEu);
        std::printf("timing   @%d: D0 med=%.1f p95=%.1f | D1 med=%.1f p95=%.1f us/call\n\n",
                    sr, all0.callUs.median, all0.callUs.p95,
                    all1.callUs.median, all1.callUs.p95);

        grandFrames0.insert(grandFrames0.end(), srFrames0.begin(), srFrames0.end());
        grandFrames1.insert(grandFrames1.end(), srFrames1.begin(), srFrames1.end());
        grandUs0.insert(grandUs0.end(), srUs0.begin(), srUs0.end());
        grandUs1.insert(grandUs1.end(), srUs1.begin(), srUs1.end());
    }

    const metrics::ItemMetrics d0 = metrics::evaluate(grandFrames0, grandUs0, kOctaveBandCents);
    const metrics::ItemMetrics d1 = metrics::evaluate(grandFrames1, grandUs1, kOctaveBandCents);
    std::printf("GRAND TOTAL: scored=%zu\n"
                "  D0: det=%.1f%% med=%.1fc p95=%.1fc oct=%.2f%% vuv=%.1f%% call med=%.1fus p95=%.1fus\n"
                "  D1: det=%.1f%% med=%.1fc p95=%.1fc oct=%.2f%% vuv=%.1f%% call med=%.1fus p95=%.1fus\n\n",
                grandFrames0.size(), 100.0 * d0.detectionRate(), d0.centsErr.median,
                d0.centsErr.p95, 100.0 * d0.octaveRate, 100.0 * d0.vuv.accuracy(),
                d0.callUs.median, d0.callUs.p95, 100.0 * d1.detectionRate(),
                d1.centsErr.median, d1.centsErr.p95, 100.0 * d1.octaveRate,
                100.0 * d1.vuv.accuracy(), d1.callUs.median, d1.callUs.p95);

    std::printf("--- D1 gates ---\n");
    check(d1.detectionRate() >= d0.detectionRate(),
          "D1 overall detection rate >= D0");
    check(d1.centsErr.median < d0.centsErr.median,
          "D1 overall median cents < D0");
    check(d1.centsErr.p95 < d0.centsErr.p95,
          "D1 overall p95 cents < D0");
    check(d1.octaveRate <= d0.octaveRate + 0.002,
          "D1 octave-error rate within +0.2pp of D0 (D0=0)");
    check(d1.vuv.accuracy() >= d0.vuv.accuracy(),
          "D1 V/UV accuracy >= D0");
    check(d1.vuv.tuEv <= d0.vuv.tuEv,
          "D1 false-voicing (tuEv) does not grow");
    check(d1.callUs.median <= 60.0 && d1.callUs.median <= 3.0 * d0.callUs.median,
          "D1 median call <= 60us and <= 3x D0");
    for (const int sr : {48000, 44100}) {
        const std::string key5 = "snr_5db@" + std::to_string(sr);
        const std::string key0 = "snr_0db@" + std::to_string(sr);
        check(d1ByName[key5].detectionRate() >= 0.80,
              "snr_5db D1 det% >= 80 @ " + std::to_string(sr));
        check(d1ByName[key5].centsErr.median <= 30.0,
              "snr_5db D1 median <= 30c @ " + std::to_string(sr));
        check(d1ByName[key0].detectionRate() <= 0.60,
              "snr_0db D1 det% stays low (no noise lock) @ " + std::to_string(sr));
    }

    if (gFailures) {
        std::printf("D1 IMPROVEMENT HARNESS: %d FAILURES\n", gFailures);
        return 1;
    }
    std::printf("D1 IMPROVEMENT HARNESS OK\n");
    return 0;
}

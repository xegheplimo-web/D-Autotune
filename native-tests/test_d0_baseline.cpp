// D0 — DSP baseline + golden corpus harness.
//
// Measures the CURRENT PitchCorrector detector on the synthetic corpus and
// prints a markdown table (copied verbatim into docs/baseline/D0_BASELINE.md).
// It asserts NOTHING about absolute quality — the numbers are the floor D1
// must beat. It only asserts that the harness runs, the corpus is
// deterministic, and the detector produces the expected output types
// (detectedHz finite and >= 0, voiced flag readable, process output finite).

#include "PitchCorrector.h"
#include "corpus_gen.h"
#include "metrics.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

constexpr int kChunk = 512;                  // AudioEngine::kChunk
constexpr int kOutChannels = 2;              // app renders interleaved stereo
constexpr std::uint64_t kSeed = 0xD0BA5EEDull;
// Detector needs kDetectWindow*kDownsample = 2048 samples before its first
// estimate; score from chunk 5 onward (first 4 chunks cannot have an estimate,
// chunk 4 carries only the very first estimate).
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

void printRow(const char* name, int sr, const metrics::ItemMetrics& m, std::size_t scored) {
    std::printf("| %-24s | %5d | %4zu | %5.1f | %7.1f | %7.1f | %5.2f | %5.1f | %8.1f | %8.1f |\n",
                name, sr, scored, 100.0 * m.detectionRate(), m.centsErr.median, m.centsErr.p95,
                100.0 * m.octaveRate, 100.0 * m.vuv.accuracy(), m.callUs.median, m.callUs.p95);
}

void printHeader() {
    std::printf("| item                     |    sr | scored | det%% | med cents | p95 cents | oct%% | vuv%% | med us/call | p95 us/call |\n");
    std::printf("|--------------------------|------:|-------:|-----:|--------:|--------:|-----:|-----:|------------:|------------:|\n");
}

}  // namespace

int main() {
    using dautotune::PitchCorrector;
    using clk = std::chrono::steady_clock;

    std::vector<metrics::Frame> grandFrames;
    std::vector<double> grandUs;

    std::printf("D0 baseline harness — PitchCorrector as-is\n");
    std::printf("config: root=0 scale=major amount=0.9 retune=35ms reverb=0.1; "
                "chunk=%d out=%dch warmup=%d chunks; seed=0x%llx; octave band=+/-%.0f cents\n\n",
                kChunk, kOutChannels, kWarmupChunks, static_cast<unsigned long long>(kSeed),
                kOctaveBandCents);

    for (const int sr : {48000, 44100}) {
        const std::vector<corpus::Item> corpusA = corpus::build(sr, kChunk, kSeed);
        const std::vector<corpus::Item> corpusB = corpus::build(sr, kChunk, kSeed);
        check(!corpusA.empty(), "corpus built @ " + std::to_string(sr));
        check(corpusA.size() == corpusB.size(), "corpus size stable @ " + std::to_string(sr));

        std::uint64_t corpusHash = 1469598103934665603ull;
        bool deterministic = true;
        const std::size_t nItems = std::min(corpusA.size(), corpusB.size());
        for (std::size_t i = 0; i < nItems; ++i) {
            const std::uint64_t h = corpus::hash(corpusA[i]);
            corpusHash ^= h;
            corpusHash *= 1099511628211ull;
            if (corpus::hash(corpusB[i]) != h) {
                deterministic = false;
                std::cout << "  hash mismatch: " << corpusA[i].name << "\n";
            }
        }
        check(deterministic, "corpus deterministic across 2 builds @ " + std::to_string(sr));
        std::printf("sr=%d  items=%zu  corpus_hash=0x%016llx\n\n", sr, corpusA.size(),
                    static_cast<unsigned long long>(corpusHash));

        printHeader();
        std::vector<metrics::Frame> srFrames;
        std::vector<double> srUs;
        std::string badType, badOut, badShape;
        for (const corpus::Item& item : corpusA) {
            PitchCorrector dsp(item.sampleRate);
            dsp.setParameters(0, 0, 0.9f, 35.0f, 0.1f);
            const std::size_t nChunks = item.samples.size() / kChunk;
            if (nChunks != item.gtF0Hz.size() || static_cast<int>(nChunks) <= kWarmupChunks)
                badShape += " " + item.name;

            std::vector<float> out(static_cast<std::size_t>(kChunk) * kOutChannels);
            std::vector<metrics::Frame> frames;
            std::vector<double> us;
            frames.reserve(nChunks);
            us.reserve(nChunks);
            bool typeOk = true;
            bool outOk = true;
            for (std::size_t c = 0; c < nChunks; ++c) {
                const float* in = item.samples.data() + c * kChunk;
                const auto t0 = clk::now();
                dsp.process(in, out.data(), kChunk, kOutChannels);
                const auto t1 = clk::now();
                const float est = dsp.detectedHz();
                if (!(std::isfinite(est) && est >= 0.0f)) typeOk = false;
                if (!std::all_of(out.begin(), out.end(),
                                 [](float v) { return std::isfinite(v); }))
                    outOk = false;
                if (static_cast<int>(c) >= kWarmupChunks) {
                    us.push_back(static_cast<double>(
                        std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count()) /
                        1000.0);
                    frames.push_back({est, dsp.voiced(), item.gtF0Hz[c]});
                }
            }
            if (!typeOk) badType += " " + item.name;
            if (!outOk) badOut += " " + item.name;

            const metrics::ItemMetrics m = metrics::evaluate(frames, us, kOctaveBandCents);
            printRow(item.name.c_str(), sr, m, frames.size());
            srFrames.insert(srFrames.end(), frames.begin(), frames.end());
            srUs.insert(srUs.end(), us.begin(), us.end());
        }
        check(badShape.empty(), "corpus items well-formed @ " + std::to_string(sr) + badShape);
        check(badType.empty(), "detectedHz finite & >= 0 on all chunks @ " + std::to_string(sr) + badType);
        check(badOut.empty(), "process output finite on all chunks @ " + std::to_string(sr) + badOut);
        const metrics::ItemMetrics all = metrics::evaluate(srFrames, srUs, kOctaveBandCents);
        printRow("** ALL @ sr **", sr, all, srFrames.size());
        std::printf("\nconfusion @%d: tvEv=%zu tvEu(FVo)=%zu tuEv(FUV)=%zu tuEu=%zu\n",
                    sr, all.vuv.tvEv, all.vuv.tvEu, all.vuv.tuEv, all.vuv.tuEu);
        std::printf("timing   @%d: %d-sample calls: median=%.1f us p95=%.1f us; per-sample median=%.1f ns\n\n",
                    sr, kChunk, all.callUs.median, all.callUs.p95,
                    1000.0 * all.callUs.median / kChunk);
        grandFrames.insert(grandFrames.end(), srFrames.begin(), srFrames.end());
        grandUs.insert(grandUs.end(), srUs.begin(), srUs.end());
    }

    const metrics::ItemMetrics grand = metrics::evaluate(grandFrames, grandUs, kOctaveBandCents);
    std::printf("GRAND TOTAL: scored=%zu det=%.1f%% med=%.1fc p95=%.1fc oct=%.2f%% vuv=%.1f%% "
                "call med=%.1fus p95=%.1fus\n",
                grandFrames.size(), 100.0 * grand.detectionRate(), grand.centsErr.median,
                grand.centsErr.p95, 100.0 * grand.octaveRate, 100.0 * grand.vuv.accuracy(),
                grand.callUs.median, grand.callUs.p95);

    if (gFailures) {
        std::printf("D0 BASELINE HARNESS: %d FAILURES\n", gFailures);
        return 1;
    }
    std::printf("D0 BASELINE HARNESS OK\n");
    return 0;
}

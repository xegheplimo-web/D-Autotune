// D5 real-voice validation batteries R1-R5 (card G2-D5, GPT round-4 D-015).
//
// Runs the live full chain (D4 host path: amount 1.0, retune 35 ms, C major,
// reverb 0, chunk 512) on the committed real-singing corpus declared by
// docs/corpus/manifest.jsonl. Schema: docs/corpus/README.md.
//
// SKIP DISCIPLINE (HARD-2): when the manifest is absent or declares zero
// clips, every battery prints "SKIP - corpus absent (0 clips)" and the
// binary exits 0. A present-but-corrupt corpus (bad manifest line, SHA-256
// mismatch, non-schema WAV, >15 s, bad annotation) FAILS the binary: real
// evidence must not silently downgrade to a skip.
//
// R1 output-F0 vs annotated reference + correction ratio (+ det% informational)
// R2 formant retention F1/F2 vs input, +-60 Hz gate
// R3 envelope pumping on >=0.5 s sustained segments (prints the D6 trigger)
// R4 artifacts at annotated note boundaries + sustained glitch check
// R5 impulse-centroid DSP delay per corpus sample rate (D4 class 2.27-4.17 ms)

#include "corpus_gen.h"
#include "corpus_loader.h"
#include "d5_measure.h"
#include "metrics.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

namespace {

int gFailures = 0;
int gWarnings = 0;

void check(bool ok, const std::string& what) {
    std::printf("  %s: %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (!ok) ++gFailures;
}

void warn(const std::string& what) {
    std::printf("  WARN: %s\n", what.c_str());
    ++gWarnings;
}

void skip(const char* battery, std::size_t clips) {
    std::printf("%s: SKIP - corpus absent (%zu clips)\n", battery, clips);
}

// Compile-time corpus root; D5_CORPUS_DIR env var overrides it so the harness
// can be pointed at scratch corpora (mechanics verification) or mirrors.
const std::string kCorpus = [] {
    const char* env = std::getenv("D5_CORPUS_DIR");
    return (env && env[0]) ? std::string(env) : std::string(D5_CORPUS_DIR);
}();

// --- corpus loading --------------------------------------------------------------

struct Loaded {
    std::vector<d5corpus::Clip> clips;
    std::size_t rejected = 0;
    bool corpusPresent = false;  // manifest parsed AND >=1 row declared
};

Loaded loadAll() {
    Loaded L;
    std::vector<d5corpus::ClipMeta> rows;
    if (!d5corpus::loadManifest(kCorpus, &rows)) {
        std::printf("FATAL: manifest.jsonl is present but malformed (errors above)\n");
        ++gFailures;
        return L;
    }
    if (rows.empty()) return L;  // absent corpus: the SKIP path
    L.corpusPresent = true;
    for (const d5corpus::ClipMeta& m : rows) {
        d5corpus::Clip c;
        std::string err;
        if (!d5corpus::loadClip(kCorpus, m, &c, &err)) {
            std::printf("  REJECT %s (%s): %s\n", m.clipId.c_str(), m.file.c_str(),
                        err.c_str());
            ++L.rejected;
            continue;
        }
        L.clips.push_back(std::move(c));
    }
    std::printf("corpus: %zu declared, %zu usable, %zu rejected\n", rows.size(),
                L.clips.size(), L.rejected);
    if (L.rejected > 0) {
        std::printf("  FAIL: %zu declared clip(s) failed integrity/schema checks\n",
                    L.rejected);
        ++gFailures;
    }
    if (L.clips.empty() && L.corpusPresent) {
        std::printf("  FAIL: manifest declares clips but none are usable\n");
        ++gFailures;
    }
    return L;
}

// --- shared per-clip render --------------------------------------------------------

std::map<std::string, d5m::Rendered> gRender;

const d5m::Rendered& render(const d5corpus::Clip& c, bool withFrames) {
    const std::string key = c.meta.clipId + (withFrames ? ":f" : "");
    auto it = gRender.find(key);
    if (it != gRender.end()) return it->second;
    d5m::Rendered r = d5m::renderChain(c.samples, c.fs, c.notes, withFrames);
    return gRender.emplace(key, std::move(r)).first->second;
}

// --- R1: output F0 vs annotated reference --------------------------------------------

void batteryR1(const Loaded& L) {
    std::printf("R1. output-F0 vs annotated reference (middle-60%% windows)\n");
    if (!L.corpusPresent) {
        skip("R1", L.clips.size());
        return;
    }
    std::vector<double> errVsRef;    // |cents(out f0, annotated f0)| (applied shift)
    std::vector<double> errVsTgt;    // |cents(out f0, snap target)| (residual)
    std::vector<double> errVsMidi;   // |cents(out f0, etHz(midi))| (informational)
    std::vector<double> errInVsTgt;  // |cents(in f0, snap target)| (sung deviation)
    std::vector<double> corrRatio;
    std::map<std::string, std::vector<double>> perSinger;
    std::size_t segsUsed = 0, windows = 0;
    metrics::Confusion vuvAll;
    std::size_t voicedFrames = 0, detectedFrames = 0;

    for (const d5corpus::Clip& c : L.clips) {
        if (c.notes.empty()) continue;
        const d5m::Rendered& r = render(c, true);
        for (const metrics::Frame& f : r.frames) {
            if (f.gtHz <= 0.0f) continue;
            ++voicedFrames;
            if (f.estVoiced) ++vuvAll.tvEv; else ++vuvAll.tvEu;
            if (f.estHz > 0.0f) ++detectedFrames;
        }
        for (const d5corpus::NoteSeg& s : c.notes) {
            const auto [a, b] = d5m::mid60(s, c.fs, r.rendered);
            if (b - a < static_cast<std::size_t>(0.08 * c.fs)) continue;
            ++segsUsed;
            // Snap target = median of the tracker's chosen grid note across
            // this segment's mid-60% (NOT the annotated midi: a note sung
            // off-scale is corrected to the nearest member, so scoring the
            // output against its own snap target is the honest residual).
            std::vector<double> tgts;
            for (std::size_t ch = a / d5m::kChunk;
                 ch < r.tgtHz.size() && ch * d5m::kChunk < b; ++ch)
                if (r.tgtHz[ch] > 0.0f) tgts.push_back(r.tgtHz[ch]);
            if (tgts.empty()) continue;
            const double tgtHz = metrics::summarize(tgts).median;
            // Sub-window F0 estimates (~120 ms hop 60 ms) across mid-60%.
            std::vector<double> outErrSeg, inErrSeg;
            const int win = static_cast<int>(0.12 * c.fs);
            const int hop = static_cast<int>(0.06 * c.fs);
            const double midiHz = d5m::etHz(s.midi);
            for (std::size_t w = a; w + win <= b; w += hop) {
                const double fo = d5m::measureF0(r.mono.data() + w, win, c.fs);
                const double fi = d5m::measureF0(c.samples.data() + w, win, c.fs);
                if (fo <= 0.0) continue;
                ++windows;
                errVsRef.push_back(std::abs(d5m::cents(fo, s.f0Hz)));
                errVsTgt.push_back(std::abs(d5m::cents(fo, tgtHz)));
                errVsMidi.push_back(std::abs(d5m::cents(fo, midiHz)));
                perSinger[c.meta.singer].push_back(std::abs(d5m::cents(fo, tgtHz)));
                if (fi > 0.0) {
                    inErrSeg.push_back(std::abs(d5m::cents(fi, tgtHz)));
                    outErrSeg.push_back(std::abs(d5m::cents(fo, tgtHz)));
                    errInVsTgt.push_back(std::abs(d5m::cents(fi, tgtHz)));
                }
            }
            const metrics::Stats si = metrics::summarize(inErrSeg);
            const metrics::Stats so = metrics::summarize(outErrSeg);
            if (si.median >= 5.0)  // already-in-tune segments carry no ratio
                corrRatio.push_back((si.median - so.median) / si.median);
        }
    }
    if (segsUsed == 0) {
        warn("R1: corpus has no annotated segments - nothing scored");
        return;
    }
    const metrics::Stats sRef = metrics::summarize(errVsRef);
    const metrics::Stats sTgt = metrics::summarize(errVsTgt);
    const metrics::Stats sMidi = metrics::summarize(errVsMidi);
    const metrics::Stats sIn = metrics::summarize(errInVsTgt);
    const metrics::Stats sCorr = metrics::summarize(corrRatio);
    std::printf("  | metric | n | median | p95 | worst |\n");
    std::printf("  |--------|---|--------|-----|-------|\n");
    auto worst = [](const std::vector<double>& v) {
        return v.empty() ? 0.0 : *std::max_element(v.begin(), v.end());
    };
    std::printf("  | out vs annotated f0, applied shift (c) | %zu | %.1f | %.1f | %.1f |\n",
                sRef.n, sRef.median, sRef.p95, worst(errVsRef));
    std::printf("  | out vs snap target, residual (c) | %zu | %.1f | %.1f | %.1f |\n",
                sTgt.n, sTgt.median, sTgt.p95, worst(errVsTgt));
    std::printf("  | out vs annotated midi (c, informational) | %zu | %.1f | %.1f | %.1f |\n",
                sMidi.n, sMidi.median, sMidi.p95, worst(errVsMidi));
    std::printf("  | in vs snap target, sung deviation (c) | %zu | %.1f | %.1f | %.1f |\n",
                sIn.n, sIn.median, sIn.p95, worst(errInVsTgt));
    std::printf("  | correction ratio (segs, in-err>=5c) | %zu | %.2f | %.2f | - |\n",
                sCorr.n, sCorr.median, sCorr.p95);
    std::printf("  per-singer out-vs-snap-target median (c):\n");
    for (const auto& [singer, v] : perSinger) {
        const metrics::Stats ss = metrics::summarize(v);
        std::printf("    %-12s n=%-5zu med=%6.1f  p95=%6.1f\n", singer.c_str(), ss.n,
                    ss.median, ss.p95);
    }
    const double det = voicedFrames ? static_cast<double>(detectedFrames) /
                                          static_cast<double>(voicedFrames)
                                    : 0.0;
    std::printf("  detector on real voices (informational, no gate): det %.1f%% "
                "of %zu voiced chunks; VUV conf %zu/%zu/%zu/%zu\n",
                100.0 * det, voicedFrames, vuvAll.tvEv, vuvAll.tvEu, vuvAll.tuEv,
                vuvAll.tuEu);
    // D6 octave-tail gate: a ~1200-cent residual is the octave-lock signature
    // (was p95 1204.9/1201.2c on the two female singers pre-fix). 600 cents
    // gives 6x margin over the largest legitimate snap shift (~100c).
    std::size_t octWindows = 0;
    for (const double e : errVsTgt)
        if (e >= 600.0) ++octWindows;
    std::printf("  octave-tail: %zu windows with |residual| >= 600c\n",
                octWindows);
    check(octWindows == 0, "R1: zero octave-tail windows (|residual| >= 600c)");
    check(windows > 0, "R1: scored at least one F0 window");
}

// --- R2: formant retention ------------------------------------------------------------

void batteryR2(const Loaded& L) {
    std::printf("R2. formant retention on sustained segments (+-60 Hz gate)\n");
    if (!L.corpusPresent) {
        skip("R2", L.clips.size());
        return;
    }
    double worstF1 = 0.0, worstF2 = 0.0;
    std::size_t scored = 0;
    for (const d5corpus::Clip& c : L.clips) {
        for (const d5corpus::NoteSeg& s : c.notes) {
            if (s.tEnd - s.tStart < 0.3) continue;
            const auto [a, b] = d5m::mid60(s, c.fs, render(c, false).rendered);
            const int len = static_cast<int>(b - a);
            if (len < static_cast<int>(0.08 * c.fs)) continue;
            const double fi = d5m::measureF0(c.samples.data() + a, len, c.fs);
            const double fo = d5m::measureF0(render(c, false).mono.data() + a, len, c.fs);
            if (fi <= 0.0 || fo <= 0.0) continue;
            // D6: score the LPC envelope centroid on a centered 60 ms frame,
            // not the f0-harmonic grid. On high-F0 real voice the grid can
            // hold zero harmonics (returns 0 vs a real centroid) and band-edge
            // membership flips move the centroid by ~f0 — measurement noise,
            // not engine damage. The LPC envelope is F0-independent, so the
            // +-60 Hz gate measures actual formant movement.
            const auto [fa, fl] = d5m::lpcFrame(a, len, c.fs);
            const double c1i = d5m::lpcBandCentroid(c.samples.data() + fa, fl,
                                                    c.fs, 500.0, 900.0);
            const double c2i = d5m::lpcBandCentroid(c.samples.data() + fa, fl,
                                                    c.fs, 1000.0, 1500.0);
            const double c1o = d5m::lpcBandCentroid(
                render(c, false).mono.data() + fa, fl, c.fs, 500.0, 900.0);
            const double c2o = d5m::lpcBandCentroid(
                render(c, false).mono.data() + fa, fl, c.fs, 1000.0, 1500.0);
            ++scored;
            worstF1 = std::max(worstF1, std::abs(c1o - c1i));
            worstF2 = std::max(worstF2, std::abs(c2o - c2i));
        }
    }
    if (scored == 0) {
        warn("R2: no sustained segments scored");
        return;
    }
    std::printf("  scored %zu sustained windows; worst |dc| F1 %.0f Hz, F2 %.0f Hz "
                "(gate +-60)\n",
                scored, worstF1, worstF2);
    check(worstF1 <= 60.0, "R2: F1 centroid within +-60 Hz of input on real voice");
    check(worstF2 <= 60.0, "R2: F2 centroid within +-60 Hz of input on real voice");
}

// --- R3: envelope pumping ---------------------------------------------------------------

void batteryR3(const Loaded& L) {
    std::printf("R3. envelope pumping on >=0.5 s sustained segments\n");
    if (!L.corpusPresent) {
        skip("R3", L.clips.size());
        return;
    }
    double worstStdExcess = 0.0, worstP95Excess = 0.0;
    std::size_t scored = 0;
    std::string worstClip;
    for (const d5corpus::Clip& c : L.clips) {
        for (const d5corpus::NoteSeg& s : c.notes) {
            if (s.tEnd - s.tStart < 0.5) continue;
            const bool longTone = c.meta.context == "long_tones";
            if (!longTone) continue;
            std::size_t a = static_cast<std::size_t>((s.tStart + 0.06) * c.fs);
            std::size_t b = static_cast<std::size_t>((s.tEnd - 0.06) * c.fs);
            b = std::min(b, render(c, false).rendered);
            if (b <= a || b - a < static_cast<std::size_t>(0.3 * c.fs)) continue;
            const d5m::PumpDepth pin =
                d5m::pumpDepth(c.samples.data() + a, b - a, c.fs);
            const d5m::PumpDepth pout =
                d5m::pumpDepth(render(c, false).mono.data() + a, b - a, c.fs);
            const double exStd = pout.stdDb - pin.stdDb;
            const double exP95 = pout.p95DeltaDb - pin.p95DeltaDb;
            ++scored;
            if (exStd > worstStdExcess) {
                worstStdExcess = exStd;
                worstClip = c.meta.clipId;
            }
            worstP95Excess = std::max(worstP95Excess, exP95);
        }
    }
    if (scored == 0) {
        warn("R3: no >=0.5 s long_tones segments scored");
        return;
    }
    std::printf("  scored %zu long-tone segments; worst input-corrected pump "
                "excess: std %+.2f dB (%s), p95 dEnv %+.2f dB\n",
                scored, worstStdExcess, worstClip.c_str(), worstP95Excess);
    if (worstStdExcess > 3.0) {
        std::printf("  *** D6 TRIGGER: pump excess %.2f dB > 3 dB on real sustained "
                    "vowels - orchestrator must dispatch the root-cause "
                    "investigation (card D6) ***\n",
                    worstStdExcess);
    } else {
        std::printf("  D6 trigger rule: excess <= 3 dB - no root-cause dispatch\n");
    }
}

// --- R4: artifacts ----------------------------------------------------------------------

void batteryR4(const Loaded& L) {
    std::printf("R4. artifacts at annotated boundaries + sustained glitch check\n");
    if (!L.corpusPresent) {
        skip("R4", L.clips.size());
        return;
    }
    double worstAmpl = 0.0;
    std::size_t boundaries = 0;
    int ampFail = 0;
    for (const d5corpus::Clip& c : L.clips) {
        const d5m::Rendered& r = render(c, false);
        for (const d5corpus::NoteSeg& s : c.notes) {
            for (const double tSec : {s.tStart, s.tEnd}) {
                const std::size_t t = static_cast<std::size_t>(tSec * c.fs);
                if (t < 2000 || t + 2200 >= r.rendered) continue;
                const std::size_t a = t - 2000;
                const std::size_t bEnd = std::min(r.rendered, t + 2200);
                const double inB = d5m::maxStep(c.samples, a, bEnd);
                const double nN = d5m::maxStep(r.mono, a, bEnd);
                ++boundaries;
                worstAmpl = std::max(worstAmpl, nN / std::max(1e-9, inB));
                if (nN > 1.25 * inB + 0.01) {
                    ++ampFail;
                    std::printf("  boundary %s @ %.2f s: out step %.4f vs in %.4f "
                                "AMPLIFIED\n",
                                c.meta.clipId.c_str(), tSec, nN, inB);
                }
            }
        }
        // Sustained glitch-free: interior of the longest segment.
        const d5corpus::NoteSeg* longest = nullptr;
        for (const d5corpus::NoteSeg& s : c.notes)
            if (!longest || s.tEnd - s.tStart > longest->tEnd - longest->tStart)
                longest = &s;
        if (longest && longest->tEnd - longest->tStart >= 0.6) {
            const std::size_t a =
                static_cast<std::size_t>((longest->tStart + 0.15) * c.fs);
            const std::size_t b =
                std::min(r.rendered, static_cast<std::size_t>((longest->tEnd - 0.15) * c.fs));
            const double inSl = d5m::maxStep(c.samples, a, b);
            const double outSl = d5m::maxStep(r.mono, a, b);
            const double bound = std::max(0.02 * 0.3, 2.0 * inSl);
            if (outSl > bound) {
                std::printf("  sustained %s: out slew %.4f > bound %.4f "
                            "(in %.4f)\n",
                            c.meta.clipId.c_str(), outSl, bound, inSl);
                ++ampFail;
            }
        }
    }
    std::printf("  checked %zu note boundaries; worst step amplification %.2fx\n",
                boundaries, worstAmpl);
    check(ampFail == 0, "R4: no click amplification at boundaries / sustained glitch");
}

// --- R5: DSP delay --------------------------------------------------------------------------

void batteryR5(const Loaded& L) {
    std::printf("R5. impulse-centroid DSP delay per corpus sample rate\n");
    if (!L.corpusPresent) {
        skip("R5", L.clips.size());
        return;
    }
    bool seen48 = false, seen44 = false;
    for (const d5corpus::Clip& c : L.clips) {
        if (c.fs == 48000) seen48 = true;
        if (c.fs == 44100) seen44 = true;
    }
    for (const int sr : {48000, 44100}) {
        if ((sr == 48000 && !seen48) || (sr == 44100 && !seen44)) continue;
        const double d220 = d5m::impulseCentroidDelaySamp(sr, 220.0, 1.15f);
        const double ms = 1000.0 * d220 / sr;
        const double budget = sr / 220.0 + 1.0 + 96.0;
        const bool klass = ms >= 2.27 && ms <= 4.17;
        std::printf("  sr %d: impulse-centroid delay %.0f samp = %.2f ms "
                    "(D4 class 2.27-4.17 ms: %s)\n",
                    sr, d220, ms, klass ? "in class" : "OUT OF CLASS");
        check(d220 >= 0.0 && d220 <= budget,
              "R5: delay inside the D3 P+1 budget at this rate");
        check(klass, "R5: delay inside the D4 2.27-4.17 ms class");
    }
}

}  // namespace

int main() {
    std::printf("D5 real-voice validation batteries R1-R5\n");
    std::printf("corpus dir: %s\n", kCorpus.c_str());
    std::printf("chain: live PitchCorrector, amount 1.0, retune 35 ms, C major, "
                "reverb 0, chunk %d\n\n",
                d5m::kChunk);

    const Loaded L = loadAll();
    std::printf("\n");
    if (!L.corpusPresent)
        std::printf("*** corpus absent - all real-voice batteries SKIP "
                    "(not a failure; see docs/corpus/README.md) ***\n\n");

    batteryR1(L);
    std::printf("\n");
    batteryR2(L);
    std::printf("\n");
    batteryR3(L);
    std::printf("\n");
    batteryR4(L);
    std::printf("\n");
    batteryR5(L);

    std::printf("\n=== D5 REAL-VOICE SUMMARY ===\n");
    if (!L.corpusPresent) {
        std::printf("R1 output-F0:      SKIP - corpus absent (0 clips)\n");
        std::printf("R2 formants:       SKIP - corpus absent (0 clips)\n");
        std::printf("R3 pumping:        SKIP - corpus absent (0 clips)\n");
        std::printf("R4 artifacts:      SKIP - corpus absent (0 clips)\n");
        std::printf("R5 DSP delay:      SKIP - corpus absent (0 clips)\n");
        std::printf("blind listening:   BLOCKED_HUMAN (pack target: d5_listening_pack)\n");
    }
    if (gFailures) {
        std::printf("D5 REAL-VOICE: %d FAILURES, %d warnings\n", gFailures, gWarnings);
        return 1;
    }
    std::printf("D5 REAL-VOICE: %s (%d warnings)\n",
                L.corpusPresent ? "COMPLETE" : "ALL SKIP - corpus absent", gWarnings);
    return 0;
}

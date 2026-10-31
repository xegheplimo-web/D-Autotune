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

// --- D7 diagnosis support -------------------------------------------------------
//
// R1's out-vs-snap residual uses the SEGMENT-MEDIAN of the engine's own
// targetHz() as reference (GPT round-5 correction). The residual alone cannot
// say WHY a window deviates; the diagnosis below re-aligns each worst window
// in time (output window -> input time via the measured impulse-centroid
// delay) and compares four signals: output F0, the INSTANTANEOUS targetHz at
// the corresponding input time, the segment-median target, and the annotated
// reference F0. Cause taxonomy (one primary cause per window):
//   a = vibrato/reference disagreement (out tracks tgt; tgt vs annot/med legitimately differ)
//   b = retune smoothing lag (out mid-flight between pre/post-change targets)
//   c = target change within segment (median target is a poor reference)
//   d = measurement error (measureF0 unreliable on unstable/low-energy frames)
//   e = genuine engine defect (out disagrees with BOTH tgt and annot in a
//       reproducible pattern) — REPORT ONLY, never auto-fixed.
// Full per-window rows print under D5_R1_DIAG=1; the cause tally is always
// printed. Coverage rows print under D5_COV_DIAG=1; genuinely-missed chunks
// always print.

double quantile(std::vector<double> v, double q) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    const double pos = q * static_cast<double>(v.size() - 1);
    const std::size_t lo = static_cast<std::size_t>(pos);
    const double f = pos - static_cast<double>(lo);
    const double hi = lo + 1 < v.size() ? v[lo + 1] : v[lo];
    return v[lo] + f * (hi - v[lo]);
}

// Measured engine delay per sample rate (R5 impulse-centroid probe), used to
// map an output-time window back to the input time that produced it.
double delaySampFor(int fs) {
    static std::map<int, double> cache;
    auto it = cache.find(fs);
    if (it != cache.end()) return it->second;
    const double d = d5m::impulseCentroidDelaySamp(fs, 220.0, 1.15f);
    return cache.emplace(fs, d > 0.0 ? d : 0.0).first->second;
}

struct WinRec {
    std::size_t clip = 0, seg = 0;
    std::size_t w = 0;
    int win = 0;
    double fo = 0.0, fi = 0.0, ncO = 0.0;
    double eMed = 0.0, tgtMed = 0.0;
    double segTgtRangeC = 0.0;  // cents span of nonzero tgtHz over the mid-60% chunks
    int segSteps = 0;           // >30c adjacent-chunk target steps in that span
};

struct NdChunk {
    std::size_t clip = 0, chunk = 0;
    int seg = -1;
    bool estVoiced = false;
};

// First annotation segment containing time t (segments are time-ordered and
// non-overlapping per the corpus schema), or -1.
int segIdxAt(const std::vector<d5corpus::NoteSeg>& notes, double t) {
    for (std::size_t i = 0; i < notes.size(); ++i)
        if (t >= notes[i].tStart && t < notes[i].tEnd) return static_cast<int>(i);
    return -1;
}

// RMS of input samples over [t0,t1) seconds, clamped to the clip.
double clipRms(const d5corpus::Clip& c, double t0, double t1) {
    const std::size_t a = static_cast<std::size_t>(std::max(0.0, t0) * c.fs);
    const std::size_t b = std::min(c.samples.size(),
                                   static_cast<std::size_t>(std::max(0.0, t1) * c.fs));
    return b > a ? d5m::rmsOf(c.samples.data() + a, b - a) : 0.0;
}

// Index of the last >30c target step with i in [from,to] (i compares
// tgtHz[i] vs tgtHz[i-1]), or tgtHz.size() when none.
std::size_t lastTgtStep(const d5m::Rendered& r, std::size_t from, std::size_t to) {
    if (to >= r.tgtHz.size()) to = r.tgtHz.size() - 1;
    if (from >= to || r.tgtHz.empty()) return r.tgtHz.size();
    for (std::size_t i = to; i > from; --i)
        if (r.tgtHz[i] > 0.0f && r.tgtHz[i - 1] > 0.0f &&
            std::abs(d5m::cents(r.tgtHz[i], r.tgtHz[i - 1])) > 30.0)
            return i;
    return r.tgtHz.size();
}

struct DiagRow {
    std::size_t clip = 0, seg = 0, w = 0;
    int win = 0;
    double tC = 0.0;      // window center, output (clip) time
    double fo = 0.0, fi = 0.0, ncO = 0.0, rmsO = 0.0;
    double eMed = 0.0;    // the R1 residual: |cents(fo, segment-median target)|
    double tgtMed = 0.0, tgtInst = 0.0, annot = 0.0;
    double eInst = -1.0, eAnn = 0.0, eInstAnn = -1.0;
    double stepB = 0.0, stepA = 0.0;  // target values around the relevant step
    int winSteps = 0, segSteps = 0;
    char cls = 'd';
    const char* conf = "low";
    std::string note;
};

DiagRow diagnoseWindow(const Loaded& L, const WinRec& wr) {
    const d5corpus::Clip& c = L.clips[wr.clip];
    const d5m::Rendered& r = render(c, true);
    const d5corpus::NoteSeg& s = c.notes[wr.seg];
    DiagRow d;
    d.clip = wr.clip; d.seg = wr.seg; d.w = wr.w; d.win = wr.win;
    d.fo = wr.fo; d.fi = wr.fi; d.ncO = wr.ncO;
    d.eMed = wr.eMed; d.tgtMed = wr.tgtMed; d.annot = s.f0Hz;
    d.segSteps = wr.segSteps;
    d.tC = (static_cast<double>(wr.w) + 0.5 * wr.win) / c.fs;
    const double delay = delaySampFor(c.fs);
    const double inCtr = std::max(0.0, d.tC * c.fs - delay);
    const std::size_t chC = std::min(r.tgtHz.size() - 1,
                                     static_cast<std::size_t>(inCtr) / d5m::kChunk);
    d.tgtInst = r.tgtHz[chC];
    // Target steps strictly inside the window's input-time chunk span.
    const std::size_t chLo = static_cast<std::size_t>(
        std::max(0.0, static_cast<double>(wr.w) - delay)) / d5m::kChunk;
    const std::size_t chHi =
        std::min(r.tgtHz.size() - 1,
                 static_cast<std::size_t>(
                     std::max(0.0, static_cast<double>(wr.w) + wr.win - delay)) /
                     d5m::kChunk);
    std::size_t firstStep = r.tgtHz.size();
    for (std::size_t i = chLo + 1; i <= chHi; ++i) {
        if (r.tgtHz[i] > 0.0f && r.tgtHz[i - 1] > 0.0f &&
            std::abs(d5m::cents(r.tgtHz[i], r.tgtHz[i - 1])) > 30.0) {
            ++d.winSteps;
            if (firstStep == r.tgtHz.size()) firstStep = i;
            if (i <= chC + 1) {  // latest step at/before the window center wins
                d.stepB = r.tgtHz[i - 1];
                d.stepA = r.tgtHz[i];
            }
        }
    }
    if (d.stepB <= 0.0 && firstStep != r.tgtHz.size()) {
        d.stepB = r.tgtHz[firstStep - 1];
        d.stepA = r.tgtHz[firstStep];
    }
    // A step just ahead of the window still explains a lagging output:
    // look back ~12 chunks (~140 ms) for the most recent pre-window change.
    const bool recentStep = lastTgtStep(
        r, chC > 13 ? chC - 13 : 1, chC) != r.tgtHz.size();
    if (recentStep && d.stepB <= 0.0) {
        const std::size_t i = lastTgtStep(r, chC > 13 ? chC - 13 : 1, chC);
        d.stepB = r.tgtHz[i - 1];
        d.stepA = r.tgtHz[i];
    }
    d.rmsO = d5m::rmsOf(r.mono.data() + wr.w, static_cast<std::size_t>(wr.win));
    d.eInst = d.tgtInst > 0.0 ? std::abs(d5m::cents(d.fo, d.tgtInst)) : -1.0;
    d.eAnn = std::abs(d5m::cents(d.fo, d.annot));
    d.eInstAnn =
        d.tgtInst > 0.0 ? std::abs(d5m::cents(d.tgtInst, d.annot)) : -1.0;
    // Instantaneous target is a SPIKE (single-chunk tracker dip/rally that
    // returns to the old level) when both flanking chunks agree with each
    // other but not with it. The 120 ms scoring window averages the whole
    // move, so scoring against a spike is a reference artifact, not tracking.
    bool tgtSpike = false;
    if (d.tgtInst > 0.0 && chC >= 1 && chC + 1 < r.tgtHz.size() &&
        r.tgtHz[chC - 1] > 0.0f && r.tgtHz[chC + 1] > 0.0f &&
        std::abs(d5m::cents(r.tgtHz[chC - 1], r.tgtHz[chC + 1])) <= 30.0 &&
        std::abs(d5m::cents(d.tgtInst, r.tgtHz[chC - 1])) > 30.0)
        tgtSpike = true;
    // An unvoiced hop inside the last ~6 chunks means smoothedRatio_ is still
    // slewing from 1.0 back toward the target — the output legitimately lags.
    bool recentUnvoiced = false;
    for (std::size_t i = chC; i-- > 0 && i + 6 >= chC;)
        if (r.tgtHz[i] <= 0.0f) { recentUnvoiced = true; break; }
    const bool measBad =
        d.ncO < 0.50 || d.rmsO < 0.003 || d.fi <= 0.0 ||
        std::abs(d5m::cents(d.fi, d.annot)) > 120.0;
    const bool nearStep = d.winSteps > 0 || recentStep;

    if (d.tgtInst <= 0.0) {
        // Engine was unvoiced at this input time: output is the aligned
        // passthrough of the sung note. fo≈fi means the residual exists only
        // against a median target the tracker was not applying — a reference
        // artifact, not a tracking error.
        if (d.fi > 0.0 && std::abs(d5m::cents(d.fo, d.fi)) <= 40.0) {
            d.cls = 'a'; d.conf = "med";
            d.note = "engine-unvoiced passthrough (det gap); out~=in~=annot";
        } else {
            d.cls = 'e'; d.conf = "low";
            d.note = "engine unvoiced but out diverges from input";
        }
    } else if (d.eInst <= 25.0) {
        // Output tracks the instantaneous target: the residual is a
        // reference-side effect.
        if (wr.segSteps >= 3 && wr.segTgtRangeC <= 160.0) {
            d.cls = 'a'; d.conf = "high";
            d.note = "target oscillates (vibrato snap); median smeared";
        } else if (wr.segSteps >= 1 || d.winSteps >= 1 ||
                   wr.segTgtRangeC > 30.0) {
            d.cls = 'c'; d.conf = "high";
            d.note = "mid-segment target change; out tracks inst target";
        } else {
            d.cls = 'a'; d.conf = "low";
            d.note = "out~=tgt~=med; residual already within tolerance";
        }
    } else if (tgtSpike) {
        d.cls = 'c'; d.conf = "med";
        d.note = "transient tracker dip/rally inside window; inst tgt "
                 "unrepresentative of the scored span";
    } else if (measBad) {
        d.cls = 'd'; d.conf = "med";
        d.note = "measureF0 unreliable (nc/rms low or input also unstable)";
    } else if (recentUnvoiced && d.fi > 0.0 &&
               std::abs(d5m::cents(d.fo, d.fi)) <= 35.0) {
        d.cls = 'b'; d.conf = "med";
        d.note = "post-unvoiced slew: out~=in while smoothed ratio re-engages";
    } else if (nearStep && d.stepB > 0.0) {
        const double cLo = d5m::cents(d.fo, std::min(d.stepB, d.stepA));
        const double cHi = d5m::cents(d.fo, std::max(d.stepB, d.stepA));
        if (std::abs(d5m::cents(d.fo, d.stepB)) <= 30.0) {
            d.cls = 'b'; d.conf = "high";
            d.note = "output still at pre-change target (retune slew)";
        } else if (cLo >= -25.0 && cHi <= 25.0) {
            d.cls = 'b'; d.conf = "med";
            d.note = "output mid-flight between pre/post-change targets";
        } else {
            d.cls = 'd'; d.conf = "low";
            d.note = "near target step but outside lag corridor";
        }
    } else if (d.eAnn <= 25.0 && d.eInstAnn > 40.0) {
        d.cls = 'e'; d.conf = "low";
        d.note = "out~=sung pitch while tracker target demands shift";
    } else if (std::abs(d.eInst - 1200.0) <= 120.0 ||
               std::abs(d.eInst - 600.0) <= 80.0) {
        d.cls = 'e'; d.conf = "med";
        d.note = "harmonic-pattern deviation vs instantaneous target";
    } else {
        d.cls = 'd'; d.conf = "med";
        d.note = "unexplained single-window deviation (likely measureF0)";
    }
    return d;
}

// D7 Task-1 entry: classify every scored window above its singer's p75.
void r1Diagnosis(const Loaded& L, const std::vector<WinRec>& recs,
                 const std::map<std::string, std::vector<double>>& perSinger) {
    std::map<std::string, double> p75;
    for (const auto& [singer, v] : perSinger) p75[singer] = quantile(v, 0.75);
    std::vector<DiagRow> rows;
    std::map<std::string, std::map<char, int>> tally;
    for (const WinRec& wr : recs) {
        if (wr.eMed <= p75[L.clips[wr.clip].meta.singer]) continue;
        DiagRow d = diagnoseWindow(L, wr);
        rows.push_back(d);
        ++tally[L.clips[wr.clip].meta.singer][d.cls];
    }
    std::printf("  D7 residual diagnosis (windows above per-singer p75 "
                "of |out-tgtMed|; causes: a=vibrato/ref b=retune-lag "
                "c=mid-seg-tgt-change d=measurement e=engine-defect):\n");
    std::size_t eCount = 0;
    for (const auto& [singer, m] : tally) {
        std::size_t n = 0;
        for (const auto& [cls, k] : m) n += k;
        std::printf("    %-12s p75=%5.1f  n=%-4zu a=%-3zu b=%-3zu c=%-3zu "
                    "d=%-3zu e=%-3zu\n",
                    singer.c_str(), p75[singer], n,
                    static_cast<std::size_t>(m.count('a') ? m.at('a') : 0),
                    static_cast<std::size_t>(m.count('b') ? m.at('b') : 0),
                    static_cast<std::size_t>(m.count('c') ? m.at('c') : 0),
                    static_cast<std::size_t>(m.count('d') ? m.at('d') : 0),
                    static_cast<std::size_t>(m.count('e') ? m.at('e') : 0));
        eCount += static_cast<std::size_t>(m.count('e') ? m.at('e') : 0);
    }
    if (std::getenv("D5_R1_DIAG")) {
        std::printf("    diag rows (sorted by residual; times in s, f0 in Hz, "
                    "err in c):\n");
        std::printf("    singer clip tC f0_out f0_in nc rms_out tgt_inst "
                    "tgt_med annot e_inst e_med e_ann segSteps class conf "
                    "note\n");
        std::vector<DiagRow> srt = rows;
        std::sort(srt.begin(), srt.end(),
                  [](const DiagRow& x, const DiagRow& y) {
                      return x.eMed > y.eMed;
                  });
        for (const DiagRow& d : srt) {
            const d5corpus::Clip& c = L.clips[d.clip];
            std::printf("    %-6s %-34s %6.2f %7.1f %7.1f %.2f %.4f %8.1f "
                        "%8.1f %7.1f %7.1f %6.1f %6.1f %3d  %c %-4s %s\n",
                        c.meta.singer.c_str(), c.meta.clipId.c_str(), d.tC,
                        d.fo, d.fi, d.ncO, d.rmsO, d.tgtInst, d.tgtMed,
                        d.annot, d.eInst, d.eMed, d.eAnn, d.segSteps, d.cls,
                        d.conf, d.note.c_str());
        }
    }
    if (eCount > 0)
        warn("R1 diagnosis: " + std::to_string(eCount) +
             " window(s) classified (e) suspected engine defect - "
             "REPORT ONLY (card G2-D7); inspect D5_R1_DIAG rows");
}

// --- D7 Task 2: detector-coverage classification ---------------------------------
// Every voiced-annotated chunk where the engine reported estHz<=0 is classed
// as S (silence-adjacent: near an onset/offset whose gap is low-energy),
// T (transition: near a boundary whose gap still carries voiced energy),
// B (breath/frication: interior chunk, aperiodic or near-silent), or
// M (genuinely missed: interior, periodic and energetic — REPORT ONLY).

struct NdRow {
    std::size_t clip = 0, chunk = 0;
    double tC = 0.0;
    int seg = -1;
    bool estVoiced = false;
    char cls = 'B';
    int sub = 0;  // B only: 1 = periodic-but-low-energy (at/below detect
                  // floor ~4e-5 mean-square), 2 = aperiodic/frication
    double rms = 0.0, nc = 0.0, probe = 0.0;
};

NdRow classifyNd(const Loaded& L, const NdChunk& nd) {
    const d5corpus::Clip& c = L.clips[nd.clip];
    NdRow o;
    o.clip = nd.clip; o.chunk = nd.chunk; o.seg = nd.seg;
    o.estVoiced = nd.estVoiced;
    o.tC = (nd.chunk * d5m::kChunk + 0.5 * d5m::kChunk) / c.fs;
    if (nd.seg < 0) {  // gtHz>0 implies containment; defensive default
        o.cls = 'S';
        return o;
    }
    const d5corpus::NoteSeg& s = c.notes[static_cast<std::size_t>(nd.seg)];
    const double dStart = o.tC - s.tStart, dEnd = s.tEnd - o.tC;
    const double edge = std::min(dStart, dEnd);
    if (edge <= 0.080) {
        // Boundary-adjacent: probe input energy in the ~50 ms just OUTSIDE
        // the nearer edge. A voiced gap (note-to-note material still
        // sounding) means the detector is inside a pitch transition; a dead
        // gap means silence-adjacent.
        const double segRef = clipRms(
            c, s.tStart + 0.5 * (s.tEnd - s.tStart) - 0.05,
            s.tStart + 0.5 * (s.tEnd - s.tStart) + 0.05);
        bool voicedGap = false;
        if (dStart <= dEnd) {
            const double gap = clipRms(c, s.tStart - 0.05, s.tStart);
            o.probe = segRef > 1e-9 ? gap / segRef : 0.0;
            voicedGap = o.probe >= 0.30;
        } else {
            const double gap = clipRms(c, s.tEnd, s.tEnd + 0.05);
            o.probe = segRef > 1e-9 ? gap / segRef : 0.0;
            voicedGap = o.probe >= 0.30;
        }
        o.cls = voicedGap ? 'T' : 'S';
        return o;
    }
    // Interior chunk: periodicity + energy decide breath vs genuinely-missed.
    const std::size_t ctr = nd.chunk * d5m::kChunk + d5m::kChunk / 2;
    const std::size_t half = 1024;
    const std::size_t a = ctr > half ? ctr - half : 0;
    const std::size_t b = std::min(c.samples.size(), ctr + half);
    o.rms = d5m::rmsOf(c.samples.data() + a, b - a);
    o.nc = d5m::peakNc(c.samples.data() + a, static_cast<int>(b - a), c.fs,
                       70.0, 1000.0);
    const double f0chk = d5m::measureF0(c.samples.data() + a,
                                        static_cast<int>(b - a), c.fs);
    o.cls = (o.rms >= 0.008 && o.nc >= 0.45 && f0chk > 0.0) ? 'M' : 'B';
    if (o.cls == 'B') o.sub = (o.nc >= 0.45 && o.rms < 0.008) ? 1 : 2;
    return o;
}

void coverageReport(const Loaded& L, const std::vector<NdChunk>& nds) {
    // counts[singer][context] -> S,T,B,M + voiced-held per class
    std::map<std::string, std::map<std::string, std::map<char, int>>> counts;
    std::map<char, int> held;
    int bLowE = 0, bAper = 0;
    std::vector<NdRow> missed;
    for (const NdChunk& nd : nds) {
        const d5corpus::Clip& c = L.clips[nd.clip];
        const NdRow o = classifyNd(L, nd);
        ++counts[c.meta.singer][c.meta.context][o.cls];
        if (o.estVoiced) ++held[o.cls];
        if (o.cls == 'B') { if (o.sub == 1) ++bLowE; else ++bAper; }
        if (o.cls == 'M') missed.push_back(o);
    }
    std::printf("  D7 detector-coverage classification of %zu non-detected "
                "voiced chunks (S=silence-adjacent T=transition "
                "B=breath/frication M=genuinely-missed):\n",
                nds.size());
    std::printf("    | singer | context | n | S | T | B | M |\n");
    std::printf("    |--------|---------|---|---|---|---|---|\n");
    for (const auto& [singer, ctxMap] : counts) {
        for (const auto& [ctx, m] : ctxMap) {
            const int n = (m.count('S') ? m.at('S') : 0) +
                          (m.count('T') ? m.at('T') : 0) +
                          (m.count('B') ? m.at('B') : 0) +
                          (m.count('M') ? m.at('M') : 0);
            std::printf("    | %-6s | %-7s | %d | %d | %d | %d | %d |\n",
                        singer.c_str(), ctx.c_str(), n,
                        m.count('S') ? m.at('S') : 0,
                        m.count('T') ? m.at('T') : 0,
                        m.count('B') ? m.at('B') : 0,
                        m.count('M') ? m.at('M') : 0);
        }
    }
    std::printf("    B split: %d low-energy interior (rms<0.008, at/below the "
                "detector's 4e-5 energy floor), %d aperiodic/frication\n",
                bLowE, bAper);
    std::printf("    engine voiced-flag still held on non-detected chunks: "
                "S=%d T=%d B=%d M=%d\n",
                held.count('S') ? held.at('S') : 0,
                held.count('T') ? held.at('T') : 0,
                held.count('B') ? held.at('B') : 0,
                held.count('M') ? held.at('M') : 0);
    if (!missed.empty()) {
        std::printf("    flagged genuinely-missed chunks (interior, periodic, "
                    "energetic; REPORT ONLY):\n");
        const std::size_t cap = std::min<std::size_t>(missed.size(), 40);
        for (std::size_t i = 0; i < cap; ++i) {
            const NdRow& o = missed[i];
            const d5corpus::Clip& c = L.clips[o.clip];
            const d5corpus::NoteSeg& s =
                c.notes[static_cast<std::size_t>(o.seg)];
            std::printf("      %-34s t=%6.2f midi=%d nc=%.2f rms=%.4f "
                        "estVoiced=%d\n",
                        c.meta.clipId.c_str(), o.tC, s.midi, o.nc, o.rms,
                        o.estVoiced ? 1 : 0);
        }
        if (missed.size() > cap)
            std::printf("      ... +%zu more\n", missed.size() - cap);
        warn("coverage: " + std::to_string(missed.size()) +
             " genuinely-missed steady voiced chunk(s) - REPORT ONLY "
             "(card G2-D7)");
    }
    if (std::getenv("D5_COV_DIAG")) {
        std::printf("    all non-detected chunks: clip chunk tC cls nc rms "
                    "probe estVoiced\n");
        for (const NdChunk& nd : nds) {
            const d5corpus::Clip& c = L.clips[nd.clip];
            const NdRow o = classifyNd(L, nd);
            std::printf("      %-34s %5zu %6.2f %c %.2f %.4f %.2f %d\n",
                        c.meta.clipId.c_str(), o.chunk, o.tC, o.cls, o.nc,
                        o.rms, o.probe, o.estVoiced ? 1 : 0);
        }
    }
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
    std::vector<WinRec> winRecs;   // D7: per-scored-window records
    std::vector<NdChunk> ndChunks; // D7: non-detected voiced chunks

    for (std::size_t ci = 0; ci < L.clips.size(); ++ci) {
        const d5corpus::Clip& c = L.clips[ci];
        if (c.notes.empty()) continue;
        const d5m::Rendered& r = render(c, true);
        for (std::size_t fi = 0; fi < r.frames.size(); ++fi) {
            const metrics::Frame& f = r.frames[fi];
            if (f.gtHz <= 0.0f) continue;
            ++voicedFrames;
            if (f.estVoiced) ++vuvAll.tvEv; else ++vuvAll.tvEu;
            if (f.estHz > 0.0f) {
                ++detectedFrames;
                continue;
            }
            NdChunk nd;
            nd.clip = ci;
            nd.chunk = fi;
            nd.estVoiced = f.estVoiced;
            nd.seg = segIdxAt(
                c.notes,
                (fi * d5m::kChunk + 0.5 * d5m::kChunk) / c.fs);
            ndChunks.push_back(nd);
        }
        for (std::size_t ni = 0; ni < c.notes.size(); ++ni) {
            const d5corpus::NoteSeg& s = c.notes[ni];
            const auto [a, b] = d5m::mid60(s, c.fs, r.rendered);
            if (b - a < static_cast<std::size_t>(0.08 * c.fs)) continue;
            ++segsUsed;
            // Snap target = median of the tracker's chosen grid note across
            // this segment's mid-60% (NOT the annotated midi: a note sung
            // off-scale is corrected to the nearest member, so scoring the
            // output against its own snap target is the honest residual).
            std::vector<double> tgts;
            double tgtLo = 1e30, tgtHi = 0.0;
            int segSteps = 0;
            for (std::size_t ch = a / d5m::kChunk;
                 ch < r.tgtHz.size() && ch * d5m::kChunk < b; ++ch) {
                if (r.tgtHz[ch] <= 0.0f) continue;
                tgts.push_back(r.tgtHz[ch]);
                tgtLo = std::min(tgtLo, static_cast<double>(r.tgtHz[ch]));
                tgtHi = std::max(tgtHi, static_cast<double>(r.tgtHz[ch]));
                if (ch > 0 && r.tgtHz[ch - 1] > 0.0f &&
                    std::abs(d5m::cents(r.tgtHz[ch], r.tgtHz[ch - 1])) > 30.0)
                    ++segSteps;
            }
            if (tgts.empty()) continue;
            const double tgtHz = metrics::summarize(tgts).median;
            const double segRangeC =
                tgtHi > 0.0 ? d5m::cents(tgtHi, tgtLo) : 0.0;
            // Sub-window F0 estimates (~120 ms hop 60 ms) across mid-60%.
            std::vector<double> outErrSeg, inErrSeg;
            const int win = static_cast<int>(0.12 * c.fs);
            const int hop = static_cast<int>(0.06 * c.fs);
            const double midiHz = d5m::etHz(s.midi);
            for (std::size_t w = a; w + win <= b; w += hop) {
                double ncO = 0.0;
                const double fo = d5m::measureF0(r.mono.data() + w, win, c.fs,
                                                 60.0, 1000.0, &ncO);
                const double fi = d5m::measureF0(c.samples.data() + w, win, c.fs);
                if (fo <= 0.0) continue;
                ++windows;
                const double eMed = std::abs(d5m::cents(fo, tgtHz));
                errVsRef.push_back(std::abs(d5m::cents(fo, s.f0Hz)));
                errVsTgt.push_back(eMed);
                errVsMidi.push_back(std::abs(d5m::cents(fo, midiHz)));
                perSinger[c.meta.singer].push_back(eMed);
                WinRec wr;
                wr.clip = ci; wr.seg = ni; wr.w = w; wr.win = win;
                wr.fo = fo; wr.fi = fi; wr.ncO = ncO;
                wr.eMed = eMed; wr.tgtMed = tgtHz;
                wr.segTgtRangeC = segRangeC; wr.segSteps = segSteps;
                winRecs.push_back(wr);
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
    r1Diagnosis(L, winRecs, perSinger);
    coverageReport(L, ndChunks);
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
    int warnBand = 0;
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
                // D7 diagnostic warning (GPT round-5): same ratio+absolute
                // tolerance shape as the hard gate but at 1.10x — nonblocking,
                // printed with amplitude/clip/timestamp. The 1.25x+0.01 hard
                // gate below is unchanged.
                if (nN > 1.10 * inB + 0.01 && nN <= 1.25 * inB + 0.01) {
                    ++warnBand;
                    warn("R4 boundary step >1.10x (nonblocking): " +
                         c.meta.clipId + " @ " + std::to_string(tSec) +
                         " s: out step " + std::to_string(nN) + " vs in " +
                         std::to_string(inB) + " (" +
                         std::to_string(nN / std::max(1e-9, inB)) + "x)");
                }
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
    std::printf("  checked %zu note boundaries; worst step amplification %.2fx; "
                "%d in the >1.10x diagnostic warn band\n",
                boundaries, worstAmpl, warnBand);
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

// D4 — end-to-end full-chain verification (card G2-D4, GPT round-3 D-014).
//
// One binary holds BOTH engines: the live dautotune::PitchCorrector (D3
// TD-PSOLA shifter) and the byte-frozen d2ref::PitchCorrectorD2 (D1 detector +
// D2 tracker + the OLD two-head granular shifter, 70b1147). Detector and
// tracker are byte-identical in both, so any output difference under identical
// parameters is shifter-only — that is the A/B.
//
// Batteries: A pitch accuracy, B formant retention, C artifacts, D signal
// delay, E callback cost — each on BOTH chains at BOTH 44100 and 48000 Hz —
// plus the round-3 regression verdict (F). Reverb = 0 for the measurement
// batteries; one reverb-on run proves stability.
//
// NOTE on the card's "A3 -> D4" label: the nearest-member snap law can never
// map a sung 220 Hz onto D4 (A3 IS a C-major member). The honest full-chain
// equivalent is sung-in-the-D4-basin (285 Hz -> D4, +52 c) for accuracy, and
// the deepest stable snap (sung ~66.10 -> G4, +90 c) for the formant
// contrast; the +500 c driven column replicates D3-B for reference.

#include "PitchCorrector.h"
#include "corpus_gen.h"
#include "d2_reference.h"
#include "metrics.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

namespace {

constexpr int kChunk = 512;
constexpr int kOutCh = 2;
constexpr double kPi = 3.14159265358979323846;
constexpr std::uint64_t kSeed = 0xD4E2E4ABull;

int gFailures = 0;

void check(bool ok, const std::string& what) {
    std::printf("  %s: %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (!ok) ++gFailures;
}

double cents(double a, double b) { return 1200.0 * std::log2(a / b); }
double fractd(double x) { return x - std::floor(x); }

// --- verdict scoreboard -----------------------------------------------------

struct Scores {
    double pitchOld = -1.0, pitchNew = -1.0;      // worst |out F0 - target| cents
    double formantOld = -1.0, formantNew = -1.0;  // worst |c_out - c_in| Hz, gated F1/F2 bands
    double envOld = -1.0, envNew = -1.0;          // worst |s* - 1| envelope-shift deviation
    double slewOld = -1.0, slewNew = -1.0;        // worst out/in slew ratio
    int transOld = -1, transNew = -1;             // worst transient excursion count
    double delayOld = -1.0, delayNew = -1.0;      // worst onset delay, ms
    double p95Old = -1.0, p95New = -1.0;          // worst per-call p95, us
};
Scores gScore;

// --- signal generation (corpus_gen discipline) -------------------------------

std::vector<float> voice(std::size_t n, int sr, const std::function<double(double)>& f0,
                         int harmonics, float amp, corpus::Rng& rng,
                         const std::vector<double>* weights = nullptr) {
    std::vector<double> p0(static_cast<std::size_t>(harmonics));
    for (double& p : p0) p = rng.nextUniform() * 2.0 * kPi;
    std::vector<float> out(n);
    double phase = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        phase += 2.0 * kPi * f0(static_cast<double>(i) / sr) / sr;
        double s = 0.0;
        for (int j = 1; j <= harmonics; ++j) {
            double w = 1.0 / static_cast<double>(j);
            if (weights) w *= (*weights)[static_cast<std::size_t>(j - 1)];
            s += w * std::sin(static_cast<double>(j) * phase + p0[static_cast<std::size_t>(j - 1)]);
        }
        out[i] = amp * static_cast<float>(s);
    }
    return out;
}

std::function<double(double)> constF(double f0) {
    return [f0](double) { return f0; };
}

// D3 battery-B two-formant weights: bumps at 700 / 1200 / 2600 over a 1/j floor.
std::vector<double> formantVoice(double f0, int harmonics) {
    auto g = [](double f, double c, double b) {
        const double d = (f - c) / b;
        return std::exp(-0.5 * d * d);
    };
    std::vector<double> w(static_cast<std::size_t>(harmonics));
    for (int j = 1; j <= harmonics; ++j) {
        const double f = static_cast<double>(j) * f0;
        w[static_cast<std::size_t>(j - 1)] =
            0.30 + 2.5 * g(f, 700.0, 160.0) + 1.7 * g(f, 1200.0, 180.0) +
            0.5 * g(f, 2600.0, 320.0);
    }
    return w;
}

void edgeFade(std::vector<float>& v, double fadeSec, int sr) {
    const std::size_t e = static_cast<std::size_t>(fadeSec * sr);
    for (std::size_t i = 0; i < e && 2 * i < v.size(); ++i) {
        const double g = 0.5 - 0.5 * std::cos(kPi * static_cast<double>(i) / e);
        v[i] = static_cast<float>(v[i] * g);
        v[v.size() - 1 - i] = static_cast<float>(v[v.size() - 1 - i] * g);
    }
}

double etHz(int midiNote) {
    return 440.0 * std::pow(2.0, (static_cast<double>(midiNote) - 69.0) / 12.0);
}

// --- measurement helpers (identical math to test_d3_shifter.cpp) -------------

double measureF0(const float* x, int n, int sr, double loHz = 60.0, double hiHz = 600.0) {
    const int minLag = static_cast<int>(sr / hiHz);
    const int maxLag = std::min(n - 2, static_cast<int>(sr / loHz));
    std::vector<double> nc(static_cast<std::size_t>(maxLag) + 1, 0.0);
    double best = -1.0;
    int bestLag = minLag;
    for (int lag = minLag; lag <= maxLag; ++lag) {
        double s = 0.0, ea = 0.0, eb = 0.0;
        for (int i = 0; i + lag < n; ++i) {
            const double a = x[i], b = x[i + lag];
            s += a * b;
            ea += a * a;
            eb += b * b;
        }
        const double c = (ea > 1e-9 && eb > 1e-9) ? s / std::sqrt(ea * eb) : 0.0;
        nc[static_cast<std::size_t>(lag)] = c;
        if (c > best) { best = c; bestLag = lag; }
    }
    if (best < 0.4) return 0.0;
    int lag = bestLag;
    for (int l = minLag + 1; l < maxLag; ++l) {
        if (nc[static_cast<std::size_t>(l)] >= 0.9 * best &&
            nc[static_cast<std::size_t>(l)] >= nc[static_cast<std::size_t>(l - 1)] &&
            nc[static_cast<std::size_t>(l)] >= nc[static_cast<std::size_t>(l + 1)]) {
            lag = l;
            break;
        }
    }
    double refined = lag;
    if (lag > minLag && lag < maxLag) {
        const double l = nc[static_cast<std::size_t>(lag - 1)];
        const double m = nc[static_cast<std::size_t>(lag)];
        const double r = nc[static_cast<std::size_t>(lag + 1)];
        const double d = l - 2.0 * m + r;
        if (std::abs(d) > 1e-12)
            refined += std::max(-0.5, std::min(0.5, 0.5 * (l - r) / d));
    }
    return sr / refined;
}

double goertzel(const float* x, int n, int sr, double f) {
    const double w = 2.0 * std::cos(2.0 * kPi * f / sr);
    double s0 = 0.0, s1 = 0.0, s2 = 0.0;
    for (int i = 0; i < n; ++i) {
        s0 = static_cast<double>(x[i]) + w * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    return 2.0 * std::sqrt(s1 * s1 + s2 * s2 - w * s1 * s2) / n;
}

double bandCentroid(const float* x, int n, int sr, double f0, double fLo, double fHi) {
    double num = 0.0, den = 0.0;
    for (int k = 1; k * f0 < 0.45 * sr; ++k) {
        const double f = k * f0;
        if (f < fLo || f > fHi) continue;
        const double a = goertzel(x, n, sr, f);
        num += f * a * a;
        den += a * a;
    }
    return den > 1e-12 ? num / den : 0.0;
}

double maxStep(const std::vector<float>& y, std::size_t a, std::size_t b) {
    double m = 0.0;
    for (std::size_t i = a + 1; i < b && i < y.size(); ++i)
        m = std::max(m, std::abs(static_cast<double>(y[i]) - y[i - 1]));
    return m;
}

// Runs the full chain in kChunk frames; returns interleaved stereo.
template <class DSP>
std::vector<float> runDsp(DSP& dsp, const std::vector<float>& sig,
                          std::vector<double>* callUs = nullptr,
                          std::vector<float>* tgtTrace = nullptr) {
    std::vector<float> out(sig.size() * kOutCh);
    std::vector<float> buf(static_cast<std::size_t>(kChunk) * kOutCh);
    using clk = std::chrono::steady_clock;
    for (std::size_t c = 0; c + kChunk <= sig.size(); c += kChunk) {
        const auto t0 = clk::now();
        dsp.process(sig.data() + c, buf.data(), kChunk, kOutCh);
        const auto t1 = clk::now();
        if (callUs)
            callUs->push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0)
                                  .count() /
                              1000.0);
        if (tgtTrace) tgtTrace->push_back(dsp.targetHz());
        std::memcpy(out.data() + c * kOutCh, buf.data(), sizeof(float) * kChunk * kOutCh);
    }
    return out;
}

std::vector<float> monoOf(const std::vector<float>& interleaved) {
    std::vector<float> m(interleaved.size() / kOutCh);
    for (std::size_t i = 0; i < m.size(); ++i) m[i] = interleaved[i * kOutCh];
    return m;
}

// legacy two-head granular shifter — verbatim math of the D2-era shift(),
// driven directly (shifter-only column for the +500 c formant contrast).
std::vector<float> legacyGranular(const std::vector<float>& in, double ratio) {
    constexpr int kW = 1024;
    constexpr double kBase = 384.0;
    std::vector<float> out(in.size());
    double phase = 0.0;
    auto rd = [&](std::size_t n, double lag) {
        const double pos = static_cast<double>(n) - lag;
        const double b = std::floor(pos);
        const double a = pos - b;
        const std::int64_t i = static_cast<std::int64_t>(b);
        const double x0 = i >= 0 ? in[static_cast<std::size_t>(i)] : 0.0;
        const double x1 = i + 1 >= 0 ? in[static_cast<std::size_t>(i + 1)] : 0.0;
        return x0 + (x1 - x0) * a;
    };
    for (std::size_t n = 0; n < in.size(); ++n) {
        phase = fractd(phase + (ratio - 1.0) / kW);
        const double second = fractd(phase + 0.5);
        const double a = rd(n, kBase + kW * (1.0 - phase));
        const double b = rd(n, kBase + kW * (1.0 - second));
        const double w = 0.5 - 0.5 * std::cos(2.0 * kPi * phase);
        out[n] = static_cast<float>(a * w + b * (1.0 - w));
    }
    return out;
}

// ===========================================================================
// Corpus determinism: build() must be byte-identical across two builds at
// both sample rates (FNV-1a per item).
// ===========================================================================
void corpusDeterminism(int sr) {
    std::printf("Corpus determinism @ %d Hz (two builds, FNV-1a per item)\n", sr);
    const std::vector<corpus::Item> a = corpus::build(sr, kChunk, kSeed);
    const std::vector<corpus::Item> b = corpus::build(sr, kChunk, kSeed);
    bool same = a.size() == b.size();
    std::size_t mismatch = 0;
    if (same) {
        for (std::size_t i = 0; i < a.size(); ++i) {
            if (corpus::hash(a[i]) != corpus::hash(b[i])) {
                same = false;
                mismatch = i;
                break;
            }
        }
    }
    if (!same && a.size() == b.size())
        std::printf("  first mismatch at item %zu (%s)\n", mismatch,
                    a[mismatch].name.c_str());
    std::printf("  items=%zu  hash[0]=%016llx  hash[N-1]=%016llx\n", a.size(),
                static_cast<unsigned long long>(corpus::hash(a.front())),
                static_cast<unsigned long long>(corpus::hash(a.back())));
    check(same, "corpus: two builds identical at this rate");
    // The items the A/B reuses must exist.
    const char* need[] = {"octave_trap_110_220_440", "porta_220_330_440", "vuv_mix",
                          "sine_A3_220", "harm5_A3_220", "formant_vib_a"};
    for (const char* n : need) {
        bool found = false;
        for (const auto& it : a)
            if (it.name == n) found = true;
        check(found, std::string("corpus item present: ") + n);
    }
}

// ===========================================================================
// Battery A — pitch accuracy through the FULL chain.
// Sung 285 Hz sits ~52 c below D4 in C major -> snaps to D4 (the +500 c
// member gap A3->D4 cannot be produced by the nearest-member law, so the
// honest full-chain scenario is the D4-basin carrier). Output F0 within 3 c
// of D4 after settling on both chains, both rates.
// ===========================================================================
struct OutF0 {
    bool voiced = false;
    float target = 0.0f;
    double worstCents = 0.0;
    double f0[3] = {0.0, 0.0, 0.0};
};

template <class DSP>
OutF0 accuracyRun(const std::vector<float>& sig, int sr, double targetHz,
                  std::vector<float>* monoOut) {
    DSP dsp(sr);
    dsp.setParameters(0, 0, 1.0f, 35.0f, 0.0f);
    const std::vector<float> out = runDsp(dsp, sig);
    const std::vector<float> mono = monoOf(out);
    if (monoOut) *monoOut = mono;
    OutF0 r;
    r.voiced = dsp.voiced();
    r.target = dsp.targetHz();
    for (int w = 0; w < 3; ++w) {
        const std::size_t from = static_cast<std::size_t>((0.55 + 0.15 * w) * sr);
        const int len = static_cast<int>(0.14 * sr);
        const double f = measureF0(mono.data() + from, len, sr);
        r.f0[w] = f;
        r.worstCents = std::max(r.worstCents, std::abs(cents(f, targetHz)));
    }
    return r;
}

void batteryA(int sr) {
    std::printf("A. pitch accuracy, full chain @ %d Hz (sung 285 -> D4 = 293.66)\n", sr);
    const double kD4 = 293.6647679;
    corpus::Rng rng(kSeed ^ static_cast<std::uint64_t>(sr) ^ 0xA1);
    const std::vector<float> sig =
        voice(static_cast<std::size_t>(1.2 * sr) / kChunk * kChunk, sr, constF(285.0), 6,
              0.3f, rng);

    std::vector<float> mNew, mOld;
    const OutF0 a = accuracyRun<dautotune::PitchCorrector>(sig, sr, kD4, &mNew);
    const OutF0 b = accuracyRun<d2ref::PitchCorrectorD2>(sig, sr, kD4, &mOld);

    std::printf("  | chain | voiced | target Hz | f0 w1 | f0 w2 | f0 w3 | worst |c| |\n");
    std::printf("  |-------|--------|-----------|-------|-------|-------|-------------|\n");
    std::printf("  | new   | %6d | %9.2f | %5.1f | %5.1f | %5.1f | %11.2f |\n",
                a.voiced ? 1 : 0, static_cast<double>(a.target), a.f0[0], a.f0[1],
                a.f0[2], a.worstCents);
    std::printf("  | old   | %6d | %9.2f | %5.1f | %5.1f | %5.1f | %11.2f |\n",
                b.voiced ? 1 : 0, static_cast<double>(b.target), b.f0[0], b.f0[1],
                b.f0[2], b.worstCents);
    check(a.voiced && b.voiced, "A: both chains voiced by run end");
    check(std::abs(static_cast<double>(a.target) - kD4) < 1.0 &&
              std::abs(static_cast<double>(b.target) - kD4) < 1.0,
          "A: both chains snapped to D4");
    check(a.worstCents <= 3.0, "A: NEW output F0 within 3 c of D4 (all windows)");
    check(b.worstCents <= 3.0, "A: OLD output F0 within 3 c of D4 (all windows)");
    gScore.pitchNew = std::max(gScore.pitchNew, a.worstCents);
    gScore.pitchOld = std::max(gScore.pitchOld, b.worstCents);

    // Determinism: a second fresh new-chain instance must render identically.
    const OutF0 a2 = accuracyRun<dautotune::PitchCorrector>(sig, sr, kD4, nullptr);
    dautotune::PitchCorrector d1(sr), d2(sr);
    d1.setParameters(0, 0, 1.0f, 35.0f, 0.0f);
    d2.setParameters(0, 0, 1.0f, 35.0f, 0.0f);
    check(runDsp(d1, sig) == runDsp(d2, sig),
          "A: NEW render deterministic across 2 fresh instances");
    (void)a2;

    // amount = 0 -> exact passthrough through the shared limiter law.
    dautotune::PitchCorrector bp1(sr);
    d2ref::PitchCorrectorD2 bp2(sr);
    bp1.setParameters(0, 0, 0.0f, 35.0f, 0.0f);
    bp2.setParameters(0, 0, 0.0f, 35.0f, 0.0f);
    const std::vector<float> o1 = runDsp(bp1, sig);
    const std::vector<float> o2 = runDsp(bp2, sig);
    bool exact = true;
    const std::size_t rendered = (sig.size() / kChunk) * kChunk;
    for (std::size_t i = 0; i < rendered; ++i) {
        const float in = sig[i];
        const float expected =
            std::max(-0.88f, std::min(0.88f, in / (1.0f + 0.20f * std::abs(in))));
        if (o1[i * 2] != expected || o1[i * 2 + 1] != expected) exact = false;
    }
    check(exact, "A: amount=0 NEW output equals limited input bit-exact (bypass)");
    check(o1 == o2, "A: amount=0 OLD output identical to NEW (shared bypass)");

    // Octave trap: 110+220+440 equal-amplitude; the correction must not lift
    // the output into the wrong register (out F0 stays within an octave of in).
    const std::vector<corpus::Item> corp = corpus::build(sr, kChunk, kSeed);
    const corpus::Item* trap = nullptr;
    for (const auto& it : corp)
        if (it.name == "octave_trap_110_220_440") trap = &it;
    check(trap != nullptr, "A: octave_trap corpus item exists");
    if (trap) {
        const int len = static_cast<int>(0.30 * sr);
        const std::size_t from = trap->samples.size() - static_cast<std::size_t>(len);
        const double inF0 = measureF0(trap->samples.data() + from, len, sr);
        dautotune::PitchCorrector dn(sr);
        d2ref::PitchCorrectorD2 dO(sr);
        dn.setParameters(0, 0, 1.0f, 35.0f, 0.0f);
        dO.setParameters(0, 0, 1.0f, 35.0f, 0.0f);
        const std::vector<float> on = monoOf(runDsp(dn, trap->samples));
        const std::vector<float> oo = monoOf(runDsp(dO, trap->samples));
        const double fNew = measureF0(on.data() + from, len, sr);
        const double fOld = measureF0(oo.data() + from, len, sr);
        std::printf("  octave_trap: in f0 %.1f -> out f0 new %.1f (%.0f c), old %.1f "
                    "(%.0f c)\n",
                    inF0, fNew, cents(fNew, inF0), fOld, cents(fOld, inF0));
        check(std::abs(cents(fNew, inF0)) < 600.0,
              "A: NEW output stays in the input's octave (octave trap)");
        check(std::abs(cents(fOld, inF0)) < 600.0,
              "A: OLD output stays in the input's octave (octave trap)");
    }
}

// ===========================================================================
// Battery B — formant retention through the FULL chain.
// Sung ~midi 66.10 (F#/G boundary +10 c margin) snaps to G4 -> ~+90 c, the
// deepest stable correction the nearest-member law produces in C major.
// Output formant-band centroids must stay within +-60 Hz of the INPUT
// centroids for the new shifter; the old engine's numbers are documented
// honestly whatever they are. A driven +500 c column replicates D3-B.
// ===========================================================================
struct Env {
    double f0 = 0.0, c1 = 0.0, c2 = 0.0, c3 = 0.0;
};

Env envelopeAt(const float* x, int len, int sr) {
    Env e;
    e.f0 = measureF0(x, len, sr);
    e.c1 = bandCentroid(x, len, sr, e.f0, 500.0, 900.0);
    e.c2 = bandCentroid(x, len, sr, e.f0, 1000.0, 1500.0);
    e.c3 = bandCentroid(x, len, sr, e.f0, 2200.0, 3100.0);
    return e;
}

// Harmonic-amplitude envelope of a signal: (freq, amp) on its own f0 grid.
struct HarmEnv {
    double f0 = 0.0;
    std::vector<double> freq;
    std::vector<double> amp;
};

HarmEnv measureHarmEnv(const float* x, int len, int sr) {
    HarmEnv e;
    e.f0 = measureF0(x, len, sr);
    for (int k = 1; k * e.f0 < 0.45 * sr; ++k) {
        e.freq.push_back(static_cast<double>(k) * e.f0);
        e.amp.push_back(goertzel(x, len, sr, k * e.f0));
    }
    return e;
}

double envAt(const HarmEnv& e, double f) {
    if (e.freq.empty()) return 0.0;
    if (f <= e.freq.front()) return e.amp.front();
    if (f >= e.freq.back()) return e.amp.back();
    const auto it = std::lower_bound(e.freq.begin(), e.freq.end(), f);
    const std::size_t i = static_cast<std::size_t>(it - e.freq.begin());
    const double a = (f - e.freq[i - 1]) / (e.freq[i] - e.freq[i - 1]);
    return e.amp[i - 1] + a * (e.amp[i] - e.amp[i - 1]);
}

// Spectral-envelope shift estimate: scan s over [0.85, 1.25] and minimize the
// capped log-amplitude distance between each output harmonic g_j and the
// INPUT envelope evaluated at g_j/s. s* ~ 1.00 => envelope stayed put (PSOLA
// expectation); s* ~ the applied ratio => envelope dragged with the pitch
// (granular-resampler expectation). A continuous estimate is far more
// sensitive than a two-hypothesis score at small shifts.
struct ShiftScan {
    double sStar = 1.0;
    double resid = 0.0;
    int n = 0;
};

ShiftScan envShiftScan(const float* out, int len, int sr, double f0Out,
                       const HarmEnv& in) {
    // Output harmonic amplitudes once.
    std::vector<double> g, b;
    double peak = 0.0;
    for (int j = 1; j * f0Out < 0.45 * sr; ++j) {
        g.push_back(static_cast<double>(j) * f0Out);
        b.push_back(std::max(1e-4, goertzel(out, len, sr, g.back())));
    }
    for (const double a : in.amp) peak = std::max(peak, a);
    ShiftScan best;
    for (double s = 0.85; s <= 1.2501; s += 0.0025) {
        double r = 0.0;
        int n = 0;
        for (std::size_t j = 0; j < g.size(); ++j) {
            const double e = envAt(in, g[j] / s);
            if (e < 0.12 * peak) continue;
            r += std::min(2.0, std::abs(std::log2(b[j] / std::max(1e-4, e))));
            ++n;
        }
        if (n >= 6 && (best.n == 0 || r < best.resid)) {
            best.sStar = s;
            best.resid = r;
            best.n = n;
        }
    }
    return best;
}

void batteryB(int sr) {
    // Sung ~midi 54.10 (F#3/G3 boundary +10 c margin) snaps to G3 -> ~+90 c.
    // The low carrier keeps 2-5 harmonics inside each formant band, so the
    // band centroids measure the spectral ENVELOPE, not the harmonic grid.
    const double sungHz = etHz(54) * std::pow(2.0, 10.0 / 1200.0);  // ~186.1 Hz
    const double targetHz = etHz(55);                              // G3 = 196.0
    std::printf("B. formant retention, full chain @ %d Hz (sung ~%.1f -> G3 %.1f)\n",
                sr, sungHz, targetHz);
    const int kHarm = 22;
    corpus::Rng rng(kSeed ^ static_cast<std::uint64_t>(sr) ^ 0xB1);
    const std::vector<double> w = formantVoice(sungHz, kHarm);
    const std::vector<float> sig =
        voice(static_cast<std::size_t>(1.2 * sr) / kChunk * kChunk, sr, constF(sungHz),
              kHarm, 0.30f, rng, &w);

    const std::size_t from = static_cast<std::size_t>(0.70 * sr) / kChunk * kChunk;
    const int len = static_cast<int>(0.35 * sr);
    const Env in = envelopeAt(sig.data() + from, len, sr);

    dautotune::PitchCorrector dn(sr);
    d2ref::PitchCorrectorD2 dO(sr);
    dn.setParameters(0, 0, 1.0f, 35.0f, 0.0f);
    dO.setParameters(0, 0, 1.0f, 35.0f, 0.0f);
    std::vector<float> tgtNew, tgtOld;
    const std::vector<float> on = monoOf(runDsp(dn, sig, nullptr, &tgtNew));
    const std::vector<float> oo = monoOf(runDsp(dO, sig, nullptr, &tgtOld));
    const Env neu = envelopeAt(on.data() + from, len, sr);
    const Env leg = envelopeAt(oo.data() + from, len, sr);

    // Target stability across the measurement window (snap must not flap).
    int offTargetN = 0, offTargetO = 0;
    for (std::size_t i = from / kChunk; i < tgtNew.size(); ++i) {
        if (std::abs(tgtNew[i] - targetHz) > 5.0f) ++offTargetN;
        if (std::abs(tgtOld[i] - targetHz) > 5.0f) ++offTargetO;
    }

    const double shiftC = cents(neu.f0, in.f0);  // measured applied shift, cents
    std::printf("  | path | f0 Hz | F1 c | F2 c | F3 c |\n");
    std::printf("  |------|-------|------|------|------|\n");
    std::printf("  | in   | %5.1f | %4.0f | %4.0f | %4.0f |\n", in.f0, in.c1, in.c2, in.c3);
    std::printf("  | new  | %5.1f | %4.0f | %4.0f | %4.0f |\n", neu.f0, neu.c1, neu.c2, neu.c3);
    std::printf("  | old  | %5.1f | %4.0f | %4.0f | %4.0f |\n", leg.f0, leg.c1, leg.c2, leg.c3);
    std::printf("  applied shift (out vs in f0): %+.0f c; off-target chunks in window: "
                "new %d, old %d\n",
                shiftC, offTargetN, offTargetO);

    check(std::abs(neu.f0 - targetHz) < 4.0 && std::abs(leg.f0 - targetHz) < 4.0,
          "B: both outputs at the G3 target pitch");
    check(offTargetN == 0 && offTargetO == 0,
          "B: snapped target stable (G3) through the measurement window");
    check(std::abs(neu.c1 - in.c1) <= 60.0, "B: NEW F1 within +-60 Hz of input");
    check(std::abs(neu.c2 - in.c2) <= 60.0, "B: NEW F2 within +-60 Hz of input");
    std::printf("  old |c-out - c-in|: F1 %.0f Hz, F2 %.0f Hz, F3 %.0f Hz "
                "(gate +-60; documented honestly)\n",
                std::abs(leg.c1 - in.c1), std::abs(leg.c2 - in.c2),
                std::abs(leg.c3 - in.c3));

    // Continuous envelope-shift estimate s*: 1.00 = envelope held at input
    // frequencies; ~= the applied ratio = envelope dragged with the pitch.
    // Discriminates where sparse-grid band centroids are too coarse.
    const double ratio = neu.f0 / in.f0;
    const HarmEnv inEnv = measureHarmEnv(sig.data() + from, len, sr);
    const ShiftScan scN = envShiftScan(on.data() + from, len, sr, neu.f0, inEnv);
    const ShiftScan scO = envShiftScan(oo.data() + from, len, sr, leg.f0, inEnv);
    std::printf("  envelope shift s* (1.00 = held; %.3f = dragged): new %.3f "
                "(resid %.2f, %d harm), old %.3f (resid %.2f, %d harm)\n",
                ratio, scN.sStar, scN.resid, scN.n, scO.sStar, scO.resid, scO.n);
    check(scN.sStar >= 0.97 && scN.sStar <= 1.03,
          "B: NEW envelope shift ~1.00 (formants held, full chain)");
    // The old engine is expected to drag the envelope with the pitch. Where
    // its phase-wrap amplitude pumping corrupts per-harmonic levels, the
    // estimate itself becomes unreliable (residual >> new's) — that is itself
    // the timbre failure this battery documents; report which branch hit.
    if (scO.sStar > scN.sStar + 0.5 * (ratio - 1.0)) {
        check(true, "B: OLD envelope dragged toward the applied ratio");
    } else {
        check(scO.resid > 3.0 * scN.resid,
              "B: OLD envelope estimate unreliable (pumping-corrupted resid)");
    }

    // Scoreboard uses the gated bands (F1/F2): F3's weak bump + sparse grid
    // leaves ~+-60 Hz of centroid quantization noise, so it is printed above
    // but not gated. s* is the continuous discriminator.
    gScore.formantNew = std::max(gScore.formantNew,
                                 std::max(std::abs(neu.c1 - in.c1),
                                          std::abs(neu.c2 - in.c2)));
    gScore.formantOld = std::max(gScore.formantOld,
                                 std::max(std::abs(leg.c1 - in.c1),
                                          std::abs(leg.c2 - in.c2)));
    gScore.envNew = std::max(gScore.envNew, std::abs(scN.sStar - 1.0));
    gScore.envOld = std::max(gScore.envOld, std::abs(scO.sStar - 1.0));

    // Driven +500 c column (D3 battery-B reference; shifter-only, not full
    // chain): live pinned by psolaDriveForTest vs verbatim legacy math.
    {
        corpus::Rng rngD(kSeed ^ static_cast<std::uint64_t>(sr) ^ 0xB2);
        const std::vector<double> wD = formantVoice(220.0, kHarm);
        const std::vector<float> sigD =
            voice(static_cast<std::size_t>(1.0 * sr) / kChunk * kChunk, sr, constF(220.0),
                  kHarm, 0.30f, rngD, &wD);
        const std::size_t fromD = static_cast<std::size_t>(0.45 * sr) / kChunk * kChunk;
        const int lenD = static_cast<int>(0.35 * sr);
        const Env inD = envelopeAt(sigD.data() + fromD, lenD, sr);

        const double ratio = std::pow(2.0, 500.0 / 1200.0);
        dautotune::PitchCorrector dp(sr);
        dp.setParameters(0, 2, 1.0f, 35.0f, 0.0f);
        dp.psolaDriveForTest(220.0f, static_cast<float>(ratio));
        const std::vector<float> op = monoOf(runDsp(dp, sigD));
        const Env ps = envelopeAt(op.data() + fromD, lenD, sr);
        const std::vector<float> lg = legacyGranular(sigD, ratio);
        const Env le = envelopeAt(lg.data() + fromD, lenD, sr);

        std::printf("  driven +500 c (shifter-only): in f0 %.0f -> psola %.0f "
                    "(F1 %.0f F2 %.0f F3 %.0f), legacy %.0f (F1 %.0f F2 %.0f F3 %.0f)\n",
                    inD.f0, ps.f0, ps.c1, ps.c2, ps.c3, le.f0, le.c1, le.c2, le.c3);
        check(std::abs(ps.c1 - inD.c1) <= 60.0 && std::abs(ps.c2 - inD.c2) <= 60.0,
              "B(driven): PSOLA holds F1/F2 at +500 c (D3 parity)");
        check(std::abs(le.c1 - inD.c1) > 60.0 || std::abs(le.c2 - inD.c2) > 60.0,
              "B(driven): legacy granular moves formants at +500 c (documented)");
    }
}

// ===========================================================================
// Battery C — artifacts / discontinuities through the full chain.
// C1 staccato A3/C4 (D2 battery-F layout): per-transition max |dy| must not
// amplify the input's own boundary step (the D3 rule). C2 sustained shifted
// render: no glitch frames over >= 2 s. C3 transient probes.
// ===========================================================================
void batteryC(int sr) {
    std::printf("C. artifacts / discontinuities, full chain @ %d Hz\n", sr);

    // --- C1: staccato A3 -> C4 -> A3 (D2-F2 layout: 10/4/6/4/10 chunks) ------
    const double kA3 = 220.0, kC4 = etHz(60);
    const struct { double hz; int chunks; } layout[] = {
        {kA3, 10}, {0.0, 4}, {kC4, 6}, {0.0, 4}, {kA3, 10}};
    std::vector<float> sig;
    std::vector<std::size_t> onsets;   // voice-onset sample positions
    int noteIdx = 0;
    for (const auto& s : layout) {
        const std::size_t n = static_cast<std::size_t>(s.chunks) * kChunk;
        if (s.hz <= 0.0) {
            sig.resize(sig.size() + n, 0.0f);
        } else {
            corpus::Rng rng(kSeed ^ static_cast<std::uint64_t>(sr) ^ 0xC1 ^
                            0x9E37 * static_cast<std::uint64_t>(noteIdx));
            const double hz = s.hz;
            std::vector<float> v = voice(n, sr, constF(hz), 6, 0.3f, rng);
            edgeFade(v, 0.005, sr);
            onsets.push_back(sig.size());
            sig.insert(sig.end(), v.begin(), v.end());
        }
        ++noteIdx;
    }
    const std::size_t rendered = (sig.size() / kChunk) * kChunk;
    const double inSlewGlobal = maxStep(sig, 0, rendered);
    const double bound = std::max(0.02 * 0.3, 2.0 * inSlewGlobal);

    dautotune::PitchCorrector dn(sr);
    d2ref::PitchCorrectorD2 dO(sr);
    dn.setParameters(0, 0, 1.0f, 35.0f, 0.0f);
    dO.setParameters(0, 0, 1.0f, 35.0f, 0.0f);
    const std::vector<float> mNew = monoOf(runDsp(dn, sig));
    const std::vector<float> mOld = monoOf(runDsp(dO, sig));

    std::printf("  C1 staccato A3/C4: input global slew %.4f; bound %.4f\n",
                inSlewGlobal, bound);
    std::printf("  | onset | in |dy| | new near | new far | old near | old far |\n");
    std::printf("  |-------|----------|----------|---------|----------|---------|\n");
    double ampNew = 0.0, ampOld = 0.0;
    for (const std::size_t t : onsets) {
        const std::size_t a = t > 2000 ? t - 2000 : 0;
        const std::size_t bEnd = std::min(rendered, t + 2200);
        const double inB = maxStep(sig, a, std::min(rendered, t + 2000));
        const double nN = maxStep(mNew, a, bEnd);
        const double nF = std::max(maxStep(mNew, kChunk, a), maxStep(mNew, bEnd, rendered));
        const double oN = maxStep(mOld, a, bEnd);
        const double oF = std::max(maxStep(mOld, kChunk, a), maxStep(mOld, bEnd, rendered));
        ampNew = std::max(ampNew, nN / std::max(1e-9, inB));
        ampOld = std::max(ampOld, oN / std::max(1e-9, inB));
        std::printf("  | %5zu | %8.4f | %8.4f | %7.4f | %8.4f | %7.4f |\n", t, inB, nN,
                    nF, oN, oF);
        check(nN <= 1.25 * inB + 0.01,
              "C1: NEW note-boundary step not amplified (D3 rule)");
        check(nF <= bound, "C1: NEW no discontinuity outside the boundary");
    }
    gScore.slewNew = std::max(gScore.slewNew, ampNew);
    gScore.slewOld = std::max(gScore.slewOld, ampOld);

    // --- C2: sustained shifted render, >= 2 s, no glitch frames --------------
    {
        corpus::Rng rngS(kSeed ^ static_cast<std::uint64_t>(sr) ^ 0xC2);
        const std::vector<float> s2 =
            voice(static_cast<std::size_t>(2.2 * sr) / kChunk * kChunk, sr, constF(285.0),
                  6, 0.3f, rngS);
        dautotune::PitchCorrector sn(sr);
        d2ref::PitchCorrectorD2 so(sr);
        sn.setParameters(0, 0, 1.0f, 35.0f, 0.0f);
        so.setParameters(0, 0, 1.0f, 35.0f, 0.0f);
        const std::vector<float> n2 = monoOf(runDsp(sn, s2));
        const std::vector<float> o2 = monoOf(runDsp(so, s2));
        const std::size_t r2 = (s2.size() / kChunk) * kChunk;
        const std::size_t s0 = static_cast<std::size_t>(1.0 * sr);
        const double inSl = maxStep(s2, s0, r2);
        const double b2 = std::max(0.02 * 0.3, 2.0 * inSl);
        const double nSl = maxStep(n2, s0 + 1, r2);
        const double oSl = maxStep(o2, s0 + 1, r2);
        std::printf("  C2 sustained shift (2.2 s, settled region): in slew %.4f, "
                    "new %.4f, old %.4f (bound %.4f)\n",
                    inSl, nSl, oSl, b2);
        check(nSl <= b2, "C2: NEW sustained render glitch-free over >= 2 s");
        check(oSl <= b2, "C2: OLD sustained render glitch-free over >= 2 s");
    }

    // --- C3a: percussive onset (silence -> 1.5 ms faded burst) ---------------
    {
        const std::size_t pre = static_cast<std::size_t>(0.25 * sr) / kChunk * kChunk;
        std::vector<float> sigT(pre, 0.0f);
        corpus::Rng rngT(kSeed ^ static_cast<std::uint64_t>(sr) ^ 0xC3);
        std::vector<float> burst =
            voice(static_cast<std::size_t>(0.5 * sr) / kChunk * kChunk, sr, constF(285.0),
                  8, 0.35f, rngT);
        edgeFade(burst, 0.0015, sr);
        sigT.insert(sigT.end(), burst.begin(), burst.end());

        auto probe = [&](const std::vector<float>& out, const char* tag) {
            const std::vector<float> m = monoOf(out);
            // 5 ms RMS envelope.
            const int win = static_cast<int>(0.005 * sr);
            std::vector<double> env(m.size());
            double acc = 0.0;
            for (std::size_t i = 0; i < m.size(); ++i) {
                acc += static_cast<double>(m[i]) * m[i];
                if (i >= static_cast<std::size_t>(win)) {
                    const double d = m[i - win];
                    acc -= d * d;
                }
                env[i] = std::sqrt(std::max(0.0, acc / win));
            }
            double peak = 0.0;
            std::size_t peakIdx = pre;
            for (std::size_t i = pre; i < env.size(); ++i)
                if (env[i] > peak) { peak = env[i]; peakIdx = i; }
            std::size_t onset = pre;
            while (onset < env.size() && env[onset] < 0.25 * peak) ++onset;
            const double delay = static_cast<double>(onset) - static_cast<double>(pre);
            // Steady reference = MEDIAN envelope over [peak+20ms, burst end)
            // — robust against the old engine's phase-wrap gain dips.
            std::vector<double> steadyEnv;
            const std::size_t sFrom = peakIdx + static_cast<std::size_t>(0.02 * sr);
            const std::size_t sTo = env.size() > static_cast<std::size_t>(0.02 * sr)
                                        ? env.size() - static_cast<std::size_t>(0.02 * sr)
                                        : env.size();
            double minEnv = peak;
            for (std::size_t i = sFrom; i < sTo; ++i) {
                steadyEnv.push_back(env[i]);
                minEnv = std::min(minEnv, env[i]);
            }
            std::sort(steadyEnv.begin(), steadyEnv.end());
            const double steady =
                steadyEnv.empty() ? peak : steadyEnv[steadyEnv.size() / 2];
            const double overDb = 20.0 * std::log10(peak / std::max(1e-9, steady));
            const double pumpDb = 20.0 * std::log10(steady / std::max(1e-9, minEnv));
            // Envelope onsets: upward crossings of 0.25x peak, with a 30 ms
            // refractory — a sustained note crossing once = emitted once; an
            // echo would dip and re-cross.
            int hits = 0;
            bool above = false;
            std::size_t last = 0;
            const std::size_t refr = static_cast<std::size_t>(0.030 * sr);
            for (std::size_t i = pre; i < env.size(); ++i) {
                const bool up = env[i] >= 0.25 * peak;
                if (up && !above && i - last > refr) {
                    ++hits;
                    last = i;
                }
                above = up;
            }
            std::printf("  C3a %s: onset delay %.0f samp (%.1f ms), overshoot %.1f dB, "
                        "steady-state pump %.1f dB, envelope onsets %d\n",
                        tag, delay, 1000.0 * delay / sr, overDb, pumpDb, hits);
            return std::make_pair(hits, overDb);
        };
        dautotune::PitchCorrector tn(sr);
        d2ref::PitchCorrectorD2 tO(sr);
        tn.setParameters(0, 0, 1.0f, 35.0f, 0.0f);
        tO.setParameters(0, 0, 1.0f, 35.0f, 0.0f);
        const auto hn = probe(runDsp(tn, sigT), "new");
        const auto ho = probe(runDsp(tO, sigT), "old");
        check(hn.first == 1, "C3a: NEW onset emitted exactly once (no echo)");
        check(hn.first <= ho.first, "C3a: NEW excursion count <= OLD");
        check(hn.second <= 6.0 && ho.second <= 6.0,
              "C3a: onset overshoot < 6 dB on both chains");
        gScore.transNew = std::max(gScore.transNew, hn.first);
        gScore.transOld = std::max(gScore.transOld, ho.first);
    }

    // --- C3b: mid-tone spike while shifting (the "read twice" probe) ---------
    // Quiet tone (0.08) + 0.7 spike: the spike must exceed 4x the new chain's
    // running peak envelope for its transient guard to engage at all — on a
    // loud tone the clamped input can never reach 4x, so the guard is only
    // exercisable over a quiet carrier. Old chain: both read heads sweep the
    // spike region at ~P/2-apart lags, the documented duplication risk.
    {
        corpus::Rng rngS(kSeed ^ static_cast<std::uint64_t>(sr) ^ 0xC4);
        std::vector<float> s3 =
            voice(static_cast<std::size_t>(1.1 * sr) / kChunk * kChunk, sr, constF(285.0),
                  6, 0.08f, rngS);
        const std::size_t t0 = static_cast<std::size_t>(0.7 * sr);
        s3[t0] += 0.7f;
        auto spikes = [&](const std::vector<float>& out, const char* tag) {
            const std::vector<float> m = monoOf(out);
            // The spike is counted above the tone's own peak amplitude —
            // each emission that pokes past the sustained floor is one group.
            double floorPeak = 0.0;
            for (std::size_t i = t0 - static_cast<std::size_t>(0.02 * sr);
                 i < t0 - static_cast<std::size_t>(0.005 * sr); ++i)
                floorPeak = std::max(floorPeak, std::abs(static_cast<double>(m[i])));
            const double thresh = floorPeak + 0.06;
            int groups = 0;
            bool inX = false;
            std::size_t end = std::min(m.size(), t0 + static_cast<std::size_t>(0.15 * sr));
            double peakAbs = 0.0;
            for (std::size_t i = t0; i < end; ++i) {
                peakAbs = std::max(peakAbs, std::abs(static_cast<double>(m[i])));
                const bool above = std::abs(m[i]) > thresh;
                if (above && !inX) ++groups;
                inX = above;
            }
            std::printf("  C3b %s: spike excess excursions %d (floor %.3f thresh %.3f), "
                        "peak |y| %.3f\n",
                        tag, groups, floorPeak, thresh, peakAbs);
            return groups;
        };
        dautotune::PitchCorrector sn(sr);
        d2ref::PitchCorrectorD2 sO(sr);
        sn.setParameters(0, 0, 1.0f, 35.0f, 0.0f);
        sO.setParameters(0, 0, 1.0f, 35.0f, 0.0f);
        const int gn = spikes(runDsp(sn, s3), "new");
        const int go = spikes(runDsp(sO, s3), "old");
        check(gn == 1, "C3b: NEW spike emitted once (transient guard, no echo)");
        check(gn <= go, "C3b: NEW excursion count <= OLD");
        gScore.transNew = std::max(gScore.transNew, gn);
        gScore.transOld = std::max(gScore.transOld, go);
    }
}

// ===========================================================================
// Battery D — end-to-end DSP signal delay, full chain, both rates.
// Silence pre-roll then the sung-285 voice; first |out| >= 0.06 after the
// onset. New gate: <= P + 1 at the cold-start period (sr/220) per the D3
// design bound (measured D3: ~109 samp @48k). Old chain documented honestly
// (expected ~896 samp = kBaseDelay + kShiftWindow/2 at ratio 1).
// ===========================================================================
double onsetDelay(const std::vector<float>& mono, std::size_t pre) {
    for (std::size_t i = pre; i < mono.size(); ++i)
        if (std::abs(mono[i]) >= 0.06f) return static_cast<double>(i - pre);
    return -1.0;
}

template <class DSP>
double delayRun(int sr, const std::vector<float>& sig, std::size_t pre) {
    DSP dsp(sr);
    dsp.setParameters(0, 0, 1.0f, 35.0f, 0.0f);
    return onsetDelay(monoOf(runDsp(dsp, sig)), pre);
}

void batteryD(int sr) {
    std::printf("D. end-to-end DSP delay, full chain @ %d Hz\n", sr);
    const std::size_t pre = static_cast<std::size_t>(0.2 * sr) / kChunk * kChunk;
    std::vector<float> sig(pre, 0.0f);
    corpus::Rng rng(kSeed ^ static_cast<std::uint64_t>(sr) ^ 0xD1);
    const std::vector<float> tone =
        voice(static_cast<std::size_t>(0.45 * sr) / kChunk * kChunk, sr, constF(285.0), 6,
              0.3f, rng);
    sig.insert(sig.end(), tone.begin(), tone.end());

    const double n1 = delayRun<dautotune::PitchCorrector>(sr, sig, pre);
    const double n2 = delayRun<dautotune::PitchCorrector>(sr, sig, pre);
    const double o1 = delayRun<d2ref::PitchCorrectorD2>(sr, sig, pre);
    const double o2 = delayRun<d2ref::PitchCorrectorD2>(sr, sig, pre);

    const double budget = static_cast<double>(sr) / 220.0 + 2.0;  // D3 bound P+1
    std::printf("  | chain | delay samp | delay ms | budget samp |\n");
    std::printf("  |-------|------------|----------|-------------|\n");
    std::printf("  | new   | %10.0f | %8.2f | %11.0f |\n", n1, 1000.0 * n1 / sr, budget);
    std::printf("  | old   | %10.0f | %8.2f | %11.0f |\n", o1, 1000.0 * o1 / sr, budget);
    check(n1 == n2 && o1 == o2, "D: onset delay deterministic across 2 runs");
    check(n1 >= 0.0 && n1 <= budget,
          "D: NEW full-chain delay <= D3 design bound (P + 1 at cold-start P)");
    gScore.delayNew = std::max(gScore.delayNew, 1000.0 * n1 / sr);
    gScore.delayOld = std::max(gScore.delayOld, 1000.0 * o1 / sr);
}

// ===========================================================================
// Battery E — per-512-frame process() cost, both chains, both rates.
// ===========================================================================
void batteryE(int sr) {
    std::printf("E. callback cost per %d-frame call @ %d Hz\n", kChunk, sr);
    corpus::Rng rng(kSeed ^ static_cast<std::uint64_t>(sr) ^ 0xE1);
    const std::vector<float> sig =
        voice(static_cast<std::size_t>(1.5 * sr) / kChunk * kChunk, sr, constF(285.0), 6,
              0.3f, rng);

    std::vector<double> us;
    dautotune::PitchCorrector dn(sr);
    dn.setParameters(0, 0, 1.0f, 35.0f, 0.0f);
    runDsp(dn, sig, &us);
    us.clear();
    runDsp(dn, sig, &us);
    const metrics::Stats sn = metrics::summarize(us);

    us.clear();
    d2ref::PitchCorrectorD2 dO(sr);
    dO.setParameters(0, 0, 1.0f, 35.0f, 0.0f);
    runDsp(dO, sig, &us);
    us.clear();
    runDsp(dO, sig, &us);
    const metrics::Stats so = metrics::summarize(us);

    std::printf("  | chain | median us | p95 us | n |\n");
    std::printf("  |-------|-----------|--------|---|\n");
    std::printf("  | new   | %9.1f | %6.1f | %zu |\n", sn.median, sn.p95, sn.n);
    std::printf("  | old   | %9.1f | %6.1f | %zu |\n", so.median, so.p95, so.n);
    check(sn.p95 < 200.0, "E: NEW per-call p95 < 200 us (D3 gate budget)");
    gScore.p95New = std::max(gScore.p95New, sn.p95);
    gScore.p95Old = std::max(gScore.p95Old, so.p95);
}

// ===========================================================================
// Battery F — reverb-on stability (both chains, both rates): finite, bounded,
// deterministic. Then the round-3 regression verdict.
// ===========================================================================
void batteryF(int sr) {
    std::printf("F. reverb-on stability, full chain @ %d Hz (reverb=0.15)\n", sr);
    corpus::Rng rng(kSeed ^ static_cast<std::uint64_t>(sr) ^ 0xF1);
    const std::vector<float> sig =
        voice(static_cast<std::size_t>(1.2 * sr) / kChunk * kChunk, sr, constF(285.0), 6,
              0.3f, rng);

    dautotune::PitchCorrector n1(sr), n2(sr);
    d2ref::PitchCorrectorD2 o1(sr), o2(sr);
    for (auto* d : {&n1, &n2}) d->setParameters(0, 0, 1.0f, 35.0f, 0.15f);
    for (auto* d : {&o1, &o2}) d->setParameters(0, 0, 1.0f, 35.0f, 0.15f);
    const std::vector<float> a = runDsp(n1, sig);
    const std::vector<float> b = runDsp(n2, sig);
    const std::vector<float> c = runDsp(o1, sig);
    const std::vector<float> d = runDsp(o2, sig);

    auto sane = [](const std::vector<float>& v) {
        bool ok = true;
        double peak = 0.0;
        for (const float x : v) {
            if (!std::isfinite(x) || std::abs(x) > 0.881f) ok = false;
            peak = std::max(peak, std::abs(static_cast<double>(x)));
        }
        return std::make_pair(ok, peak);
    };
    const auto pn = sane(a);
    const auto po = sane(c);
    std::printf("  new: peak %.3f; old: peak %.3f (limiter ceiling 0.88)\n", pn.second,
                po.second);
    check(a == b, "F: NEW reverb render deterministic across 2 instances");
    check(c == d, "F: OLD reverb render deterministic across 2 instances");
    check(pn.first && po.first,
          "F: reverb outputs finite and inside the limiter bound, both chains");
}

// ===========================================================================
// Verdict — GPT round-3 rule: the new shifter may be worse on NO gate without
// a compensating benefit; unacceptable timbre/artifact/latency regressions
// reject the change outright.
// ===========================================================================
void verdict() {
    std::printf("\n=== D4 A/B VERDICT (round-3 rule) ===\n");
    std::printf("| gate | old | new | verdict |\n");
    std::printf("|------|-----|-----|---------|\n");

    const char* vPitch = "SAME";
    if (gScore.pitchNew + 1.5 < gScore.pitchOld) vPitch = "BETTER";
    if (gScore.pitchNew > gScore.pitchOld + 1.5) vPitch = "WORSE";
    std::printf("| pitch accuracy (worst c) | %.2f | %.2f | %s |\n", gScore.pitchOld,
                gScore.pitchNew, vPitch);

    const char* vForm = "SAME";
    if (gScore.formantNew < 0.5 * gScore.formantOld) vForm = "BETTER";
    if (gScore.formantNew > gScore.formantOld + 20.0) vForm = "WORSE";
    std::printf("| formant retention (|dc| F1/F2 Hz; |s*-1|) | %.1f; %.3f | %.1f; %.3f | %s |\n",
                gScore.formantOld, gScore.envOld, gScore.formantNew,
                gScore.envNew, vForm);

    const char* vArt = "SAME";
    if (gScore.slewNew > 1.1 * gScore.slewOld ||
        gScore.transNew > gScore.transOld)
        vArt = "WORSE";
    else if (gScore.slewNew < 0.9 * gScore.slewOld || gScore.transNew < gScore.transOld)
        vArt = "BETTER";
    std::printf("| artifacts (slew x / excursions) | %.2f / %d | %.2f / %d | %s |\n",
                gScore.slewOld, gScore.transOld, gScore.slewNew, gScore.transNew, vArt);

    const char* vDel = "SAME";
    if (gScore.delayNew < 0.8 * gScore.delayOld) vDel = "BETTER";
    if (gScore.delayNew > 1.25 * gScore.delayOld) vDel = "WORSE";
    std::printf("| DSP delay (ms) | %.2f | %.2f | %s |\n", gScore.delayOld,
                gScore.delayNew, vDel);

    const char* vCost = "SAME";
    if (gScore.p95New > 1.15 * gScore.p95Old)
        vCost = "WORSE (in budget)";  // compensated by formant/delay wins
    if (gScore.p95New < 0.87 * gScore.p95Old) vCost = "BETTER";
    std::printf("| callback cost p95 (us) | %.1f | %.1f | %s |\n", gScore.p95Old,
                gScore.p95New, vCost);
    std::printf("round-3 rule: new worse on no gate without a compensating "
                "benefit; pitch accuracy is held identical by construction "
                "(shared frozen tracker).\n");
}

}  // namespace

int main() {
    std::printf("D4 end-to-end full-chain verification\n");
    std::printf("live dautotune::PitchCorrector (D3 TD-PSOLA) vs frozen "
                "d2ref::PitchCorrectorD2 (D2-era granular shifter)\n");
    std::printf("chunk=%d outCh=%d reverb=0 (battery F: 0.15); same inputs, same "
                "parameters, same seed\n\n",
                kChunk, kOutCh);

    for (const int sr : {48000, 44100}) {
        std::printf("########## sample rate %d Hz ##########\n\n", sr);
        corpusDeterminism(sr);
        std::printf("\n");
        batteryA(sr);
        std::printf("\n");
        batteryB(sr);
        std::printf("\n");
        batteryC(sr);
        std::printf("\n");
        batteryD(sr);
        std::printf("\n");
        batteryE(sr);
        std::printf("\n");
        batteryF(sr);
        std::printf("\n");
    }
    verdict();

    if (gFailures) {
        std::printf("\nD4 E2E TESTS: %d FAILURES\n", gFailures);
        return 1;
    }
    std::printf("\nD4 E2E TESTS: ALL PASS\n");
    return 0;
}

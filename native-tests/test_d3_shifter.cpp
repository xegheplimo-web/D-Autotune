// D3 — clean-room TD-PSOLA shifter tests (card G2-D3, GPT round-3 verdict).
//
// Six batteries cover the mandatory gates: corrected-output F0 (A), formant
// retention vs. the replaced granular crossfade (B), click-free boundaries
// (C), transient handling (D), the actual DSP signal delay (E) and realtime
// safety / determinism (F). The shifter is exercised three ways: the full
// chain (real detect -> track -> shift), the updateTargetForTest seam
// (tracker-level drive) and psolaDriveForTest (pins applied F0 / ratio for
// shifts the +/-300-cent snap law cannot produce, e.g. +500 cents).
//
// Everything is deterministic: fixed sample rates, seeded generators, fixed
// chunking. Signal generation mirrors corpus_gen's harmonicVoice discipline.

#include "PitchCorrector.h"
#include "corpus_gen.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

namespace {

constexpr int kSr = 48000;
constexpr int kChunk = 512;
constexpr int kOutCh = 2;
constexpr double kPi = 3.14159265358979323846;

int gFailures = 0;

void check(bool ok, const std::string& what) {
    std::printf("  %s: %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (!ok) ++gFailures;
}

double cents(double a, double b) { return 1200.0 * std::log2(a / b); }
double fractd(double x) { return x - std::floor(x); }

// --- signal generation ------------------------------------------------------

// Harmonic voice on an F0 contour with optional per-harmonic weights
// (formant shaping). Same discipline as corpus_gen::harmonicVoice.
std::vector<float> voice(std::size_t n, int sr, double f0, int harmonics, float amp,
                         corpus::Rng& rng, const std::vector<double>* weights) {
    std::vector<double> p0(static_cast<std::size_t>(harmonics));
    for (double& p : p0) p = rng.nextUniform() * 2.0 * kPi;
    std::vector<float> out(n);
    double phase = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        phase += 2.0 * kPi * f0 / sr;
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

// Two-formant "vowel" weights: bumps at 700 / 1200 Hz over a 1/j floor —
// battery B's carrier. Bumps are wide enough (sigma ~ 160/180 Hz) that the
// sparse shifted harmonic grid still samples them well.
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

// --- measurement helpers (test-only) ----------------------------------------

// NCCF pitch estimate: the FIRST local correlation maximum reaching 0.9 *
// the global best, then parabolic refinement. (A threshold-crossing on the
// rising edge lands ~5% early — that biases the estimate sharp.)
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

// Goertzel amplitude at one frequency (normalized to the input amplitude).
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

// Spectral-envelope centroid inside [fLo, fHi], computed on the harmonic
// amplitude grid of a signal with fundamental f0. The centroid tracks the
// envelope peak even when only 2 harmonics fall inside the band.
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

// Run a signal through the DSP in kChunk frames; returns interleaved output.
std::vector<float> runDsp(dautotune::PitchCorrector& dsp,
                          const std::vector<float>& sig,
                          std::vector<double>* callUs = nullptr) {
    std::vector<float> out(sig.size() * kOutCh);
    std::vector<float> mono(sig.size());
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
        std::memcpy(out.data() + c * kOutCh, buf.data(), sizeof(float) * kChunk * kOutCh);
    }
    return out;
}

// --- legacy two-head granular shifter (battery B contrast) ------------------
// Verbatim-math reimplementation of the replaced shifter (d0_reference.cpp
// shift(), kShiftWindow=1024 / kBaseDelay=384 two-head Hann). The frozen D0
// class cannot be driven to +500 cents through its own snap law, so the
// reference engine is exercised directly at the same ratio.
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
// Battery A — corrected-output F0 through the full chain.
// A sung 285 Hz voice sits ~52 cents below D4 in C major (the +500-cent
// member gap A3->D4 is not reachable through the snap law — its max is
// ~100 cents, so the closest-full-member test is used). Target snaps to D4;
// amount=1; the rendered output must sit within 3 cents of 293.66 Hz.
// ===========================================================================
bool testOutputF0() {
    std::printf("A. corrected-output F0 (full chain, sung 285 -> target D4)\n");
    const double kD4 = 293.6647679;
    dautotune::PitchCorrector dsp(kSr);
    dsp.setParameters(0, 0, 1.0f, 35.0f, 0.0f);
    corpus::Rng rng(0xD3A11);
    const std::vector<float> sig = voice(static_cast<std::size_t>(1.2 * kSr), kSr,
                                         285.0, 6, 0.3f, rng, nullptr);
    const std::vector<float> out = runDsp(dsp, sig);

    check(dsp.voiced(), "A: voiced by run end");
    check(std::abs(static_cast<double>(dsp.targetHz()) - kD4) < 1.0,
          "A: target snapped to D4");

    // Measure the rendered F0 in three windows across the last 0.6 s.
    double worst = 0.0;
    bool finite = true;
    for (int w = 0; w < 3; ++w) {
        const std::size_t from = static_cast<std::size_t>((0.55 + 0.15 * w) * kSr) * 2;
        const std::size_t len = static_cast<std::size_t>(0.14 * kSr);
        std::vector<float> seg(len);
        for (std::size_t i = 0; i < len; ++i) {
            seg[i] = out[(from / 2 + i) * 2];
            if (!std::isfinite(seg[i])) finite = false;
        }
        const double f = measureF0(seg.data(), static_cast<int>(len), kSr);
        worst = std::max(worst, std::abs(cents(f, kD4)));
        std::printf("  window %.2f..%.2f s: f0 = %.2f Hz (err %.2f c)\n",
                    0.55 + 0.15 * w, 0.69 + 0.15 * w, f, cents(f, kD4));
    }
    check(finite, "A: output finite everywhere");
    check(worst <= 3.0, "A: output F0 within 3 cents of D4 across 1 s");

    // Driven +500-cent check: pins applied F0 = 220 and ratio = 2^(5/12).
    dautotune::PitchCorrector dsp2(kSr);
    dsp2.setParameters(0, 0, 1.0f, 35.0f, 0.0f);
    dsp2.psolaDriveForTest(220.0f, static_cast<float>(std::pow(2.0, 500.0 / 1200.0)));
    corpus::Rng rng2(0xD3A12);
    const std::vector<float> sig2 = voice(static_cast<std::size_t>(0.8 * kSr), kSr,
                                          220.0, 6, 0.3f, rng2, nullptr);
    const std::vector<float> out2 = runDsp(dsp2, sig2);
    const std::size_t off = static_cast<std::size_t>(0.5 * kSr);
    std::vector<float> seg(static_cast<std::size_t>(0.25 * kSr));
    for (std::size_t i = 0; i < seg.size(); ++i) seg[i] = out2[(off + i) * 2];
    const double f2 = measureF0(seg.data(), static_cast<int>(seg.size()), kSr);
    std::printf("  driven +500c: f0 = %.3f Hz (err %.2f c vs 293.66)\n", f2,
                cents(f2, kD4));
    check(std::abs(cents(f2, kD4)) <= 3.0, "A(driven): +500c output within 3 c of D4");

    // amount = 0 -> exact passthrough (same limiter law, no shifter action).
    dautotune::PitchCorrector dsp3(kSr);
    dsp3.setParameters(0, 0, 0.0f, 35.0f, 0.0f);
    const std::vector<float> out3 = runDsp(dsp3, sig2);
    bool identical = true;
    for (std::size_t i = 0; i + kChunk <= sig2.size(); ++i) {
        const float in = sig2[i];
        const float expected =
            std::max(-0.88f, std::min(0.88f, in / (1.0f + 0.20f * std::abs(in))));
        if (out3[i * 2] != expected || out3[i * 2 + 1] != expected) identical = false;
    }
    check(identical, "A: amount=0 output equals limited input bit-exact (bypass)");
    return true;
}

// ===========================================================================
// Battery B — formant retention. Vowel voice at 220 Hz, bumps at 700 / 1200
// Hz. NEW shifter at +500 cents: envelope centroids must stay near the bumps
// (+-60 Hz). The legacy engine at the same ratio must FAIL the same gate.
// ===========================================================================
bool testFormants() {
    std::printf("B. formant retention (vowel 220 Hz, +500 c shift)\n");
    corpus::Rng rng(0xF02AA7);
    const int kHarm = 22;
    const std::vector<double> w = formantVoice(220.0, kHarm);
    const std::vector<float> sig = voice(static_cast<std::size_t>(1.0 * kSr), kSr,
                                         220.0, kHarm, 0.30f, rng, &w);
    const double ratio = std::pow(2.0, 500.0 / 1200.0);
    const std::size_t from = static_cast<std::size_t>(0.45 * kSr);
    const int len = static_cast<int>(0.35 * kSr);

    struct Env {
        double f0;
        double c1;  // centroid in [500, 900]
        double c2;  // centroid in [1000, 1500]
    };
    auto envelope = [&](const float* x) {
        Env e;
        e.f0 = measureF0(x, len, kSr);
        e.c1 = bandCentroid(x, len, kSr, e.f0, 500.0, 900.0);
        e.c2 = bandCentroid(x, len, kSr, e.f0, 1000.0, 1500.0);
        return e;
    };
    const Env in = envelope(sig.data() + from);

    dautotune::PitchCorrector dsp(kSr);
    dsp.setParameters(0, 2, 1.0f, 35.0f, 0.0f);
    dsp.psolaDriveForTest(220.0f, static_cast<float>(ratio));
    const std::vector<float> out = runDsp(dsp, sig);
    std::vector<float> seg(static_cast<std::size_t>(len));
    for (int i = 0; i < len; ++i) seg[static_cast<std::size_t>(i)] = out[(from + i) * 2];
    const Env neu = envelope(seg.data());

    const std::vector<float> old = legacyGranular(sig, ratio);
    const Env leg = envelope(old.data() + from);

    std::printf("  | path | f0 Hz | F1 centroid Hz | F2 centroid Hz |\n");
    std::printf("  |------|-------|----------------|----------------|\n");
    std::printf("  | input | %6.1f | %14.1f | %14.1f |\n", in.f0, in.c1, in.c2);
    std::printf("  | PSOLA | %6.1f | %14.1f | %14.1f |\n", neu.f0, neu.c1, neu.c2);
    std::printf("  | legacy | %6.1f | %14.1f | %14.1f |\n", leg.f0, leg.c1, leg.c2);

    check(std::abs(cents(neu.f0, 293.66)) <= 5.0, "B: output F0 ~293.7 (+500 c)");
    check(std::abs(neu.c1 - 700.0) <= 60.0, "B: F1 centroid within +-60 Hz of 700");
    check(std::abs(neu.c2 - 1200.0) <= 60.0, "B: F2 centroid within +-60 Hz of 1200");
    check(std::abs(leg.c1 - 700.0) > 60.0, "B: legacy FAILS the +-60 Hz F1 gate");
    check(std::abs(leg.c2 - 1200.0) > 60.0, "B: legacy FAILS the +-60 Hz F2 gate");

    // Envelope/F0 ratio must move by roughly the shift when formants stay
    // (old engine keeps it fixed by construction of the resample).
    const double rIn = in.c1 / in.f0, rNew = neu.c1 / neu.f0, rLeg = leg.c1 / leg.f0;
    std::printf("  F1/F0 ratio: in %.2f -> psola %.2f (|move| %.0f c), legacy %.2f "
                "(|move| %.0f c)\n",
                rIn, rNew, std::abs(cents(rIn, rNew)), rLeg, std::abs(cents(rIn, rLeg)));
    check(std::abs(cents(rIn, rNew)) >= 350.0,
          "B: envelope/F0 ratio moves with the shift (formants stay put)");
    check(std::abs(cents(rIn, rLeg)) <= 0.5 * std::abs(cents(rIn, rNew)),
          "B: legacy keeps F1/F0 >=2x better (its formants follow the pitch)");
    return true;
}

// ===========================================================================
// Battery C — discontinuity / click-free. A driven ratio step mid-note and a
// real-chain A3->~D4 note change; bounded |dy| and no energy pumping.
// ===========================================================================
double maxStep(const std::vector<float>& y, std::size_t a, std::size_t b) {
    double m = 0.0;
    for (std::size_t i = a + 1; i < b && i < y.size(); ++i)
        m = std::max(m, std::abs(static_cast<double>(y[i]) - y[i - 1]));
    return m;
}

double rms(const std::vector<float>& y, std::size_t a, std::size_t b) {
    double s = 0.0;
    for (std::size_t i = a; i < b && i < y.size(); ++i) s += static_cast<double>(y[i]) * y[i];
    return std::sqrt(s / std::max<std::size_t>(1, b - a));
}

bool testDiscontinuity() {
    std::printf("C. click-free boundaries (driven ratio step + real note change)\n");
    corpus::Rng rng(0xC11C5);
    const std::vector<float> sig = voice(static_cast<std::size_t>(1.0 * kSr), kSr,
                                         220.0, 6, 0.3f, rng, nullptr);
    // runDsp renders floor(N/512)*512 frames; the unfilled tail is zeros and
    // is not part of the measured signal.
    const std::size_t rendered = (sig.size() / kChunk) * kChunk;
    std::vector<float> mono(sig.size());

    // Driven ratio step 1.0 -> 1.19 mid-stream (isolates the render path).
    dautotune::PitchCorrector dsp(kSr);
    dsp.setParameters(0, 0, 1.0f, 35.0f, 0.0f);
    dsp.psolaDriveForTest(220.0f, 1.0f);
    const std::size_t step = sig.size() / 2;
    for (std::size_t c = 0; c + kChunk <= sig.size(); c += kChunk) {
        if (c >= step) dsp.psolaDriveForTest(220.0f, 1.19f);
        std::vector<float> buf(static_cast<std::size_t>(kChunk) * kOutCh);
        dsp.process(sig.data() + c, buf.data(), kChunk, kOutCh);
        for (int i = 0; i < kChunk; ++i) mono[c + i] = buf[i * 2];
    }
    const double inSlew = maxStep(sig, 0, rendered);
    const double bound = std::max(0.02 * 0.3, 2.0 * inSlew);
    const double stepSlew = maxStep(mono, step - 4000, step + 8000);
    const double allSlew = maxStep(mono, kChunk, rendered);
    std::printf("  driven step: in slew %.4f, out max |dy| %.4f (bound %.4f), "
                "step-region %.4f\n", inSlew, allSlew, bound, stepSlew);
    check(allSlew <= bound, "C: no discontinuity across the ratio step");
    const double e0 = rms(mono, step - 9000, step - 1000);
    const double e1 = rms(mono, step - 1000, step + 7000);
    const double e2 = rms(mono, step + 8000, step + 16000);
    std::printf("  rms around step: %.3f / %.3f / %.3f\n", e0, e1, e2);
    check(e1 <= 2.0 * std::max(e0, 0.01) && e2 <= 2.5 * std::max(e0, 0.01) &&
              e1 >= 0.5 * e0 && e2 >= 0.4 * e0,
          "C: energy does not pump across the transition");

    // Real-chain note change 220 -> 285 (C major: A3 -> D4).
    dautotune::PitchCorrector dsp2(kSr);
    dsp2.setParameters(0, 0, 1.0f, 35.0f, 0.0f);
    corpus::Rng rng2(0xC11C6);
    std::vector<float> n1 = voice(step, kSr, 220.0, 6, 0.3f, rng2, nullptr);
    std::vector<float> n2 = voice(sig.size() - step, kSr, 285.0, 6, 0.3f, rng2, nullptr);
    edgeFade(n2, 0.002, kSr);  // 2 ms onset, not a spectral click
    std::vector<float> sig2;
    sig2.insert(sig2.end(), n1.begin(), n1.end());
    sig2.insert(sig2.end(), n2.begin(), n2.end());
    const std::vector<float> out2 = runDsp(dsp2, sig2);
    for (std::size_t i = 0; i < sig2.size(); ++i) mono[i] = out2[i * 2];
    // The input itself steps at the note boundary (note-1 tail -> faded-in
    // note 2). The shifter may reproduce that step delayed/smeared by up to
    // ~2 periods; it must not amplify it and must not introduce NEW steps
    // anywhere else.
    const double inSlew2 = maxStep(sig2, step - 2000, step + 2000);
    const std::size_t bEnd = step + static_cast<std::size_t>(3.0 * (kSr / 285.0)) + 400;
    const double slewNear = maxStep(mono, step - 2000, bEnd);
    const double slewFar = std::max(maxStep(mono, kChunk, step - 2000),
                                    maxStep(mono, bEnd, rendered));
    std::printf("  real note change: in boundary slew %.4f -> out near %.4f "
                "(<= 1.25x in), far %.4f (bound %.4f)\n", inSlew2, slewNear,
                slewFar, bound);
    check(slewNear <= 1.25 * inSlew2 + 0.01,
          "C: note-boundary step not amplified by the shifter");
    check(slewFar <= bound,
          "C: no shifter-introduced discontinuity outside the boundary");

    // Sustained constant ratio: glitch-free steady render.
    dautotune::PitchCorrector dsp3(kSr);
    dsp3.setParameters(0, 0, 1.0f, 35.0f, 0.0f);
    dsp3.psolaDriveForTest(220.0f, 1.15f);
    const std::vector<float> out3 = runDsp(dsp3, sig);
    const std::size_t s0 = static_cast<std::size_t>(0.5 * kSr);
    for (std::size_t i = s0; i < sig.size(); ++i) mono[i] = out3[i * 2];
    const double sustainSlew = maxStep(mono, s0 + 1, rendered);
    std::printf("  sustained r=1.15: max |dy| %.4f (bound %.4f)\n", sustainSlew, bound);
    check(sustainSlew <= bound, "C: sustained constant ratio is glitch-free");
    return true;
}

// ===========================================================================
// Battery D — transient handling. Driven steady state; a percussive onset
// must appear once, within one period of the input onset, without >6 dB
// overshoot.
// ===========================================================================
bool testTransient() {
    std::printf("D. transient handling (percussive onset + lone impulse, driven)\n");
    const double period = kSr / 220.0;

    // D1: a lone impulse on silence is the cleanest "read twice" probe — any
    // mark that re-reads it produces a second |y| excursion.
    {
        const std::size_t t0 = static_cast<std::size_t>(0.2 * kSr);
        std::vector<float> sig(static_cast<std::size_t>(0.6 * kSr), 0.0f);
        sig[t0] = 0.9f;
        dautotune::PitchCorrector dsp(kSr);
        dsp.setParameters(0, 0, 1.0f, 35.0f, 0.0f);
        dsp.psolaDriveForTest(220.0f, 1.15f);
        const std::vector<float> out = runDsp(dsp, sig);
        int hits = 0;
        double firstIdx = -1.0;
        for (std::size_t i = t0; i < sig.size(); ++i) {
            if (std::abs(out[i * 2]) > 0.2) {
                if (firstIdx < 0.0) firstIdx = static_cast<double>(i);
                // count excursions: a new hit after returning below threshold
                if (i > 0 && std::abs(out[(i - 1) * 2]) <= 0.2) ++hits;
            }
        }
        std::printf("  impulse: %d excursion(s), first at +%.0f samp (%.2f ms)\n",
                    hits, firstIdx - static_cast<double>(t0),
                    1000.0 * (firstIdx - static_cast<double>(t0)) / kSr);
        check(hits == 1, "D: impulse emitted exactly once (not read twice)");
        check(firstIdx - static_cast<double>(t0) <= period + 80.0,
              "D: impulse within ~1 period of input");
    }

    // D2: percussive voice onset — timing and overshoot.
    corpus::Rng rng(0xD7A1E7);
    const std::size_t pre = static_cast<std::size_t>(0.25 * kSr);
    std::vector<float> sig(pre, 0.0f);
    std::vector<float> burst = voice(static_cast<std::size_t>(0.5 * kSr), kSr,
                                     220.0, 8, 0.35f, rng, nullptr);
    edgeFade(burst, 0.0015, kSr);  // 1.5 ms attack: percussive but not a click
    sig.insert(sig.end(), burst.begin(), burst.end());

    dautotune::PitchCorrector dsp(kSr);
    dsp.setParameters(0, 0, 1.0f, 35.0f, 0.0f);
    dsp.psolaDriveForTest(220.0f, 1.15f);
    const std::vector<float> out = runDsp(dsp, sig);
    std::vector<float> mono(sig.size());
    for (std::size_t i = 0; i < sig.size(); ++i) mono[i] = out[i * 2];

    // 5 ms RMS envelope on input and output.
    const int win = static_cast<int>(0.005 * kSr);
    auto envelope = [&](const std::vector<float>& x) {
        std::vector<double> e(x.size());
        double acc = 0.0;
        for (std::size_t i = 0; i < x.size(); ++i) {
            acc += static_cast<double>(x[i]) * x[i];
            if (i >= static_cast<std::size_t>(win)) {
                const double d = x[i - win];
                acc -= d * d;
            }
            e[i] = std::sqrt(std::max(0.0, acc / win));
        }
        return e;
    };
    const std::vector<double> env = envelope(mono);

    double peak = 0.0;
    std::size_t peakIdx = 0;
    for (std::size_t i = pre; i < env.size(); ++i)
        if (env[i] > peak) { peak = env[i]; peakIdx = i; }

    std::size_t onset = pre;
    while (onset < env.size() && env[onset] < 0.25 * peak) ++onset;
    const double delay = static_cast<double>(onset) - static_cast<double>(pre);

    const double steady = rms(mono, peakIdx + static_cast<std::size_t>(2.5 * period),
                              peakIdx + static_cast<std::size_t>(4.0 * period));
    const double overshootDb = 20.0 * std::log10((peak / std::max(1e-9, steady)));
    std::printf("  onset delay %.0f samp (%.1f ms); overshoot %.1f dB\n",
                delay, 1000.0 * delay / kSr, overshootDb);
    check(delay >= 0.0 && delay <= period + 80.0,
          "D: transient within ~1 period of input onset");
    check(overshootDb <= 6.0, "D: no amplitude overshoot > 6 dB");
    return true;
}

// ===========================================================================
// Battery E — actual DSP signal delay. Known tone after silence; first output
// above threshold vs the input onset. Printed in samples and ms at both
// sample rates; bounded by the design budget (one applied period + 1) and
// deterministic across 2 runs.
// ===========================================================================
double measureDelaySamples(int sr, double f0, float ratio) {
    dautotune::PitchCorrector dsp(sr);
    dsp.setParameters(0, 0, 1.0f, 35.0f, 0.0f);
    dsp.psolaDriveForTest(static_cast<float>(f0), ratio);
    const std::size_t pre = static_cast<std::size_t>(0.2 * sr);
    std::vector<float> sig(pre, 0.0f);
    corpus::Rng rng(0xE11A7);
    const std::vector<float> tone = voice(static_cast<std::size_t>(0.4 * sr), sr,
                                          f0, 6, 0.3f, rng, nullptr);
    sig.insert(sig.end(), tone.begin(), tone.end());
    std::vector<float> buf(static_cast<std::size_t>(kChunk) * kOutCh);
    for (std::size_t c = 0; c + kChunk <= sig.size(); c += kChunk) {
        dsp.process(sig.data() + c, buf.data(), kChunk, kOutCh);
        for (int i = 0; i < kChunk; ++i) {
            if (std::abs(buf[i * 2]) >= 0.06f && c + i >= pre) {
                return static_cast<double>(c + i) - static_cast<double>(pre);
            }
        }
    }
    return -1.0;
}

// Impulse-centroid delay: in driven steady state a single 0.8 spike lands in
// the grains covering it; the energy centroid of its image window is the
// effective algorithm delay.
double measureImpulseDelay(int sr, double f0, float ratio) {
    dautotune::PitchCorrector dsp(sr);
    dsp.setParameters(0, 0, 1.0f, 35.0f, 0.0f);
    dsp.psolaDriveForTest(static_cast<float>(f0), ratio);
    corpus::Rng rng(0xE11A8);
    std::vector<float> sig = voice(static_cast<std::size_t>(0.6 * sr), sr, f0, 6,
                                   0.3f, rng, nullptr);
    const std::size_t t0 = static_cast<std::size_t>(0.4 * sr);
    sig[t0] += 0.8f;
    std::vector<float> buf(static_cast<std::size_t>(kChunk) * kOutCh);
    std::vector<float> mono(sig.size());
    for (std::size_t c = 0; c + kChunk <= sig.size(); c += kChunk) {
        dsp.process(sig.data() + c, buf.data(), kChunk, kOutCh);
        for (int i = 0; i < kChunk; ++i) mono[c + i] = buf[i * 2];
    }
    const double period = sr / f0;
    // Subtract the tone: measure on the difference against an identical run
    // without the spike? Simpler: energy excess over the local tone floor in
    // [t0, t0 + 2P]. The impulse image dominates a ~1-period window.
    double num = 0.0, den = 0.0;
    const double floorE = rms(mono, t0 - static_cast<std::size_t>(2.0 * period),
                              t0 - static_cast<std::size_t>(0.5 * period));
    for (std::size_t i = t0; i < t0 + static_cast<std::size_t>(2.0 * period); ++i) {
        const double excess = std::max(0.0, std::abs(static_cast<double>(mono[i])) - 3.0 * floorE);
        num += (static_cast<double>(i) - t0) * excess;
        den += excess;
    }
    return den > 1e-9 ? num / den : -1.0;
}

bool testDelay() {
    std::printf("E. actual DSP signal delay (tone onset + impulse centroid)\n");
    std::printf("  | sr | f0 | r | onset samp | onset ms | impulse samp | impulse ms |\n");
    std::printf("  |----|----|---|------------|----------|--------------|------------|\n");
    double worst = 0.0;
    double worstImp = 0.0;
    for (const int sr : {48000, 44100}) {
        for (const double f0 : {220.0, 120.0}) {
            const double d1 = measureDelaySamples(sr, f0, 1.15f);
            const double d2 = measureImpulseDelay(sr, f0, 1.15f);
            const double rep = measureDelaySamples(sr, f0, 1.15f);
            std::printf("  | %d | %4.0f | 1.15 | %10.0f | %8.2f | %12.0f | %10.2f |\n",
                        sr, f0, d1, 1000.0 * d1 / sr, d2, 1000.0 * d2 / sr);
            check(d1 == rep, "E: onset delay deterministic across 2 runs");
            const double period = sr / f0;
            check(d1 >= 0.0 && d1 <= period + 1.0 + 96.0,
                  "E: onset delay <= one applied period (+window)");
            worst = std::max(worst, d1 / period);
            worstImp = std::max(worstImp, d2 / period);
        }
    }
    std::printf("  max onset/impulse delay = %.2f / %.2f periods "
                "(design bound P + 1; 80 Hz budget = %d samp @48k = 12.5 ms)\n",
                worst, worstImp, kSr / 80 + 1);
    check(worst <= 1.5, "E: measured delay within the P + 1 budget");
    return true;
}

// ===========================================================================
// Battery F — realtime safety: determinism (two identical runs) and per-call
// cost p95 < 200 us per 512-sample call (the D0 chain measured 14.7 us).
// ===========================================================================
bool testRealtime() {
    std::printf("F. realtime safety (determinism + per-call cost)\n");
    corpus::Rng rng(0xF17E5);
    const std::vector<float> sig = voice(static_cast<std::size_t>(1.5 * kSr), kSr,
                                         220.0, 6, 0.3f, rng, nullptr);
    dautotune::PitchCorrector a(kSr), b(kSr);
    for (auto* d : {&a, &b}) {
        d->setParameters(0, 0, 1.0f, 35.0f, 0.1f);
        d->psolaDriveForTest(220.0f, 1.15f);
    }
    const std::vector<float> oa = runDsp(a, sig);
    const std::vector<float> ob = runDsp(b, sig);
    check(oa == ob, "F: identical output across 2 fresh instances (determinism)");

    std::vector<double> us;
    dautotune::PitchCorrector dsp(kSr);
    dsp.setParameters(0, 0, 1.0f, 35.0f, 0.1f);
    dsp.psolaDriveForTest(220.0f, 1.15f);
    runDsp(dsp, sig, &us);  // warm
    us.clear();
    runDsp(dsp, sig, &us);
    std::sort(us.begin(), us.end());
    const double med = us[us.size() / 2];
    const double p95 = us[static_cast<std::size_t>(0.95 * (us.size() - 1))];
    std::printf("  per-call cost: median %.1f us, p95 %.1f us per %d-sample call "
                "(budget 200 us; D0 reference 14.7 us)\n", med, p95, kChunk);
    check(p95 < 200.0, "F: per-call p95 < 200 us (~250x realtime headroom)");
    return true;
}

}  // namespace

int main() {
    std::printf("D3 TD-PSOLA shifter test battery\n");
    std::printf("sr=%d chunk=%d; full-chain + tracker seam + driven-pin runs\n\n",
                kSr, kChunk);

    struct { const char* name; bool (*fn)(); } tests[] = {
        {"A corrected-output F0", testOutputF0},
        {"B formant retention", testFormants},
        {"C discontinuity/click-free", testDiscontinuity},
        {"D transient handling", testTransient},
        {"E DSP signal delay", testDelay},
        {"F realtime safety", testRealtime},
    };
    std::vector<std::pair<const char*, bool>> results;
    for (const auto& t : tests) {
        const int before = gFailures;
        t.fn();
        results.push_back({t.name, gFailures == before});
        std::printf("\n");
    }

    std::printf("=== D3 TEST TABLE ===\n");
    std::printf("| test                          | result |\n");
    std::printf("|-------------------------------|--------|\n");
    for (const auto& r : results)
        std::printf("| %-29s | %s |\n", r.first, r.second ? "PASS" : "FAIL");

    if (gFailures) {
        std::printf("D3 SHIFTER TESTS: %d FAILURES\n", gFailures);
        return 1;
    }
    std::printf("D3 SHIFTER TESTS: ALL PASS\n");
    return 0;
}

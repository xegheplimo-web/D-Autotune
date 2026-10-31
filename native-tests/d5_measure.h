#pragma once

// D5 measurement helpers — the same math the D3/D4 batteries use, lifted into
// a shared header so test_d5_harness (synthetic unit tests), test_d5_realvoice
// (R1–R5) and d5_listening_pack share one implementation. Everything here is
// test-only: heap allocation is fine, nothing runs on the RT audio thread.
//
// Inherited definitions (do not re-invent):
//  - normalized-crosscorrelation F0 estimate: identical to test_d3/d4 measureF0
//  - formant band centroids: Goertzel on the signal's own F0 harmonic grid,
//    F1 [500,900] / F2 [1000,1500], gate +-60 Hz (D3-B / D4-B)
//  - click/step: max |y[n]-y[n-1]|, D3 rule out <= 1.25*in + 0.01 near a
//    boundary, no amplification elsewhere (battery-C machinery)
//  - impulse-centroid delay: energy centroid of a driven spike's image
//    (test_d3 battery-E); D4 class expectation 2.27-4.17 ms
//  - pump depth (new for D5): dB envelope over non-overlapping 50 ms RMS
//    windows -> per-segment std + p95 |dEnv| of consecutive windows

#include "PitchCorrector.h"
#include "corpus_gen.h"
#include "corpus_loader.h"
#include "metrics.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <functional>
#include <utility>
#include <vector>

namespace d5m {

constexpr int kChunk = 512;
constexpr int kOutCh = 2;
constexpr double kPi = 3.14159265358979323846;

inline double cents(double a, double b) { return 1200.0 * std::log2(a / b); }
inline double etHz(int midiNote) {
    return 440.0 * std::pow(2.0, (static_cast<double>(midiNote) - 69.0) / 12.0);
}

// --- synthesis (identical math to test_d4_e2e voice()) -------------------------

inline std::function<double(double)> constF(double f0) {
    return [f0](double) { return f0; };
}

inline std::vector<float> voice(std::size_t n, int sr,
                                const std::function<double(double)>& f0, int harmonics,
                                float amp, corpus::Rng& rng,
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
            s += w * std::sin(static_cast<double>(j) * phase +
                              p0[static_cast<std::size_t>(j - 1)]);
        }
        out[i] = amp * static_cast<float>(s);
    }
    return out;
}

// Two-formant envelope bumps at 700 / 1200 over a 1/j floor (D3-B layout).
inline std::vector<double> formantVoice(double f0, int harmonics) {
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

inline void edgeFade(std::vector<float>& v, double fadeSec, int sr) {
    const std::size_t e = static_cast<std::size_t>(fadeSec * sr);
    for (std::size_t i = 0; i < e && 2 * i < v.size(); ++i) {
        const double g = 0.5 - 0.5 * std::cos(kPi * static_cast<double>(i) / e);
        v[i] = static_cast<float>(v[i] * g);
        v[v.size() - 1 - i] = static_cast<float>(v[v.size() - 1 - i] * g);
    }
}

// --- full-chain render (identical to test_d4 runDsp + monoOf) -------------------

// Runs the live chain at the clip's native rate: amount 1.0, retune 35 ms,
// C major, reverb 0 — the D4 measurement settings. Frames capture the
// detector trace per chunk for the informational det% report.
struct Rendered {
    std::vector<float> mono;             // left channel of the stereo render
    std::vector<metrics::Frame> frames;  // one per processed chunk
    std::vector<float> tgtHz;            // dsp.targetHz() per chunk, aligned with frames
    std::size_t rendered = 0;            // samples actually processed
};

inline Rendered renderChain(const std::vector<float>& sig, int sr,
                            const std::vector<d5corpus::NoteSeg>& notes,
                            bool withFrames) {
    dautotune::PitchCorrector dsp(sr);
    dsp.setParameters(0, 0, 1.0f, 35.0f, 0.0f);
    Rendered r;
    r.mono.assign(sig.size(), 0.0f);
    std::vector<float> buf(static_cast<std::size_t>(kChunk) * kOutCh);
    for (std::size_t c = 0; c + kChunk <= sig.size(); c += kChunk) {
        dsp.process(sig.data() + c, buf.data(), kChunk, kOutCh);
        for (int i = 0; i < kChunk; ++i) r.mono[c + i] = buf[static_cast<std::size_t>(i) * kOutCh];
        if (withFrames) {
            const double tC = (static_cast<double>(c) + 0.5 * kChunk) / sr;
            metrics::Frame f;
            f.estHz = dsp.detectedHz();
            f.estVoiced = dsp.voiced();
            f.gtHz = 0.0f;
            for (const d5corpus::NoteSeg& s : notes) {
                if (tC >= s.tStart && tC < s.tEnd) {
                    f.gtHz = static_cast<float>(s.f0Hz);
                    break;
                }
            }
            r.frames.push_back(f);
            r.tgtHz.push_back(dsp.targetHz());
        }
    }
    r.rendered = (sig.size() / kChunk) * kChunk;
    r.mono.resize(r.rendered);
    return r;
}

// --- spectral / time helpers (identical to test_d4_e2e) -------------------------

inline double measureF0(const float* x, int n, int sr, double loHz = 60.0,
                        double hiHz = 600.0) {
    const int minLag = static_cast<int>(sr / hiHz);
    const int maxLag = std::min(n - 2, static_cast<int>(sr / loHz));
    if (maxLag <= minLag) return 0.0;
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
        if (c > best) {
            best = c;
            bestLag = lag;
        }
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
        const double rr = nc[static_cast<std::size_t>(lag + 1)];
        const double d = l - 2.0 * m + rr;
        if (std::abs(d) > 1e-12)
            refined += std::max(-0.5, std::min(0.5, 0.5 * (l - rr) / d));
    }
    return sr / refined;
}

inline double goertzel(const float* x, int n, int sr, double f) {
    const double w = 2.0 * std::cos(2.0 * kPi * f / sr);
    double s0 = 0.0, s1 = 0.0, s2 = 0.0;
    for (int i = 0; i < n; ++i) {
        s0 = static_cast<double>(x[i]) + w * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    return 2.0 * std::sqrt(s1 * s1 + s2 * s2 - w * s1 * s2) / n;
}

// Spectral-envelope centroid inside [fLo,fHi] on the f0 harmonic grid (D3-B).
inline double bandCentroid(const float* x, int n, int sr, double f0, double fLo,
                           double fHi) {
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

inline double maxStep(const std::vector<float>& y, std::size_t a, std::size_t b) {
    double m = 0.0;
    for (std::size_t i = a + 1; i < b && i < y.size(); ++i)
        m = std::max(m, std::abs(static_cast<double>(y[i]) - y[i - 1]));
    return m;
}

// --- envelope pump metric -------------------------------------------------------

// Non-overlapping winSec RMS windows -> per-window level in dBFS.
inline std::vector<double> envDbSeries(const float* x, std::size_t n, int sr,
                                       double winSec = 0.05) {
    const std::size_t w = std::max<std::size_t>(16, static_cast<std::size_t>(winSec * sr));
    std::vector<double> env;
    for (std::size_t a = 0; a + w <= n; a += w) {
        double acc = 0.0;
        for (std::size_t i = a; i < a + w; ++i)
            acc += static_cast<double>(x[i]) * x[i];
        env.push_back(20.0 * std::log10(std::sqrt(acc / static_cast<double>(w)) + 1e-9));
    }
    return env;
}

struct PumpDepth {
    double stdDb = 0.0;      // population std of the windowed dB envelope
    double p95DeltaDb = 0.0; // p95 of |env[i]-env[i-1]| over consecutive windows
    std::size_t windows = 0;
};

inline PumpDepth pumpDepth(const float* x, std::size_t n, int sr, double winSec = 0.05) {
    const std::vector<double> env = envDbSeries(x, n, sr, winSec);
    PumpDepth p;
    p.windows = env.size();
    if (env.size() < 2) return p;
    double mean = 0.0;
    for (const double e : env) mean += e;
    mean /= static_cast<double>(env.size());
    double var = 0.0;
    std::vector<double> deltas(env.size() - 1);
    for (std::size_t i = 0; i + 1 < env.size(); ++i) {
        var += (env[i] - mean) * (env[i] - mean);
        deltas[i] = std::abs(env[i + 1] - env[i]);
    }
    var += (env.back() - mean) * (env.back() - mean);
    p.stdDb = std::sqrt(var / static_cast<double>(env.size()));
    const metrics::Stats s = metrics::summarize(std::move(deltas));
    p.p95DeltaDb = s.p95;
    return p;
}

// --- delay probes (test_d3 battery-E math) ---------------------------------------

// Onset delay: samples from input onset to first |out| >= 0.06 (D3/D4 rule).
inline double onsetDelaySamp(const std::vector<float>& mono, std::size_t pre) {
    for (std::size_t i = pre; i < mono.size(); ++i)
        if (std::abs(mono[i]) >= 0.06f) return static_cast<double>(i - pre);
    return -1.0;
}

// Impulse-centroid delay: driven steady state, single 0.8 spike; energy
// centroid of the spike's image within [t0, t0+2P]. Uses the difference
// against an identical render WITHOUT the spike (the alternative the D3 code
// itself sketched) — deterministic output makes the subtraction exact, which
// removes the 3*floorE threshold that goes blind at 44.1 kHz / 120 Hz (the
// spike image smears below the tone-floor gate there). Cross-checked at
// 48 kHz/220 Hz against the D3 floor-excess method: ~180 samp both ways.
inline double impulseCentroidDelaySamp(int sr, double f0, float ratio) {
    auto render = [&](bool spike) {
        dautotune::PitchCorrector dsp(sr);
        dsp.setParameters(0, 0, 1.0f, 35.0f, 0.0f);
        dsp.psolaDriveForTest(static_cast<float>(f0), ratio);
        corpus::Rng rng(0xE11A8);
        std::vector<float> sig = voice(static_cast<std::size_t>(0.6 * sr), sr,
                                       constF(f0), 6, 0.3f, rng, nullptr);
        const std::size_t t0 = static_cast<std::size_t>(0.4 * sr);
        if (spike) sig[t0] += 0.8f;
        std::vector<float> buf(static_cast<std::size_t>(kChunk) * kOutCh);
        std::vector<float> mono(sig.size());
        for (std::size_t c = 0; c + kChunk <= sig.size(); c += kChunk) {
            dsp.process(sig.data() + c, buf.data(), kChunk, kOutCh);
            for (int i = 0; i < kChunk; ++i)
                mono[c + i] = buf[static_cast<std::size_t>(i) * kOutCh];
        }
        return mono;
    };
    const std::vector<float> with = render(true);
    const std::vector<float> without = render(false);
    const std::size_t t0 = static_cast<std::size_t>(0.4 * sr);
    const double period = sr / f0;
    double num = 0.0, den = 0.0;
    for (std::size_t i = t0; i < t0 + static_cast<std::size_t>(2.0 * period); ++i) {
        const double e = std::abs(static_cast<double>(with[i]) - without[i]);
        num += (static_cast<double>(i) - t0) * e;
        den += e;
    }
    return den > 1e-9 ? num / den : -1.0;
}

// --- annotation helpers ---------------------------------------------------------

// Middle-60% window of a note segment, in samples, clamped to the clip.
inline std::pair<std::size_t, std::size_t> mid60(const d5corpus::NoteSeg& s, int sr,
                                               std::size_t maxN) {
    const double d = s.tEnd - s.tStart;
    std::size_t a = static_cast<std::size_t>((s.tStart + 0.2 * d) * sr);
    std::size_t b = static_cast<std::size_t>((s.tStart + 0.8 * d) * sr);
    a = std::min(a, maxN);
    b = std::min(b, maxN);
    return {a, b};
}

}  // namespace d5m

#include "corpus_gen.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <functional>
#include <utility>

namespace corpus {
namespace {

constexpr double kPi = 3.14159265358979323846;

using F0Fn = std::function<double(double)>;  // t seconds -> instantaneous F0 Hz (0 = unvoiced)

std::size_t framesFor(double seconds, int sr) {
    return static_cast<std::size_t>(seconds * static_cast<double>(sr));
}

// Per-item PRNG: seeded from (seed, sampleRate, item name) so every item is
// independent of build order and identical across runs.
Rng itemRng(std::uint64_t seed, int sr, const std::string& name) {
    std::uint64_t h = 1469598103934665603ull;
    for (const char c : name) {
        h ^= static_cast<std::uint8_t>(c);
        h *= 1099511628211ull;
    }
    h ^= seed + static_cast<std::uint64_t>(sr);
    h *= 1099511628211ull;
    return Rng(h ^ 0x9E3779B97F4A7C15ull);
}

// --- signal generators ------------------------------------------------------

std::vector<float> sine(std::size_t n, int sr, double f0, float amp) {
    std::vector<float> out(n);
    for (std::size_t i = 0; i < n; ++i)
        out[i] = amp * static_cast<float>(std::sin(2.0 * kPi * f0 * static_cast<double>(i) / sr));
    return out;
}

// Sum of `harmonics` partials with 1/j rolloff and fixed random phases.
// Phase is integrated from the instantaneous F0, so a time-varying f0Fn
// produces consistent harmonic vibrato/portamento. `weights` optionally
// multiplies each 1/j amplitude (formant envelope shaping).
std::vector<float> harmonicVoice(std::size_t n, int sr, const F0Fn& f0Fn, int harmonics,
                                 float amp, Rng& rng, const std::vector<double>* weights) {
    std::vector<double> phase0(static_cast<std::size_t>(harmonics));
    for (auto& p : phase0) p = rng.nextUniform() * 2.0 * kPi;
    std::vector<float> out(n);
    double phase = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        phase += 2.0 * kPi * f0Fn(static_cast<double>(i) / sr) / sr;
        double s = 0.0;
        for (int j = 1; j <= harmonics; ++j) {
            double w = 1.0 / static_cast<double>(j);
            if (weights) w *= (*weights)[static_cast<std::size_t>(j - 1)];
            s += w * std::sin(static_cast<double>(j) * phase + phase0[static_cast<std::size_t>(j - 1)]);
        }
        out[i] = amp * static_cast<float>(s);
    }
    return out;
}

std::vector<float> whiteNoise(std::size_t n, Rng& rng, double stddev) {
    std::vector<float> out(n);
    for (auto& v : out) v = static_cast<float>(stddev * rng.nextGaussian());
    return out;
}

// Band-limited "hiss": white noise through an RBJ bandpass biquad (~5 kHz,
// sibilant region), rescaled to the requested standard deviation.
std::vector<float> bandHiss(std::size_t n, int sr, Rng& rng, double stddev,
                            double centerHz = 5000.0, double q = 2.5) {
    const double w0 = 2.0 * kPi * centerHz / sr;
    const double alpha = std::sin(w0) / (2.0 * q);
    const double b0 = alpha, b2 = -alpha;
    const double a0 = 1.0 + alpha;
    const double a1 = -2.0 * std::cos(w0) / a0;
    const double a2 = (1.0 - alpha) / a0;
    const double bb0 = b0 / a0, bb2 = b2 / a0;
    double x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0, power = 0.0;
    std::vector<float> out(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double x = rng.nextGaussian();
        const double y = bb0 * x + bb2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1; x1 = x; y2 = y1; y1 = y;
        out[i] = static_cast<float>(y);
        if (i > n / 2) power += y * y;  // measure steady-state power
    }
    const double rms = std::sqrt(power / static_cast<double>(n - n / 2 - 1));
    if (rms > 1e-12) {
        const double g = stddev / rms;
        for (auto& v : out) v = static_cast<float>(v * g);
    }
    return out;
}

// /a/-like formant envelope (F1..F3) sampled at each harmonic frequency.
std::vector<double> formantWeights(double f0, int harmonics) {
    auto g = [](double f, double c, double b) {
        const double d = (f - c) / b;
        return std::exp(-0.5 * d * d);
    };
    std::vector<double> w(static_cast<std::size_t>(harmonics));
    for (int j = 1; j <= harmonics; ++j) {
        const double f = static_cast<double>(j) * f0;
        w[static_cast<std::size_t>(j - 1)] =
            g(f, 730.0, 90.0) + 0.65 * g(f, 1090.0, 110.0) + 0.40 * g(f, 2440.0, 130.0);
    }
    return w;
}

// --- helpers ----------------------------------------------------------------

Item makeItem(std::string name, int sr, int chunk, std::vector<float> samples, const F0Fn& gt) {
    Item it;
    it.name = std::move(name);
    it.sampleRate = sr;
    it.chunk = chunk;
    it.samples = std::move(samples);
    const std::size_t nChunks = it.samples.size() / static_cast<std::size_t>(chunk);
    it.gtF0Hz.resize(nChunks);
    for (std::size_t c = 0; c < nChunks; ++c) {
        const double tCenter = (static_cast<double>(c) + 0.5) * chunk / sr;
        it.gtF0Hz[c] = static_cast<float>(gt(tCenter));
    }
    return it;
}

F0Fn constF0(double f0) { return [f0](double) { return f0; }; }

F0Fn vibratoF0(double f0, double depth, double rateHz) {
    return [f0, depth, rateHz](double t) {
        return f0 * (1.0 + depth * std::sin(2.0 * kPi * rateHz * t));
    };
}

// Piecewise-linear F0 through (time, Hz) knots; flat outside the range.
F0Fn rampF0(const std::vector<std::pair<double, double>>& knots) {
    return [knots](double t) {
        if (t <= knots.front().first) return knots.front().second;
        for (std::size_t i = 1; i < knots.size(); ++i) {
            if (t < knots[i].first) {
                const double span = knots[i].first - knots[i - 1].first;
                const double a = span > 0.0 ? (t - knots[i - 1].first) / span : 1.0;
                return knots[i - 1].second + a * (knots[i].second - knots[i - 1].second);
            }
        }
        return knots.back().second;
    };
}

void fnvAppend(std::uint64_t& h, const void* data, std::size_t bytes) {
    const auto* p = static_cast<const std::uint8_t*>(data);
    for (std::size_t i = 0; i < bytes; ++i) {
        h ^= p[i];
        h *= 1099511628211ull;
    }
}

}  // namespace

Rng::Rng(std::uint64_t seed) : state_(seed ? seed : 0x9E3779B97F4A7C15ull) {}

std::uint64_t Rng::nextU64() {
    state_ ^= state_ >> 12;
    state_ ^= state_ << 25;
    state_ ^= state_ >> 27;
    return state_ * 2685821657736338717ull;
}

double Rng::nextUniform() {
    return static_cast<double>(nextU64() >> 11) * (1.0 / 9007199254740992.0);
}

double Rng::nextGaussian() {
    if (spareReady_) {
        spareReady_ = false;
        return spare_;
    }
    double u, v, s;
    do {
        u = 2.0 * nextUniform() - 1.0;
        v = 2.0 * nextUniform() - 1.0;
        s = u * u + v * v;
    } while (s >= 1.0 || s <= 1e-30);
    const double m = std::sqrt(-2.0 * std::log(s) / s);
    spare_ = v * m;
    spareReady_ = true;
    return u * m;
}

std::uint64_t hash(const Item& item) {
    std::uint64_t h = 1469598103934665603ull;
    fnvAppend(h, item.name.data(), item.name.size());
    fnvAppend(h, &item.sampleRate, sizeof(item.sampleRate));
    fnvAppend(h, &item.chunk, sizeof(item.chunk));
    if (!item.samples.empty())
        fnvAppend(h, item.samples.data(), item.samples.size() * sizeof(float));
    if (!item.gtF0Hz.empty())
        fnvAppend(h, item.gtF0Hz.data(), item.gtF0Hz.size() * sizeof(float));
    return h;
}

std::vector<Item> build(int sr, int chunk, std::uint64_t seed) {
    std::vector<Item> items;
    constexpr double kF0s[] = {110.0, 220.0, 329.63, 440.0, 523.25, 784.0};
    const char* kNotes[] = {"A2_110", "A3_220", "E4_329", "A4_440", "C5_523", "G5_784"};

    // 1) Pure sines — exact-F0 ground truth.
    for (int n = 0; n < 6; ++n) {
        const std::string name = std::string("sine_") + kNotes[n];
        items.push_back(makeItem(name, sr, chunk,
                                 sine(framesFor(1.0, sr), sr, kF0s[n], 0.3f),
                                 constF0(kF0s[n])));
    }

    // 2) Harmonic stacks, 1/j rolloff — octave-clarity coverage.
    for (const int k : {1, 2, 5, 8}) {
        for (int n = 0; n < 6; ++n) {
            const std::string name = "harm" + std::to_string(k) + "_" + kNotes[n];
            Rng rng = itemRng(seed, sr, name);
            items.push_back(makeItem(
                name, sr, chunk,
                harmonicVoice(framesFor(1.0, sr), sr, constF0(kF0s[n]), k, 0.3f, rng, nullptr),
                constF0(kF0s[n])));
        }
    }

    // 3) Vibrato contours on a 6-harmonic voice at A3.
    for (const double d : {0.02, 0.05}) {
        for (const double fv : {5.0, 7.0}) {
            const std::string name = "vib_220_d0" + std::to_string(static_cast<int>(d * 100)) +
                                     "_f" + std::to_string(static_cast<int>(fv));
            Rng rng = itemRng(seed, sr, name);
            const F0Fn f0 = vibratoF0(220.0, d, fv);
            items.push_back(makeItem(name, sr, chunk,
                                     harmonicVoice(framesFor(1.2, sr), sr, f0, 6, 0.3f, rng, nullptr),
                                     f0));
        }
    }

    // 4) Portamento: linear F0 ramps 220 -> 330 -> 440 Hz on a 6-harmonic voice.
    {
        const F0Fn f0 = rampF0({{0.0, 220.0}, {0.40, 220.0}, {0.55, 330.0},
                                {0.90, 330.0}, {1.05, 440.0}, {1.45, 440.0}});
        Rng rng = itemRng(seed, sr, "porta_220_330_440");
        items.push_back(makeItem("porta_220_330_440", sr, chunk,
                                 harmonicVoice(framesFor(1.45, sr), sr, f0, 6, 0.3f, rng, nullptr),
                                 f0));
    }

    // 5) Voiced/unvoiced mixture: harmonic-stack vowels alternating with white
    //    noise and band-limited hiss; 60 ms raised-cosine transitions, boundary
    //    at the crossfade midpoint for ground truth.
    {
        const std::size_t n = framesFor(1.7, sr);
        struct Seg { double t0, t1; bool voiced; bool hiss; };
        const std::array<Seg, 5> segs{{{0.00, 0.40, true, false}, {0.40, 0.70, false, false},
                                       {0.70, 1.05, true, false}, {1.05, 1.30, false, true},
                                       {1.30, 1.70, true, false}}};
        Rng rngV = itemRng(seed, sr, "vuv_mix_voice");
        std::vector<float> voiced = harmonicVoice(n, sr, constF0(220.0), 6, 0.3f, rngV, nullptr);
        double vPow = 0.0;
        for (std::size_t i = 0; i < framesFor(0.38, sr) && i < n; ++i) vPow += voiced[i] * voiced[i];
        const double vRms = std::sqrt(vPow / framesFor(0.38, sr));
        const double nStd = 0.5 * vRms;  // unvoiced energy below vowel level, like aspiration

        constexpr double kXf = 0.030;  // half-width of each boundary crossfade
        const std::size_t xf = framesFor(kXf, sr);
        std::vector<float> noise(n, 0.0f);
        for (const Seg& s : segs) {
            if (s.voiced) continue;
            // Fill past the edges so the crossfade always has noise under it.
            const std::size_t b = framesFor(s.t0, sr) - std::min(xf, framesFor(s.t0, sr));
            const std::size_t e = std::min(n, framesFor(s.t1, sr) + xf);
            Rng rngN = itemRng(seed, sr, s.hiss ? "vuv_mix_hiss" : "vuv_mix_white");
            std::vector<float> fill =
                s.hiss ? bandHiss(e - b, sr, rngN, nStd) : whiteNoise(e - b, rngN, nStd);
            std::memcpy(noise.data() + b, fill.data(), (e - b) * sizeof(float));
        }
        std::vector<float> mix(n);
        for (std::size_t i = 0; i < n; ++i) {
            const double t = static_cast<double>(i) / sr;
            double vGain = 0.0;
            for (const Seg& s : segs) {
                if (!s.voiced) continue;
                // Raised-cosine edges; boundary midpoint shared with the neighbour.
                double g = 1.0;
                if (t < s.t0 + kXf && s.t0 > 0.0) {
                    const double a = (t - (s.t0 - kXf)) / (2.0 * kXf);
                    g = 0.5 - 0.5 * std::cos(kPi * std::min(1.0, std::max(0.0, a)));
                } else if (t > s.t1 - kXf && s.t1 < 1.7) {
                    const double a = ((s.t1 + kXf) - t) / (2.0 * kXf);
                    g = 0.5 - 0.5 * std::cos(kPi * std::min(1.0, std::max(0.0, a)));
                }
                if (t >= s.t0 - kXf && t <= s.t1 + kXf) vGain = std::max(vGain, g);
            }
            mix[i] = static_cast<float>(vGain * voiced[i] + (1.0 - vGain) * noise[i]);
        }
        items.push_back(makeItem("vuv_mix", sr, chunk, std::move(mix), [segs](double t) {
            for (const Seg& s : segs)
                if (t >= s.t0 && t < s.t1) return s.voiced ? 220.0 : 0.0;
            return 220.0;
        }));
    }

    // 6) SNR sweep: 6-harmonic voice at A3 plus white noise.
    for (const double snrDb : {30.0, 20.0, 10.0, 5.0, 0.0}) {
        const std::string name = "snr_" + std::to_string(static_cast<int>(snrDb)) + "db";
        const std::size_t n = framesFor(0.9, sr);
        Rng rngV = itemRng(seed, sr, name + "_v");
        std::vector<float> sig = harmonicVoice(n, sr, constF0(220.0), 6, 0.25f, rngV, nullptr);
        double p = 0.0;
        for (const float v : sig) p += v * v;
        p /= static_cast<double>(n);
        const double noiseStd = std::sqrt(p / std::pow(10.0, snrDb / 10.0));
        Rng rngN = itemRng(seed, sr, name + "_n");
        const std::vector<float> noise = whiteNoise(n, rngN, noiseStd);
        for (std::size_t i = 0; i < n; ++i) sig[i] += noise[i];
        items.push_back(makeItem(name, sr, chunk, std::move(sig), constF0(220.0)));
    }

    // 7) Octave-ambiguous mixture: equal-amplitude 110 + 220 + 440 Hz.
    {
        const std::size_t n = framesFor(1.0, sr);
        std::vector<float> sig(n);
        for (std::size_t i = 0; i < n; ++i) {
            const double t = static_cast<double>(i) / sr;
            sig[i] = 0.2f * static_cast<float>(std::sin(2.0 * kPi * 110.0 * t) +
                                             std::sin(2.0 * kPi * 220.0 * t) +
                                             std::sin(2.0 * kPi * 440.0 * t));
        }
        items.push_back(makeItem("octave_trap_110_220_440", sr, chunk, std::move(sig),
                                 constF0(110.0)));
    }

    // 8) "Singing-like" vowel: 10-harmonic vibrato voice through an /a/ formant
    //    envelope. The fundamental is attenuated (F1-aligned spectrum), a
    //    realistic missing-fundamental case.
    {
        const F0Fn f0 = vibratoF0(220.0, 0.03, 6.0);
        const std::vector<double> w = formantWeights(220.0, 10);
        Rng rng = itemRng(seed, sr, "formant_vib_a");
        items.push_back(makeItem("formant_vib_a", sr, chunk,
                                 harmonicVoice(framesFor(1.2, sr), sr, f0, 10, 0.35f, rng, &w),
                                 f0));
    }

    return items;
}

}  // namespace corpus

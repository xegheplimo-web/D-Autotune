#include "PitchCorrector.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <numeric>
#include <vector>
using dautotune::PitchCorrector;

static void check(bool ok, const char* reason) {
    if (!ok) { std::cerr << "FAIL: " << reason << "\n"; std::exit(1); }
    std::cout << "PASS: " << reason << "\n";
}

int main() {
    constexpr int fs = 48000;
    check(std::abs(PitchCorrector::nearestScaleFrequency(220.0f, 0, 0) - 220.0f) < 0.05f,
          "A3 is in C major");
    check(std::abs(PitchCorrector::nearestScaleFrequency(228.0f, 0, 0) - 220.0f) < 0.1f,
          "Near-A3 is quantized down to A3");
    check(std::abs(PitchCorrector::nearestScaleFrequency(233.08188f, 0, 2) - 233.08188f) < 0.1f,
          "Chromatic permits A-sharp");
    check(std::abs(PitchCorrector::nearestScaleFrequency(261.62557f, 0, 1) - 261.62557f) < 0.1f,
          "C belongs to C minor");
    check(PitchCorrector::nearestScaleFrequency(0, 0, 0) == 0.0f, "Invalid pitch rejected");

    PitchCorrector dsp(fs);
    dsp.setParameters(0, 0, 1.0f, 15.0f, 0.0f);
    std::vector<float> out(fs * 2);
    std::vector<float> signal(256);
    int frames = 0;
    while (frames < fs * 2) {
        int block = std::min(256, fs * 2 - frames);
        for (int j = 0; j < block; ++j) {
            const float t = static_cast<float>(frames + j) / fs;
            signal[j] = 0.30f * std::sin(2.0f * 3.14159265358979323846f * 228.0f * t);
        }
        dsp.process(signal.data(), out.data() + frames, block, 1);
        frames += block;
    }
    const float detected = dsp.detectedHz();
    const float target = dsp.targetHz();
    std::cout << "detected=" << detected << " Hz, target=" << target << " Hz, correction=" << dsp.correctionCents() << " cents\n";
    check(dsp.voiced() && std::abs(detected - 228.0f) < 6.0f, "YIN detects 228 Hz input");
    check(std::abs(target - 220.0f) < 1.0f, "C major selects A3 220 Hz");
    bool finite = std::all_of(out.begin(), out.end(), [](float v) { return std::isfinite(v) && std::abs(v) <= 0.89f; });
    check(finite, "Audio output finite and peak limited");

    // Simple period estimator after warmup. Checks actual pitch correction, not just tuner metadata.
    const int from = fs + fs / 2;
    const int to = fs * 2;
    int crossings = 0;
    for (int i = from + 1; i < to; ++i)
        if (out[i - 1] <= 0.0f && out[i] > 0.0f) ++crossings;
    const float actual = static_cast<float>(crossings) * fs / (to - from);
    std::cout << "output zero-crossing frequency ~ " << actual << " Hz\n";
    check(std::abs(actual - 220.0f) < 14.0f, "Time-domain shifter changes acoustic output pitch");

    dsp.reset(fs);
    std::fill(signal.begin(), signal.end(), 0.0f);
    for (int i = 0; i < 10; ++i) dsp.process(signal.data(), out.data(), 256, 1);
    check(!dsp.voiced(), "Silence is not interpreted as a sung note");
    std::cout << "ALL HOST DSP TESTS PASSED\n";
}

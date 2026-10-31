#include "PitchCorrector.h"
#include <chrono>
#include <cmath>
#include <iostream>
using namespace std::chrono;
int main() {
    constexpr int rate = 48000, block = 256, minutes = 7;
    dautotune::PitchCorrector engine(rate);
    engine.setParameters(0, 0, 0.90f, 35, 0.10f);
    float input[block]{};
    float output[block * 2]{};
    int64_t frames = 0;
    auto started = steady_clock::now();
    for (int64_t processed = 0; processed < static_cast<int64_t>(rate) * minutes * 60; processed += block) {
        for (int i = 0; i < block; ++i) {
            const float t = static_cast<float>((processed + i) % rate) / rate;
            input[i] = 0.3f * std::sin(2.0f * 3.14159265f * 228.0f * t);
        }
        engine.process(input, output, block, 2);
        for (float v : output) if (!std::isfinite(v) || std::abs(v) > 0.89f) return 1;
        frames += block;
    }
    const double elapsed = duration<double>(steady_clock::now() - started).count();
    std::cout << "Simulated duration: " << frames / static_cast<double>(rate) << " s; host wall time: " << elapsed
              << " s; host faster-than-realtime factor: " << frames / (rate * elapsed) << "x\n";
    std::cout << "Pitch after 7-minute processing: " << engine.detectedHz()
              << " Hz, target=" << engine.targetHz() << " Hz\n";
    return std::abs(engine.detectedHz() - 228.0f) < 7.0f ? 0 : 1;
}

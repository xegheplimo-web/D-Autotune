// DC-003 REV2 host test: NCC-based lag detection on synthetic loopback signals.
// The key gate evidence: a chirp buried at a KNOWN sample delay must be
// recovered within +/-1 sample, and invalid captures must fail closed.
#include "PingMath.h"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <random>
#include <vector>
using dautotune::ping::ScanResult;
using dautotune::ping::findLag;
using dautotune::ping::generateChirp;
using dautotune::ping::nearestRankIndex;

static void check(bool ok, const char* reason) {
    if (!ok) { std::cerr << "FAIL: " << reason << "\n"; std::exit(1); }
    std::cout << "PASS: " << reason << "\n";
}

static std::vector<float> makeChirp(int32_t fs, int32_t ms, float amp) {
    std::vector<float> ref(static_cast<size_t>(fs) * ms / 1000u, 0.0f);
    generateChirp(ref.data(), static_cast<int32_t>(ref.size()), fs, 1000.0f, 4000.0f, amp);
    return ref;
}

int main() {
    constexpr int32_t fs = 48000;
    std::mt19937 rng(20261031);
    std::normal_distribution<float> gauss(0.0f, 1.0f);

    // --- stimulus sanity ---
    const auto ref = makeChirp(fs, 200, 0.25f);
    float peak = 0.0f, energy = 0.0f;
    for (float v : ref) { peak = std::max(peak, std::abs(v)); energy += v * v; }
    check(peak > 0.20f && peak <= 0.25f + 1e-4f, "chirp peak bounded by amplitude cap");
    check(energy > 100.0f, "chirp carries usable energy");
    check(std::abs(ref.front()) < 1e-3f && std::abs(ref.back()) < 1e-3f,
          "raised-cosine edges avoid click transients");

    const int32_t lagMs250 = fs * 250 / 1000;               // search window
    const auto mkcap = [&](int32_t lag, float gain, float noise) {
        std::vector<float> cap(ref.size() + static_cast<size_t>(lagMs250), 0.0f);
        for (size_t i = 0; i < ref.size(); ++i) cap[static_cast<size_t>(lag) + i] = gain * ref[i];
        for (float& v : cap) v += noise * gauss(rng);
        return cap;
    };

    // --- known delay recovered within +/-1 sample ---
    {
        const int32_t truth = 1234;
        const auto cap = mkcap(truth, 1.0f, 0.0f);
        const ScanResult r = findLag(ref.data(), static_cast<int32_t>(ref.size()),
                                     cap.data(), static_cast<int32_t>(cap.size()));
        check(std::abs(r.lag - truth) <= 1, "clean delayed copy: lag recovered +/-1 sample");
        check(r.ncc > 0.95f, "clean delayed copy: NCC ~ 1");
        check(!r.clipped, "interior peak is not clipped");
    }

    // --- heavy attenuation (speaker->mic) must NOT shift the peak ---
    {
        const int32_t truth = 3400;
        const auto cap = mkcap(truth, 0.02f, 0.0f);         // -34 dB
        const ScanResult r = findLag(ref.data(), static_cast<int32_t>(ref.size()),
                                     cap.data(), static_cast<int32_t>(cap.size()));
        check(std::abs(r.lag - truth) <= 1, "attenuated copy: lag still +/-1 sample");
        check(r.ncc > 0.9f, "NCC is amplitude-invariant");
    }

    // --- moderate noise: still detected above the 0.6 validity threshold ---
    {
        const int32_t truth = 777;
        const auto cap = mkcap(truth, 0.05f, 0.01f);
        const ScanResult r = findLag(ref.data(), static_cast<int32_t>(ref.size()),
                                     cap.data(), static_cast<int32_t>(cap.size()));
        check(std::abs(r.lag - truth) <= 1, "noisy attenuated copy: lag +/-1 sample");
        check(r.ncc >= 0.6f, "noisy attenuated copy: NCC clears validity threshold");
    }

    // --- noise-only capture fails closed (low NCC, no fabricated lag) ---
    {
        const auto cap = mkcap(0, 0.0f, 0.05f);
        const ScanResult r = findLag(ref.data(), static_cast<int32_t>(ref.size()),
                                     cap.data(), static_cast<int32_t>(cap.size()));
        check(r.ncc < 0.6f, "noise-only capture stays under validity threshold");
    }

    // --- echo: the stronger (first) copy wins the argmax ---
    {
        const int32_t truth = 500;
        auto cap = mkcap(truth, 1.0f, 0.0f);
        for (size_t i = 0; i < ref.size(); ++i)
            cap[static_cast<size_t>(truth + 400) + i] += 0.4f * ref[i];
        const ScanResult r = findLag(ref.data(), static_cast<int32_t>(ref.size()),
                                     cap.data(), static_cast<int32_t>(cap.size()));
        check(std::abs(r.lag - truth) <= 1, "first (stronger) arrival wins over echo");
    }

    // --- delay beyond the search window must be flagged clipped ---
    {
        auto cap = mkcap(lagMs250, 1.0f, 0.0f);             // peak at last legal lag
        const ScanResult r = findLag(ref.data(), static_cast<int32_t>(ref.size()),
                                     cap.data(), static_cast<int32_t>(cap.size()));
        check(r.clipped, "peak on the window boundary is reported clipped");
    }

    // --- degenerate inputs never fabricate a result ---
    {
        std::vector<float> silence(ref.size() + static_cast<size_t>(lagMs250), 0.0f);
        ScanResult r = findLag(ref.data(), static_cast<int32_t>(ref.size()),
                               silence.data(), static_cast<int32_t>(silence.size()));
        check(r.ncc < 0.6f, "silent capture cannot validate");
        std::vector<float> zeroRef(ref.size(), 0.0f);
        const auto cap = mkcap(100, 1.0f, 0.0f);
        r = findLag(zeroRef.data(), static_cast<int32_t>(zeroRef.size()),
                    cap.data(), static_cast<int32_t>(cap.size()));
        check(r.lag == -1, "zero-energy reference yields no candidate");
        r = findLag(nullptr, 0, cap.data(), static_cast<int32_t>(cap.size()));
        check(r.lag == -1, "null/empty inputs rejected");
        r = findLag(ref.data(), static_cast<int32_t>(ref.size()),
                    cap.data(), 10);                        // cap shorter than ref
        check(r.lag == -1, "capture shorter than reference rejected");
    }

    // --- nearest-rank percentiles for median/p95 reporting ---
    check(nearestRankIndex(100, 50.0) == 49, "median index of 100");
    check(nearestRankIndex(100, 95.0) == 94, "p95 index of 100");
    check(nearestRankIndex(3, 95.0) == 2, "p95 of 3 clamps to last");
    check(nearestRankIndex(1, 50.0) == 0, "single element");
    check(nearestRankIndex(0, 50.0) == -1, "empty set has no rank");

    std::cout << "ALL PING-MATH TESTS PASSED\n";
}

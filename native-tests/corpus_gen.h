#pragma once

// D0 golden corpus: fully synthetic, generated deterministically at test time.
// No binary audio is committed. Everything derives from a fixed seed through a
// small xorshift64* PRNG, so the corpus is byte-identical on every run.
//
// This is test-only code — heap allocation is fine here (NOT the RT path).

#include <cstdint>
#include <string>
#include <vector>

namespace corpus {

// xorshift64* PRNG. Deterministic across runs and platforms for a given seed.
class Rng {
public:
    explicit Rng(std::uint64_t seed);
    std::uint64_t nextU64();
    double nextUniform();   // [0, 1)
    double nextGaussian();  // ~N(0, 1), Box-Muller

private:
    std::uint64_t state_;
    bool spareReady_ = false;
    double spare_ = 0.0;
};

struct Item {
    std::string name;
    int sampleRate = 0;
    int chunk = 0;                  // frames per evaluation chunk
    std::vector<float> samples;     // float32 mono
    std::vector<float> gtF0Hz;      // one entry per chunk: ground-truth F0 at the
                                    // chunk's center time, Hz; 0.0f = unvoiced
};

// Builds the complete D0 corpus for one sample rate. Item order is fixed.
// Each item gets its own Rng derived from (seed, sampleRate, item name), so
// items are independent of build order.
std::vector<Item> build(int sampleRate, int chunk, std::uint64_t seed);

// FNV-1a over the item payload. Used to prove the corpus is deterministic.
std::uint64_t hash(const Item& item);

}  // namespace corpus

// D5 blind-listening pack generator (card G2-D5, deliverable D).
//
// Renders A/B pairs (input vs full-chain output) for a deterministic
// selection of >=5 corpus clips into docs/corpus/listening_pack/ under
// randomized names (pair_NN_A.wav / pair_NN_B.wav). The A/B role mapping is
// written to docs/corpus/listening_key.csv — committed, so the pack is
// reproducible and auditable. Rating sheet: docs/corpus/listening_sheet.md.
// The actual >=3-person listening is a HUMAN task (BLOCKED_HUMAN); this tool
// only prepares the pack.
//
// Same skip discipline as test_d5_realvoice: absent corpus -> loud SKIP,
// exit 0; corrupt corpus -> FAIL.

#include "corpus_gen.h"
#include "corpus_loader.h"
#include "d5_measure.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

namespace {

// Compile-time corpus root; D5_CORPUS_DIR env var overrides it (scratch
// corpora, mirrors). With an override the pack lands under that root too.
const std::string kCorpus = [] {
    const char* env = std::getenv("D5_CORPUS_DIR");
    return (env && env[0]) ? std::string(env) : std::string(D5_CORPUS_DIR);
}();
constexpr std::uint64_t kSeed = 0xD51157EULL;  // fixed: reproducible A/B roles
constexpr std::size_t kMaxPairs = 8;

std::vector<float> rmsMatch(const std::vector<float>& out,
                            const std::vector<float>& in, std::size_t n) {
    double ei = 0.0, eo = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        ei += static_cast<double>(in[i]) * in[i];
        eo += static_cast<double>(out[i]) * out[i];
    }
    const double g = eo > 1e-12 ? std::sqrt(ei / eo) : 1.0;
    std::vector<float> y(out.begin(), out.begin() + static_cast<std::ptrdiff_t>(n));
    for (float& v : y) v = static_cast<float>(v * g);
    // Fair loudness + shared headroom: scale the pair together if either
    // side would clip the 16-bit master.
    double peak = 0.0;
    for (std::size_t i = 0; i < n; ++i)
        peak = std::max(peak, std::max(std::abs(static_cast<double>(in[i])),
                                       std::abs(static_cast<double>(y[i]))));
    if (peak > 0.89)
        for (float& v : y) v = static_cast<float>(v * (0.89 / peak));
    return y;
}

}  // namespace

int main() {
    std::printf("D5 blind-listening pack generator\n");
    std::printf("corpus dir: %s\n", kCorpus.c_str());

    std::vector<d5corpus::ClipMeta> rows;
    if (!d5corpus::loadManifest(kCorpus, &rows)) {
        std::printf("FAIL: manifest present but malformed\n");
        return 1;
    }
    if (rows.empty()) {
        std::printf("d5_listening_pack: SKIP - corpus absent (0 clips)\n");
        return 0;
    }

    // Deterministic selection: first clip per distinct singer (max diversity
    // per the >=4-singer mandate), then fill to kMaxPairs in manifest order.
    std::vector<const d5corpus::ClipMeta*> sel;
    std::set<std::string> singers;
    for (const d5corpus::ClipMeta& m : rows)
        if (singers.insert(m.singer).second) sel.push_back(&m);
    for (const d5corpus::ClipMeta& m : rows) {
        if (sel.size() >= kMaxPairs) break;
        bool have = false;
        for (const auto* s : sel) have = have || s == &m;
        if (!have) sel.push_back(&m);
    }
    if (sel.size() < 5) {
        std::printf("WARN: only %zu selectable clips (< 5) - pack needs the "
                    "20+ clip corpus; not a failure yet\n",
                    sel.size());
    }

    const std::filesystem::path packDir =
        std::filesystem::path(kCorpus) / "listening_pack";
    std::error_code ec;
    std::filesystem::remove_all(packDir, ec);
    std::filesystem::create_directories(packDir, ec);
    if (ec) {
        std::printf("FAIL: cannot create %s (%s)\n", packDir.string().c_str(),
                    ec.message().c_str());
        return 1;
    }

    corpus::Rng rng(kSeed);
    std::string key = "pair_id,slot,role,clip_id\n";
    int failures = 0;
    std::size_t made = 0;
    for (std::size_t i = 0; i < sel.size(); ++i) {
        const d5corpus::ClipMeta& m = *sel[i];
        d5corpus::Clip c;
        std::string err;
        if (!d5corpus::loadClip(kCorpus, m, &c, &err)) {
            std::printf("  REJECT %s: %s\n", m.clipId.c_str(), err.c_str());
            ++failures;
            continue;
        }
        const d5m::Rendered r = d5m::renderChain(c.samples, c.fs, c.notes, false);
        std::vector<float> outM = rmsMatch(r.mono, c.samples, r.rendered);
        std::vector<float> inT(c.samples.begin(),
                               c.samples.begin() + static_cast<std::ptrdiff_t>(r.rendered));

        // Randomized A/B role, fixed seed -> reproducible key.
        const bool aIsInput = rng.nextUniform() < 0.5;
        char name[32];
        std::snprintf(name, sizeof(name), "pair_%02zu", i + 1);
        const std::string aPath = (packDir / (std::string(name) + "_A.wav")).string();
        const std::string bPath = (packDir / (std::string(name) + "_B.wav")).string();
        const std::vector<float>& aSig = aIsInput ? inT : outM;
        const std::vector<float>& bSig = aIsInput ? outM : inT;
        if (!d5corpus::writeWav16(aPath, aSig, c.fs, &err) ||
            !d5corpus::writeWav16(bPath, bSig, c.fs, &err)) {
            std::printf("  WRITE FAIL %s: %s\n", m.clipId.c_str(), err.c_str());
            ++failures;
            continue;
        }
        key += std::string(name) + ",A," + (aIsInput ? "input" : "output") + "," +
               m.clipId + "\n";
        key += std::string(name) + ",B," + (aIsInput ? "output" : "input") + "," +
               m.clipId + "\n";
        ++made;
        std::printf("  %s: %s (fs %d, %s / %s / %s)\n", name, m.clipId.c_str(), c.fs,
                    m.singer.c_str(), m.context.c_str(), m.technique.c_str());
    }

    // The key is committed at the corpus root (git-visible, auditors only).
    const std::string keyPath = kCorpus + "/listening_key.csv";
    if (FILE* f = std::fopen(keyPath.c_str(), "wb")) {
        std::fwrite(key.data(), 1, key.size(), f);
        std::fclose(f);
    } else {
        std::printf("FAIL: cannot write %s\n", keyPath.c_str());
        return 1;
    }
    std::printf("pack: %zu pair(s) -> %s; key -> %s\n", made,
                packDir.string().c_str(), keyPath.c_str());
    if (made < 5)
        std::printf("WARN: pack has %zu pairs (< 5) - rerun after the full "
                    "corpus lands\n",
                    made);
    if (failures) {
        std::printf("d5_listening_pack: %d FAILURES\n", failures);
        return 1;
    }
    std::printf("d5_listening_pack: %s\n",
                made >= 5 ? "PACK READY" : "INCOMPLETE (corpus too small)");
    return 0;
}

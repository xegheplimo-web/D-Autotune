// D5 blind-listening pack generator v2 (card G2-D8 — GPT round-5 redesign).
//
// Renders A/B pairs (input vs full-chain output) for a balanced selection of
// corpus clips into docs/corpus/listening_pack/ under randomized names
// (pair_NN_A.wav / pair_NN_B.wav). The v1 pack had two defects GPT round-5
// caught: listening_key.csv was committed publicly — the "blind" mapping was
// readable — and manifest-order selection skewed the pairs (f1 in 5 of 8).
// The v1 key stays in git history, so this generator re-randomizes
// EVERYTHING under a fresh seed and keeps the new key private:
//
//   * 8 MAIN pairs balanced 2 per singer — 1 long_tones + 1 scales each.
//     Per singer+context the candidate pool is shuffled before picking, so
//     clip choice never follows manifest order.
//   * 2 hidden A/A CONTROL pairs (1 long_tones + 1 scales clip not used by
//     any main pair): the same rendered audio is written to BOTH slots, so
//     A and B are byte-identical. They are a rater-reliability check — a
//     reliable rater reports "identical" / preference "none". Raters are
//     not told which pairs are controls.
//   * pair_NN numbering and the A/B role of each main pair are drawn from a
//     NEW seed, so the leaked v1 key cannot predict this mapping.
//   * The key is PRIVATE — written to docs/corpus/listening_key_v2.csv,
//     which is .gitignore'd and must never be committed before the raters'
//     sheets are frozen.
//
// Rating sheet: docs/corpus/listening_sheet.md. The actual >=3-person
// listening is a HUMAN task (BLOCKED_HUMAN); this tool only prepares the pack.
//
// Same skip discipline as test_d5_realvoice: absent corpus -> loud SKIP,
// exit 0; corrupt corpus -> FAIL. A corpus that cannot fill the balanced
// design (4 singers x {long_tones,scales} mains + 1 leftover clip per
// context for the controls) also FAILs loudly, listing the missing
// singer/context.

#include "corpus_gen.h"
#include "corpus_loader.h"
#include "d5_measure.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace {

// Compile-time corpus root; D5_CORPUS_DIR env var overrides it (scratch
// corpora, mirrors). With an override the pack lands under that root too.
const std::string kCorpus = [] {
    const char* env = std::getenv("D5_CORPUS_DIR");
    return (env && env[0]) ? std::string(env) : std::string(D5_CORPUS_DIR);
}();
// Fresh v2 seed — MUST differ from the v1 0xD51157E: the old key is public
// in git history, so any mapping it predicts is compromised.
constexpr std::uint64_t kSeed = 0xD8B11D2EULL;
constexpr std::size_t kMainSingers = 4;  // 4 singers x 2 pairs = 8 main pairs
const char* const kContexts[] = {"long_tones", "scales"};

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

// Fisher-Yates over the deterministic corpus::Rng — same permutation on
// every platform for a given seed.
template <typename T>
void shufflePool(std::vector<T>& v, corpus::Rng& rng) {
    for (std::size_t i = v.size(); i > 1; --i) {
        const std::size_t j = static_cast<std::size_t>(rng.nextUniform() *
                                                       static_cast<double>(i));
        std::swap(v[i - 1], v[j]);  // j < i because nextUniform() < 1
    }
}

// One pack row after selection, before pair_NN numbering is assigned.
struct Plan {
    const d5corpus::ClipMeta* meta = nullptr;
    bool control = false;
    bool aIsInput = false;  // main pairs only — controls are A/A identical
};

}  // namespace

int main() {
    std::printf("D5 blind-listening pack generator v2\n");
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

    corpus::Rng rng(kSeed);

    // --- balanced selection ---------------------------------------------------
    // Pool: singer -> context -> clips. Buckets start in manifest order but
    // are shuffled before anything is drawn, so selection never follows it.
    std::map<std::string,
             std::map<std::string, std::vector<const d5corpus::ClipMeta*>>>
        pool;
    for (const d5corpus::ClipMeta& m : rows)
        pool[m.singer][m.context].push_back(&m);

    std::vector<std::string> singers;
    for (const auto& kv : pool) singers.push_back(kv.first);
    if (singers.size() < kMainSingers) {
        std::printf("FAIL: corpus has %zu distinct singer(s); the balanced "
                    "design needs %zu (1 long_tones + 1 scales pair each)\n",
                    singers.size(), kMainSingers);
        return 1;
    }
    shufflePool(singers, rng);
    singers.resize(kMainSingers);  // extra singers still feed the controls

    // Per chosen singer: 1 shuffled pick per context -> 2 main pairs each.
    // Leftovers join the control candidate pool.
    std::vector<const d5corpus::ClipMeta*> mainClips;
    std::map<std::string, std::vector<const d5corpus::ClipMeta*>> spare;
    int missing = 0;
    for (const std::string& s : singers) {
        for (const char* ctx : kContexts) {
            std::vector<const d5corpus::ClipMeta*>& bucket = pool[s][ctx];
            if (bucket.empty()) {
                std::printf("FAIL: singer %s has no %s clip - the balanced "
                            "design needs 1 of each context per singer\n",
                            s.c_str(), ctx);
                ++missing;
                continue;
            }
            shufflePool(bucket, rng);
            mainClips.push_back(bucket.front());
            spare[ctx].insert(spare[ctx].end(), bucket.begin() + 1,
                              bucket.end());
        }
    }
    if (missing) {
        std::printf("d5_listening_pack: FAIL - %d missing singer/context "
                    "slot(s)\n",
                    missing);
        return 1;
    }
    // Clips of singers outside the main quartet are control candidates too —
    // "not used in the main pairs" is the only constraint.
    for (const auto& s : pool) {
        if (std::find(singers.begin(), singers.end(), s.first) !=
            singers.end())
            continue;
        for (const char* ctx : kContexts) {
            const auto it = s.second.find(ctx);
            if (it != s.second.end())
                spare[ctx].insert(spare[ctx].end(), it->second.begin(),
                                  it->second.end());
        }
    }
    for (const char* ctx : kContexts) {
        if (spare[ctx].empty()) {
            std::printf("FAIL: no leftover %s clip for the hidden A/A "
                        "control\n",
                        ctx);
            return 1;
        }
        shufflePool(spare[ctx], rng);
    }

    // --- plans: 8 mains + 2 controls, then shuffle for pair_NN numbering ------
    std::vector<Plan> plans;
    for (const d5corpus::ClipMeta* m : mainClips) {
        Plan p;
        p.meta = m;
        p.aIsInput = rng.nextUniform() < 0.5;
        plans.push_back(p);
    }
    for (const char* ctx : kContexts) {
        Plan p;
        p.meta = spare[ctx].front();
        p.control = true;
        plans.push_back(p);
    }
    shufflePool(plans, rng);  // pair_id assignment is itself randomized

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

    std::string key = "pair_id,kind,slot,role,clip_id\n";
    int failures = 0;
    std::size_t made = 0;
    for (std::size_t i = 0; i < plans.size(); ++i) {
        const Plan& p = plans[i];
        const d5corpus::ClipMeta& m = *p.meta;
        const char* kind = p.control ? "control" : "main";
        d5corpus::Clip c;
        std::string err;
        if (!d5corpus::loadClip(kCorpus, m, &c, &err)) {
            std::printf("  REJECT %s: %s\n", m.clipId.c_str(), err.c_str());
            ++failures;
            continue;
        }
        const d5m::Rendered r =
            d5m::renderChain(c.samples, c.fs, c.notes, false);
        std::vector<float> outM = rmsMatch(r.mono, c.samples, r.rendered);
        std::vector<float> inT(c.samples.begin(),
                               c.samples.begin() + static_cast<std::ptrdiff_t>(r.rendered));

        char name[32];
        std::snprintf(name, sizeof(name), "pair_%02zu", i + 1);
        const std::string aPath =
            (packDir / (std::string(name) + "_A.wav")).string();
        const std::string bPath =
            (packDir / (std::string(name) + "_B.wav")).string();

        // Hidden A/A control: the SAME rendered signal in both slots — the
        // rmsMatch/headroom path is reused unchanged and the two files come
        // out byte-identical. Main pairs keep the randomized A/B role.
        const std::vector<float>& aSig =
            p.control ? outM : (p.aIsInput ? inT : outM);
        const std::vector<float>& bSig =
            p.control ? outM : (p.aIsInput ? outM : inT);
        if (!d5corpus::writeWav16(aPath, aSig, c.fs, &err) ||
            !d5corpus::writeWav16(bPath, bSig, c.fs, &err)) {
            std::printf("  WRITE FAIL %s: %s\n", m.clipId.c_str(), err.c_str());
            ++failures;
            continue;
        }
        if (p.control) {
            key += std::string(name) + ",control,A,control," + m.clipId + "\n";
            key += std::string(name) + ",control,B,control," + m.clipId + "\n";
        } else {
            key += std::string(name) + ",main,A," +
                   (p.aIsInput ? "input" : "output") + "," + m.clipId + "\n";
            key += std::string(name) + ",main,B," +
                   (p.aIsInput ? "output" : "input") + "," + m.clipId + "\n";
        }
        ++made;
        // The public summary names singer/context/kind only — never the A/B
        // roles and never the clip_id (the key is private).
        std::printf("  %s: %s / %s / %s\n", name, m.singer.c_str(),
                    m.context.c_str(), kind);
    }

    // PRIVATE key: listening_key_v2.csv is .gitignore'd. The v1 key was
    // committed publicly and is void — this file must never be committed
    // before the raters' sheets are frozen.
    const std::string keyPath = kCorpus + "/listening_key_v2.csv";
    if (FILE* f = std::fopen(keyPath.c_str(), "wb")) {
        std::fwrite(key.data(), 1, key.size(), f);
        std::fclose(f);
    } else {
        std::printf("FAIL: cannot write %s\n", keyPath.c_str());
        return 1;
    }
    std::printf("pack: %zu pair(s) -> %s; PRIVATE key -> %s\n", made,
                packDir.string().c_str(), keyPath.c_str());
    if (failures) {
        std::printf("d5_listening_pack: %d FAILURES\n", failures);
        return 1;
    }
    std::printf("d5_listening_pack: PACK READY (8 main + 2 control)\n");
    return 0;
}

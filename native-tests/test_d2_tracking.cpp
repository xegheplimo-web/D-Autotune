// D2 — F0 tracking + hysteresis unit tests.
//
// These are STATE-BEHAVIOR tests, not accuracy tests: they verify the
// tracker's jump gate, the voiced-flag Schmitt trigger, breath-window state
// retention, bounded holds, and ramp following. Tests A/B/C drive the state
// machine directly through PitchCorrector::updateTargetForTest (a detector
// seam that feeds synthetic DetOut values); the audio half of C and test D
// run real audio through process() so the D1 detector is exercised end to
// end. Everything is deterministic — fixed sample rates, fixed sequences,
// the seeded golden corpus for the ramp item.

#include "PitchCorrector.h"
#include "corpus_gen.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {

constexpr int kSr = 48000;
constexpr int kChunk = 512;
constexpr int kOutCh = 2;

int gFailures = 0;

void check(bool ok, const std::string& what) {
    std::printf("  %s: %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (!ok) ++gFailures;
}

double cents(double a, double b) { return 1200.0 * std::log2(a / b); }

// Feed n injected detections at (hz, conf). strict pass semantics by default.
void feed(dautotune::PitchCorrector& dsp, float hz, float conf, int n,
          bool relaxed = false) {
    for (int i = 0; i < n; ++i) dsp.updateTargetForTest(hz, conf, relaxed);
}

// ---------------------------------------------------------------------------
// Test A — outlier rejection / jump gate (card a).
// A single-frame +500-cent glitch must not retune the output; a consistent
// jump must be accepted on the 2nd consecutive agreeing detection.
// ---------------------------------------------------------------------------
bool testOutlier() {
    std::printf("A. outlier rejection (single-frame glitch vs consistent jump)\n");
    dautotune::PitchCorrector dsp(kSr);
    dsp.setParameters(0, 0, 1.0f, 35.0f, 0.0f);  // C major: A3 and D4 in scale
    constexpr float kA3 = 220.0f;
    const float kD4 = static_cast<float>(220.0 * std::pow(2.0, 500.0 / 1200.0));  // +500c

    feed(dsp, kA3, 0.9f, 3);  // establish track (cold start: 1 unvoiced + voiced)
    check(dsp.voiced(), "A: voiced after 3 steady frames");
    check(std::abs(dsp.targetHz() - kA3) < 2.0f, "A: tracking A3 target");

    // Single-frame +500-cent glitch.
    feed(dsp, kD4, 0.9f, 1);
    check(dsp.voiced(), "A: voiced flag held on glitch frame");
    check(std::abs(dsp.targetHz() - kA3) < 2.0f,
          "A: correction target did NOT jump on single-frame glitch");
    check(std::abs(dsp.detectedHz() - kD4) < 3.0f,
          "A: raw estimate still reported on held frame");

    feed(dsp, kA3, 0.9f, 2);  // glitch passes; track continues
    check(dsp.voiced() && std::abs(dsp.targetHz() - kA3) < 2.0f,
          "A: still on A3 after glitch (no retune)");

    // Consistent jump to +500 cents: accepted on the 2nd agreeing detection.
    feed(dsp, kD4, 0.9f, 1);
    check(dsp.voiced() && std::abs(dsp.targetHz() - kA3) < 2.0f,
          "A: 1st jump frame held at old target (confirming)");
    feed(dsp, kD4, 0.9f, 1);
    check(dsp.voiced() && std::abs(dsp.targetHz() - kD4) < 3.0f,
          "A: 2nd agreeing jump frame accepted -> D4 target");
    check(std::abs(dsp.detectedHz() - kD4) < 3.0f,
          "A: applied F0 follows accepted jump");
    feed(dsp, kD4, 0.9f, 2);
    check(dsp.voiced() && std::abs(dsp.targetHz() - kD4) < 3.0f,
          "A: steady on new note");
    return true;
}

// ---------------------------------------------------------------------------
// Test B — voiced-state hysteresis (card b).
// Confidence straddling the hold band (0.65 in / 0.40 hold / 0.20 out) must
// not flutter the voiced flag: <= 2 transitions total.
// ---------------------------------------------------------------------------
bool testHysteresis() {
    std::printf("B. voiced-state hysteresis (Schmitt band, conf .65/.40/.20)\n");
    dautotune::PitchCorrector dsp(kSr);
    dsp.setParameters(0, 0, 1.0f, 35.0f, 0.0f);

    // relaxed flag => admission gate 0.44, so conf .65 is admitted but weak
    // (< strong band .70); .40 and .20 land below the gate entirely.
    const std::vector<float> seq = {0.65f, 0.65f, 0.40f, 0.65f, 0.40f, 0.20f};
    std::vector<bool> flags;
    int transitions = 0;
    bool prev = false;
    for (const float c : seq) {
        feed(dsp, 220.0f, c, 1, /*relaxed=*/true);
        const bool v = dsp.voiced();
        flags.push_back(v);
        if (v != prev) ++transitions;
        prev = v;
    }
    check(flags[1], "B: entered voiced on 2nd confirmed .65");
    check(flags[2] && flags[4], "B: .40 dips held voiced (no flap)");
    check(!flags[5], "B: .20 below exit floor released unvoiced");
    check(transitions <= 2, "B: voiced transitions <= 2");
    return true;
}

// ---------------------------------------------------------------------------
// Test C — breath between phrases (card c).
// An unvoiced gap < 150 ms preserves the tracking ring: the first voiced
// frame after the gap is accepted immediately (no warm-up). A longer gap
// clears it (control). Also an end-to-end audio sanity run.
// ---------------------------------------------------------------------------
bool testBreath() {
    std::printf("C. breath window preserves tracking state (<150 ms)\n");
    const int gapFramesFor100ms = 9;  // 9 * 10.67ms = 96 ms < 150 ms window (14)

    dautotune::PitchCorrector dsp(kSr);
    dsp.setParameters(0, 0, 1.0f, 35.0f, 0.0f);
    feed(dsp, 220.0f, 0.9f, 3);            // voiced on A3
    feed(dsp, 0.0f, 0.0f, gapFramesFor100ms);  // ~100 ms breath
    check(!dsp.voiced(), "C: unvoiced reported during the gap");
    // Re-entry: one weak on-track estimate must hold immediately — the ring
    // survived, so there is no confirmation warm-up. (A tentative held frame
    // reports 0 Hz, mirroring D1's reporting rule; the flag is the evidence.)
    feed(dsp, 220.0f, 0.65f, 1, /*relaxed=*/true);
    check(dsp.voiced(), "C: first post-gap frame voiced immediately (state preserved)");
    feed(dsp, 220.0f, 0.65f, 1, /*relaxed=*/true);
    check(dsp.voiced() && std::abs(dsp.detectedHz() - 220.0f) < 3.0f &&
              std::abs(dsp.targetHz() - 220.0f) < 2.0f,
          "C: correction target continuous across breath");

    // Control: gap longer than the breath window clears the track.
    dautotune::PitchCorrector dsp2(kSr);
    dsp2.setParameters(0, 0, 1.0f, 35.0f, 0.0f);
    feed(dsp2, 220.0f, 0.9f, 3);
    feed(dsp2, 0.0f, 0.0f, 16);  // ~171 ms > 150 ms window
    feed(dsp2, 220.0f, 0.65f, 1, /*relaxed=*/true);
    check(!dsp2.voiced(), "C: >150 ms gap cleared state (warm-up required)");
    feed(dsp2, 220.0f, 0.65f, 1, /*relaxed=*/true);
    check(dsp2.voiced(), "C: control re-confirms on the next frame");

    // End-to-end: real audio, 100 ms silence between two A3 phrases.
    std::printf("  -- audio run: 220 Hz, 100 ms silence, 220 Hz --\n");
    dautotune::PitchCorrector dspA(kSr);
    dspA.setParameters(0, 0, 0.9f, 35.0f, 0.1f);
    std::vector<float> out(static_cast<std::size_t>(kChunk) * kOutCh);
    auto tone = [&](float hz) {
        std::vector<float> b(kChunk);
        for (int i = 0; i < kChunk; ++i)
            b[i] = 0.3f * std::sin(2.0 * 3.14159265358979323846 * hz * i / kSr);
        return b;
    };
    const std::vector<float> silence(kChunk, 0.0f);
    static double phase = 0.0;
    auto toneCont = [&](float hz) {  // phase-continuous tone for clean periodicity
        std::vector<float> b(kChunk);
        for (int i = 0; i < kChunk; ++i) {
            phase += 2.0 * 3.14159265358979323846 * hz / kSr;
            b[i] = 0.3f * static_cast<float>(std::sin(phase));
        }
        return b;
    };
    int voicedChunk = -1;
    for (int c = 0; c < 24; ++c) {  // ~0.26 s of A3
        const std::vector<float> b = toneCont(220.0f);
        dspA.process(b.data(), out.data(), kChunk, kOutCh);
    }
    for (int c = 0; c < 10; ++c)  // ~107 ms silence < 150 ms
        dspA.process(silence.data(), out.data(), kChunk, kOutCh);
    check(!dspA.voiced(), "C(audio): unvoiced during silence");
    for (int c = 0; c < 6 && voicedChunk < 0; ++c) {
        const std::vector<float> b = toneCont(220.0f);
        dspA.process(b.data(), out.data(), kChunk, kOutCh);
        if (dspA.voiced()) voicedChunk = c;
    }
    check(voicedChunk >= 0 && voicedChunk <= 3,
          "C(audio): voiced within <=4 chunks after breath (no cold warm-up)");
    check(std::abs(cents(dspA.detectedHz(), 220.0)) < 50.0,
          "C(audio): re-entry estimate within 50 cents of A3");
    (void)tone;
    return true;
}

// ---------------------------------------------------------------------------
// Test D — ramp following (card d).
// The golden-corpus porta item (220 -> 330 Hz linear ramp over 150 ms)
// through the real detector: |est - gt| must be <= 50 cents at ramp end and
// stay <= 50 cents through the 60 ms settle window after it.
// ---------------------------------------------------------------------------
bool testRamp() {
    std::printf("D. ramp following (porta 220->330 over 150 ms, real detector)\n");
    const std::vector<corpus::Item> corpus = corpus::build(kSr, kChunk, 0xD0BA5EEDull);
    const corpus::Item* porta = nullptr;
    for (const auto& it : corpus)
        if (it.name == "porta_220_330_440") porta = &it;
    check(porta != nullptr, "D: porta corpus item exists");
    if (!porta) return false;

    dautotune::PitchCorrector dsp(kSr);
    dsp.setParameters(0, 2, 0.9f, 35.0f, 0.1f);  // chromatic: no scale constraint
    std::vector<float> out(static_cast<std::size_t>(kChunk) * kOutCh);
    const std::size_t nChunks = porta->samples.size() / kChunk;

    // Ramp 220->330 occupies [0.40, 0.55] s; settle window ends at 0.61 s.
    double worstSettle = 0.0;
    int firstGood = -1;     // first chunk >=0.55 s within 50 cents
    double rampMax = 0.0;   // worst error while ramp is moving
    for (std::size_t c = 0; c < nChunks; ++c) {
        const float* in = porta->samples.data() + c * kChunk;
        dsp.process(in, out.data(), kChunk, kOutCh);
        const double t = (static_cast<double>(c) + 0.5) * kChunk / kSr;
        const float gt = porta->gtF0Hz[c];
        const float est = dsp.detectedHz();
        if (gt <= 0.0f || est <= 0.0f) continue;
        const double err = std::abs(cents(est, gt));
        if (t >= 0.40 && t <= 0.55) rampMax = std::max(rampMax, err);
        if (t > 0.55 && t <= 0.61) {
            worstSettle = std::max(worstSettle, err);
            if (firstGood < 0 && err <= 50.0) firstGood = static_cast<int>(c);
        }
    }
    std::printf("  ramp-window max |err| = %.1f c; settle(0.55-0.61 s) worst = %.1f c\n",
                rampMax, worstSettle);
    check(firstGood >= 0, "D: some chunk in the settle window within 50 cents");
    check(worstSettle <= 50.0,
          "D: within 50 cents of target at ramp end through +60 ms settle");
    return true;
}

// ---------------------------------------------------------------------------
// Test E — bounded hold (extra safety check).
// Alternating disagreeing strong frames can never confirm a jump; the hold
// must release (voiced drops) after kHoldMax updates instead of latching.
// ---------------------------------------------------------------------------
bool testHoldRelease() {
    std::printf("E. bounded hold releases on unconfirmable input\n");
    dautotune::PitchCorrector dsp(kSr);
    dsp.setParameters(0, 0, 1.0f, 35.0f, 0.0f);
    feed(dsp, 220.0f, 0.9f, 3);  // voiced on A3
    bool releasedEarly = false;
    bool released = false;
    for (int i = 0; i < 8; ++i) {
        // Alternate between the tracked note and +500 cents: neither jump
        // confirms, so holds accumulate until the bound forces a release.
        feed(dsp, (i % 2 == 0) ? 293.66f : 220.0f, 0.9f, 1);
        if (!dsp.voiced()) {
            released = true;
            if (i < 4) releasedEarly = true;  // must hold at least 4 frames
            break;
        }
    }
    check(released, "E: voiced released after bounded holds");
    check(!releasedEarly, "E: held for the full hold window before releasing");
    return true;
}

}  // namespace

int main() {
    std::printf("D2 tracking + hysteresis test battery\n");
    std::printf("sr=%d chunk=%d; injected DetOut seam + real-audio checks\n\n", kSr, kChunk);

    struct { const char* name; bool (*fn)(); } tests[] = {
        {"A outlier rejection", testOutlier}, {"B hysteresis", testHysteresis},
        {"C breath window", testBreath},      {"D ramp following", testRamp},
        {"E bounded hold release", testHoldRelease},
    };
    std::vector<std::pair<const char*, bool>> results;
    for (const auto& t : tests) {
        const int before = gFailures;
        t.fn();
        results.push_back({t.name, gFailures == before});
        std::printf("\n");
    }

    std::printf("=== D2 TEST TABLE ===\n");
    std::printf("| test                          | result |\n");
    std::printf("|-------------------------------|--------|\n");
    for (const auto& r : results)
        std::printf("| %-29s | %s |\n", r.first, r.second ? "PASS" : "FAIL");

    if (gFailures) {
        std::printf("D2 TRACKING TESTS: %d FAILURES\n", gFailures);
        return 1;
    }
    std::printf("D2 TRACKING TESTS: ALL PASS\n");
    return 0;
}

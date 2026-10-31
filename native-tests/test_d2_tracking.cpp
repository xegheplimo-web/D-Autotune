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
//
// Round-3 extension (card G2-D2EXT): batteries F (staccato note
// transitions), G (scale-boundary vibrato) and H (300-1200-cent interval
// jumps) measure the APPLIED F0 (derived from targetHz + correctionCents),
// target-note transition cadence and the correction-ratio residual — the
// applied-output view GPT round-3 asked for, not only reported detectedHz.
// A "chatter window" (>= 3 member transitions inside one vibrato half-cycle)
// is the instrument that would justify target-note hysteresis.

#include "PitchCorrector.h"
#include "corpus_gen.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
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

// ===========================================================================
// Round-3 test extension (card G2-D2EXT, verdict D-014): batteries F/G/H
// measure APPLIED F0, target-note transitions and the correction-ratio
// residual — not only the reported detectedHz stream. appliedHz_ is internal;
// cents_ = 1200*log2(target/appliedHz)*amount, so the public telemetry pair
// recovers it exactly whenever a target is reported.
// ===========================================================================

constexpr double kHopMs = 1000.0 * static_cast<double>(kChunk) / kSr;
constexpr double kTwoPi = 2.0 * 3.14159265358979323846;

struct Obs {
    float det = 0.0f;   // detectedHz() after this update
    float tgt = 0.0f;   // targetHz() — the snapped scale member (applied note)
    float cnt = 0.0f;   // correctionCents() — residual correction * amount
    bool vox = false;   // voiced()
};

// Equal-tempered member frequency (exactly what nearestScaleFrequency emits).
double etHz(int midiNote) {
    return 440.0 * std::pow(2.0, (static_cast<double>(midiNote) - 69.0) / 12.0);
}

double appliedHzFrom(float tgt, float cnt, float amount) {
    return (tgt > 0.0f && amount > 0.0f)
               ? static_cast<double>(tgt) / std::pow(2.0, cnt / (1200.0 * amount))
               : 0.0;
}

Obs feedObs(dautotune::PitchCorrector& dsp, float hz, float conf,
            bool relaxed = false) {
    dsp.updateTargetForTest(hz, conf, relaxed);
    return {dsp.detectedHz(), dsp.targetHz(), dsp.correctionCents(), dsp.voiced()};
}

std::vector<Obs> runAudio(dautotune::PitchCorrector& dsp,
                         const std::vector<float>& sig) {
    std::vector<float> buf(static_cast<std::size_t>(kChunk) * kOutCh);
    std::vector<Obs> out;
    for (std::size_t c = 0; c + kChunk <= sig.size(); c += kChunk) {
        dsp.process(sig.data() + c, buf.data(), kChunk, kOutCh);
        out.push_back(
            {dsp.detectedHz(), dsp.targetHz(), dsp.correctionCents(), dsp.voiced()});
    }
    return out;
}

// Member-transition stream: indices where the nonzero snapped target moves
// between scale members. Unvoiced frames (tgt == 0) are transparent.
struct Trans {
    int idx;
    float fromHz;
    float toHz;
};
std::vector<Trans> memberTransitions(const std::vector<Obs>& o) {
    std::vector<Trans> t;
    float prev = 0.0f;
    for (int i = 0; i < static_cast<int>(o.size()); ++i) {
        if (o[i].tgt <= 0.0f) continue;
        if (prev > 0.0f && std::abs(cents(o[i].tgt, prev)) > 5.0)
            t.push_back({i, prev, o[i].tgt});
        prev = o[i].tgt;
    }
    return t;
}

// Counts member transitions whose landing index falls in [c0, c1).
int transitionsIn(const std::vector<Trans>& t, int c0, int c1) {
    int n = 0;
    for (const Trans& x : t)
        if (x.idx >= c0 && x.idx < c1) ++n;
    return n;
}

// Windows of `win` consecutive updates containing >= `minT` member transitions
// are target chatter (flip-flopping faster than a note can musically change).
int chatterWindows(const std::vector<Trans>& t, int win, int minT) {
    int n = 0;
    for (std::size_t i = 0; i + minT <= t.size(); ++i)
        if (t[i + minT - 1].idx - t[i].idx <= win) ++n;
    return n;
}

// --- audio signal generation (deterministic; corpus-style PRNG discipline) --

// Harmonic stack voice on an F0 contour: integrated phase, 1/j rolloff, fixed
// phases drawn from a seeded Rng. Mirrors corpus_gen's harmonicVoice.
std::vector<float> voice(std::size_t n, const std::function<double(double)>& f0,
                         int harmonics, float amp, corpus::Rng& rng) {
    std::vector<double> p0(static_cast<std::size_t>(harmonics));
    for (double& p : p0) p = rng.nextUniform() * kTwoPi;
    std::vector<float> out(n);
    double phase = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        phase += kTwoPi * f0(static_cast<double>(i) / kSr) / kSr;
        double s = 0.0;
        for (int j = 1; j <= harmonics; ++j)
            s += std::sin(static_cast<double>(j) * phase +
                          p0[static_cast<std::size_t>(j - 1)]) /
                 static_cast<double>(j);
        out[i] = amp * static_cast<float>(s);
    }
    return out;
}

// 5 ms raised-cosine edges: staccato onsets are fast but not spectral clicks.
void edgeFades(std::vector<float>& v) {
    const std::size_t e = static_cast<std::size_t>(0.005 * kSr);
    for (std::size_t i = 0; i < e && 2 * i < v.size(); ++i) {
        const float g = static_cast<float>(0.5 - 0.5 * std::cos(3.14159265358979323846 *
                                                              static_cast<double>(i) / e));
        v[i] *= g;
        v[v.size() - 1 - i] *= g;
    }
}

// A note span in chunk units: [c0, c1) sung at sungHz, snapped to memberHz.
struct NoteSpan {
    int c0;
    int c1;
    double sungHz;
    double memberHz;
    const char* name;
};

// Per-note round-3 metrics row.
struct NoteRow {
    const char* name = "";
    double retuneMs = -1;      // first admitted detection -> target on member
    double settleMs = -1;      // landing -> |correction residual| <= 5 c held
    double maxAppliedErr = -1; // max |derived appliedHz - sung Hz|, cents
    double maxResid = -1;      // max |correctionCents - expected residual|
    int transitions = -1;      // member transitions landing inside the span
};

// `admitIsStart` covers the injected seam: every fed frame is admitted, so the
// admission index is the span start even when the report row shows det == 0
// (unvoiced/held bookkeeping). At audio level the first chunk whose estimate
// lands within 150 cents of the sung note while voiced is the admission.
NoteRow analyzeSpan(const std::vector<Obs>& o, const std::vector<Trans>& trs,
                    const NoteSpan& sp, float amount, bool admitIsStart) {
    NoteRow r;
    r.name = sp.name;
    const int end = std::min(sp.c1, static_cast<int>(o.size()));
    int admit = admitIsStart ? sp.c0 : -1;
    int land = -1;
    for (int i = sp.c0; i < end; ++i) {
        if (admit < 0 && o[i].vox && o[i].det > 0.0f &&
            std::abs(cents(o[i].det, static_cast<float>(sp.sungHz))) <= 150.0)
            admit = i;
        if (land < 0 && o[i].tgt > 0.0f &&
            std::abs(cents(o[i].tgt, static_cast<float>(sp.memberHz))) <= 5.0)
            land = i;
    }
    if (admit < 0) admit = sp.c0;
    if (land >= 0) r.retuneMs = (land - admit) * kHopMs;

    int settle = -1;
    for (int i = std::max(land, 0); i < end && land >= 0; ++i) {
        bool clean = true;
        for (int j = i; j < end; ++j)
            if (std::abs(o[j].cnt) > 5.0f) { clean = false; break; }
        if (clean) { settle = i; break; }
    }
    if (settle >= 0) r.settleMs = (settle - land) * kHopMs;

    double maxErr = 0.0, maxRes = 0.0;
    for (int i = std::max(land, 0); i < end && land >= 0; ++i) {
        const double a = appliedHzFrom(o[i].tgt, o[i].cnt, amount);
        if (a > 0.0)
            maxErr = std::max(
                maxErr, std::abs(cents(static_cast<float>(a), static_cast<float>(sp.sungHz))));
        maxRes = std::max(maxRes, std::abs(static_cast<double>(o[i].cnt)));
    }
    r.maxAppliedErr = maxErr;
    r.maxResid = maxRes;
    r.transitions = transitionsIn(trs, sp.c0, end);
    return r;
}

void printRows(const char* title, const std::vector<NoteRow>& rows) {
    std::printf("  %s\n", title);
    std::printf("  | note | onset retune ms | settle ms | max applied-F0 err c | "
                "max corr-ratio resid c | transitions |\n");
    std::printf("  |------|-----------------|-----------|----------------------|"
                "------------------------|-------------|\n");
    for (const NoteRow& r : rows)
        std::printf("  | %-4s | %15.1f | %9.1f | %20.1f | %22.1f | %11d |\n",
                    r.name, r.retuneMs, r.settleMs, r.maxAppliedErr, r.maxResid,
                    r.transitions);
}

// ---------------------------------------------------------------------------
// Battery F — staccato notes (card F): A3 -> C4 -> A3, 40 ms gaps inside the
// 150 ms breath window. A3->C4 is +300.1 cents — JUST over the 300 c gate, so
// every onset exercises the confirmed-jump cadence (pending frame, then the
// agreeing frame reseeds the ring).
// ---------------------------------------------------------------------------
bool testStaccatoSeam() {
    std::printf("F1. staccato transitions (seam: A3 85 -> C4 64 -> A3 85 ms, 43 ms gaps)\n");
    dautotune::PitchCorrector dsp(kSr);
    dsp.setParameters(0, 0, 1.0f, 35.0f, 0.0f);  // C major; A3, C4 are members
    const float kA3 = 220.0f, kC4 = static_cast<float>(etHz(60));  // 261.6256

    const struct { float hz; int n; } script[] = {
        {kA3, 8}, {0.0f, 4}, {kC4, 6}, {0.0f, 4}, {kA3, 8}};
    const NoteSpan spans[] = {{0, 8, kA3, kA3, "A3"},
                              {12, 18, kC4, etHz(60), "C4"},
                              {22, 30, kA3, kA3, "A3"}};
    std::vector<Obs> o;
    for (const auto& s : script)
        for (int i = 0; i < s.n; ++i)
            o.push_back(feedObs(dsp, s.hz, s.hz > 0.0f ? 0.9f : 0.0f));
    const std::vector<Trans> trs = memberTransitions(o);

    check(!o[0].vox, "F1: cold-start frame unvoiced");
    check(o[2].vox && std::abs(cents(o[2].tgt, kA3)) <= 5.0,
          "F1: A3 voiced + on-member by 2nd update");
    for (int i = 8; i < 12; ++i)
        check(!o[i].vox, "F1: unvoiced on every gap update (i)");
    check(o[12].vox, "F1: voiced held on first C4 frame (no unvoiced flap)");
    check(std::abs(cents(o[12].tgt, static_cast<float>(etHz(60)))) > 5.0,
          "F1: target NOT on C4 during the pending frame (frozen)");
    check(std::abs(cents(o[13].tgt, static_cast<float>(etHz(60)))) <= 5.0,
          "F1: C4 target lands on the 2nd new-note update (~21 ms cadence)");
    for (int i = 13; i < 18; ++i)
        check(std::abs(cents(o[i].tgt, static_cast<float>(etHz(60)))) <= 5.0,
              "F1: C4 target stable through note end (no flip)");
    check(o[22].vox && std::abs(cents(o[22].tgt, kA3)) > 5.0,
          "F1: voiced held on first A3 return frame, target still pending");
    check(std::abs(cents(o[23].tgt, kA3)) <= 5.0,
          "F1: A3 target lands on the 2nd update of the return");
    check(trs.size() == 2, "F1: exactly 2 member transitions (A3->C4->A3)");

    std::vector<NoteRow> rows;
    for (const NoteSpan& sp : spans)
        rows.push_back(analyzeSpan(o, trs, sp, 1.0f, /*admitIsStart=*/true));
    printRows("F1 seam per-note metrics:", rows);
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const NoteRow& r = rows[i];
        check(r.retuneMs >= 0.0 && r.retuneMs <= 21.4,
              "F1: onset retune within confirmed-jump cadence (<= 2 updates)");
        // First note voices cold (0->member isn't a member switch); later
        // notes must retune exactly once — a second flip would be chatter.
        check(r.transitions == (i == 0 ? 0 : 1),
              "F1: exactly one transition per note (no chatter)");
    }
    return true;
}

bool testStaccatoAudio() {
    std::printf("F2. staccato transitions (real detector, harmonic voice)\n");
    // Same layout as F1 in chunks: 10+4+6+4+10 x 10.667 ms ~= 107+43+64+43+107.
    const float kA3 = 220.0f;
    const double kC4 = etHz(60);
    const struct { double hz; int chunks; } layout[] = {
        {kA3, 10}, {0.0, 4}, {kC4, 6}, {0.0, 4}, {kA3, 10}};
    const NoteSpan spans[] = {{0, 10, kA3, kA3, "A3"},
                              {14, 20, kC4, kC4, "C4"},
                              {24, 34, kA3, kA3, "A3"}};

    std::vector<float> sig;
    int noteIdx = 0;
    for (const auto& s : layout) {
        const std::size_t n = static_cast<std::size_t>(s.chunks) * kChunk;
        if (s.hz <= 0.0) {
            sig.resize(sig.size() + n, 0.0f);
        } else {
            corpus::Rng rng(0xD2E57ACC + 0x9E37 * noteIdx);
            const double hz = s.hz;
            std::vector<float> v =
                voice(n, [hz](double) { return hz; }, 6, 0.3f, rng);
            edgeFades(v);
            sig.insert(sig.end(), v.begin(), v.end());
        }
        ++noteIdx;
    }

    dautotune::PitchCorrector dsp(kSr);
    dsp.setParameters(0, 0, 1.0f, 35.0f, 0.0f);
    const std::vector<Obs> o = runAudio(dsp, sig);
    const std::vector<Trans> trs = memberTransitions(o);

    std::printf("  per-chunk telemetry (det/tgt/vox/corr-c):\n");
    for (std::size_t i = 0; i < o.size(); ++i)
        std::printf("    %2zu det=%7.1f tgt=%7.1f vox=%d cnt=%+6.1f\n", i,
                    static_cast<double>(o[i].det), static_cast<double>(o[i].tgt),
                    o[i].vox ? 1 : 0, static_cast<double>(o[i].cnt));

    // A 43 ms gap is shorter than the 42.7 ms analysis window, so the flag may
    // stay voiced on window tail energy — the Schmitt-band requirement is that
    // it never FLAPS: at most one drop + one re-voice across the whole gap.
    auto flapCount = [&o](int a, int b) {
        int n = 0;
        for (int i = a + 1; i <= b; ++i)
            if (o[i].vox != o[i - 1].vox) ++n;
        return n;
    };
    check(flapCount(9, 15) <= 2, "F2: <= 2 voiced-flag transitions across gap 1");
    check(flapCount(19, 25) <= 2, "F2: <= 2 voiced-flag transitions across gap 2");
    check(trs.size() == 2, "F2: exactly 2 member transitions end to end");
    check(o[33].vox && std::abs(cents(o[33].tgt, kA3)) <= 5.0,
          "F2: tail frame voiced and on A3");

    std::vector<NoteRow> rows;
    for (const NoteSpan& sp : spans)
        rows.push_back(analyzeSpan(o, trs, sp, 1.0f, /*admitIsStart=*/false));
    printRows("F2 audio per-note metrics:", rows);
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const NoteRow& r = rows[i];
        check(r.retuneMs >= 0.0 && r.retuneMs <= 21.4,
              "F2: onset retune <= 2 updates after first admitted detection");
        check(r.transitions == (i == 0 ? 0 : 1),
              "F2: exactly one member transition per note");
        check(r.maxResid >= 0.0 && r.maxResid <= 10.0,
              "F2: correction-ratio residual <= 10 c after landing");
    }
    return true;
}

// ---------------------------------------------------------------------------
// Battery G — scale-boundary vibrato (card G). C major: near A3 the members
// are G3 (55) / A3 (57) / B3 (59) with decision midpoints at +-100 cents, so a
// +-50-cent vibrato on A3 cannot cross. The crossing carrier is therefore the
// G3<->A3 boundary itself (G#3, 207.65 Hz); the grazing carrier sits 55 cents
// above G3 so only the vibrato peak pokes ~5 cents into the A3 basin — the
// case most likely to expose target chatter.
// ---------------------------------------------------------------------------

// Feed a vibrato contour through the seam for `n` updates and collect Obs.
std::vector<Obs> vibratoSeam(dautotune::PitchCorrector& dsp, double carrier,
                           double depthC, double rateHz, int n, double jitterC,
                           corpus::Rng* rng) {
    std::vector<Obs> out;
    for (int i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) * kHopMs / 1000.0;
        const double centsOff =
            depthC * std::sin(2.0 * 3.14159265358979323846 * rateHz * t) +
            (rng ? jitterC * (2.0 * rng->nextUniform() - 1.0) : 0.0);
        const float hz = static_cast<float>(carrier * std::pow(2.0, centsOff / 1200.0));
        out.push_back(feedObs(dsp, hz, 0.9f));
    }
    return out;
}

// Interior dwell between consecutive member transitions (excludes the edges
// where a run starts/ends mid-dwell).
int minDwell(const std::vector<Trans>& t, int fromIdx, int toIdx) {
    int d = 1 << 30;
    for (std::size_t i = 1; i < t.size(); ++i)
        if (t[i].idx > fromIdx && t[i].idx < toIdx)
            d = std::min(d, t[i].idx - t[i - 1].idx);
    return d == (1 << 30) ? -1 : d;
}

bool testVibrato() {
    std::printf("G. scale-boundary vibrato (C major, G3/A3 boundary at G#3 = 207.65 Hz)\n");
    constexpr double kG3 = 195.9977, kA3 = 220.0, kGS3 = 207.6523;
    const int n = 96;  // ~1.02 s = ~5.1 vibrato cycles at 5 Hz
    const double cycles = n * kHopMs / 1000.0 * 5.0;

    // G1: non-crossing — A3 +-50 cents, basins edges at +-100 cents.
    {
        dautotune::PitchCorrector dsp(kSr);
        dsp.setParameters(0, 0, 1.0f, 35.0f, 0.0f);
        corpus::Rng rng(0x6A11E7);
        const std::vector<Obs> o = vibratoSeam(dsp, kA3, 50.0, 5.0, n, 4.0, &rng);
        const std::vector<Trans> trs = memberTransitions(o);
        double lo = 1e9, hi = -1e9;
        for (const Obs& x : o)
            if (x.det > 0.0f) {
                lo = std::min(lo, static_cast<double>(x.det));
                hi = std::max(hi, static_cast<double>(x.det));
            }
        std::printf("  G1 non-crossing: det span %.1f..%.1f Hz, transitions %zu\n",
                    lo, hi, trs.size());
        check(trs.empty(), "G1: +-50 c vibrato on A3 never leaves the A3 basin");
        check(lo < 214.5 && hi > 225.5,
              "G1: detectedHz stream tracks the full +-50 c swing (continuous)");
    }

    // G2: crossing — carrier ON the G3/A3 boundary, ~half-cycle dwell per side.
    {
        dautotune::PitchCorrector dsp(kSr);
        dsp.setParameters(0, 0, 1.0f, 35.0f, 0.0f);
        const std::vector<Obs> o = vibratoSeam(dsp, kGS3, 50.0, 5.0, n, 0.0, nullptr);
        const std::vector<Trans> trs = memberTransitions(o);
        const int dwell = minDwell(trs, 5, n - 5);
        std::printf(
            "  G2 crossing: %zu transitions over %.1f cycles, min interior dwell %d upd\n",
            trs.size(), cycles, dwell);
        for (const Trans& t : trs) {
            const bool gUp = std::abs(cents(t.fromHz, static_cast<float>(kG3))) < 10.0 &&
                             std::abs(cents(t.toHz, static_cast<float>(kA3))) < 10.0;
            const bool gDn = std::abs(cents(t.fromHz, static_cast<float>(kA3))) < 10.0 &&
                             std::abs(cents(t.toHz, static_cast<float>(kG3))) < 10.0;
            check(gUp || gDn, "G2: every transition is a G3<->A3 member switch");
        }
        check(dwell < 0 || dwell >= 4,
              "G2: no flip-back within half a cycle of a crossing (no chatter)");
        check(chatterWindows(trs, 9, 3) == 0, "G2: < 3 transitions per half-cycle");
    }

    // G3: crossing + detector-like jitter — the chatter probe.
    {
        dautotune::PitchCorrector dsp(kSr);
        dsp.setParameters(0, 0, 1.0f, 35.0f, 0.0f);
        corpus::Rng rng(0xC4055);
        const std::vector<Obs> o = vibratoSeam(dsp, kGS3, 50.0, 5.0, n, 4.0, &rng);
        const std::vector<Trans> trs = memberTransitions(o);
        const int dwell = minDwell(trs, 5, n - 5);
        std::printf("  G3 crossing+jitter: %zu transitions, min interior dwell %d upd\n",
                    trs.size(), dwell);
        check(chatterWindows(trs, 9, 3) == 0,
              "G3: jitter near the boundary produces no flip-flop run");
    }

    // G4: grazing — only the vibrato peak pokes ~5 c into the A3 basin.
    {
        const double grazeCarrier = kG3 * std::pow(2.0, 55.0 / 1200.0);  // +55 c on G3
        dautotune::PitchCorrector dsp(kSr);
        dsp.setParameters(0, 0, 1.0f, 35.0f, 0.0f);
        corpus::Rng rng(0x6B42);
        const std::vector<Obs> o = vibratoSeam(dsp, grazeCarrier, 50.0, 5.0, n, 4.0, &rng);
        const std::vector<Trans> trs = memberTransitions(o);
        std::printf("  G4 graze (peak ~5 c past boundary): %zu transitions\n", trs.size());
        // A clean graze is enter+exit = a transition pair; >= 3 inside one
        // half-cycle window is flip-flopping.
        check(chatterWindows(trs, 9, 3) == 0,
              "G4: grazes are single pairs, never a flip-flop run");
    }

    // G5: audio level — harmonic voice through the real detector, both a
    // non-crossing and a crossing carrier.
    struct Case {
        const char* name;
        double carrier;
        int win;                 // window in updates
        int minT;                // transitions inside win that count as chatter
        double expectT0, expectT1;  // allowed total-transition band
    };
    const Case cases[] = {
        {"G5a audio non-crossing A3+-50c", kA3, 9, 3, 0.0, 0.0},
        {"G5b audio crossing G#3+-50c", kGS3, 9, 3, 5.0, 14.0},
        {"G5c audio graze G3+55c+-50c", kG3 * std::pow(2.0, 55.0 / 1200.0), 9, 3,
         0.0, 14.0},
    };
    for (const Case& cs : cases) {
        corpus::Rng rng(0x0B51DE + static_cast<unsigned>(cs.carrier));
        const double carrier = cs.carrier;
        std::vector<float> sig =
            voice(static_cast<std::size_t>(n) * kChunk,
                  [carrier](double t) {
                      return carrier * std::pow(2.0, 50.0 * std::sin(kTwoPi * 5.0 * t) / 1200.0);
                  },
                  6, 0.3f, rng);
        dautotune::PitchCorrector dsp(kSr);
        dsp.setParameters(0, 0, 1.0f, 35.0f, 0.0f);
        const std::vector<Obs> o = runAudio(dsp, sig);
        const std::vector<Trans> trs = memberTransitions(o);
        const int dwell = minDwell(trs, 10, n - 10);
        std::printf("  %s: %zu transitions, min interior dwell %d upd\n", cs.name,
                    trs.size(), dwell);
        check(trs.size() >= static_cast<std::size_t>(cs.expectT0) &&
                  trs.size() <= static_cast<std::size_t>(cs.expectT1),
              "G5: transition count inside the expected band");
        check(chatterWindows(trs, cs.win, cs.minT) == 0,
              "G5: no flip-flop run inside any half-cycle window");
    }
    return true;
}

// ---------------------------------------------------------------------------
// Battery H — 300..1200-cent intervals (card H): A3 -> D4 (+500) -> A4 (+1200,
// i.e. +700 above D4) -> A3 (-1200). Every jump clears the 300-cent gate, so
// the pending-jump path — not the sub-gate instant path — is what must retune
// within the 2-update confirmed-jump cadence. The -1200-cent octave return is
// a musical interval: the tracker must accept it as a note change, not an
// octave-error reject.
// ---------------------------------------------------------------------------
bool testIntervalsSeam() {
    std::printf("H1. 300-1200 c interval jumps (seam: A3 -> D4 -> A4 -> A3)\n");
    dautotune::PitchCorrector dsp(kSr);
    dsp.setParameters(0, 0, 1.0f, 35.0f, 0.0f);
    const double kA3 = 220.0, kD4 = etHz(62), kA4 = 440.0;
    const struct { double hz; int n; const char* name; } script[] = {
        {kA3, 4, "A3"}, {kD4, 4, "D4"}, {kA4, 4, "A4"}, {kA3, 6, "A3"}};

    std::vector<Obs> o;
    std::vector<NoteSpan> spans;
    int pos = 0;
    for (const auto& s : script) {
        spans.push_back({pos, pos + s.n, s.hz, s.hz, s.name});
        for (int i = 0; i < s.n; ++i)
            o.push_back(feedObs(dsp, static_cast<float>(s.hz), 0.9f));
        pos += s.n;
    }
    const std::vector<Trans> trs = memberTransitions(o);

    // Each jump: first frame pending (target frozen on the OLD member), second
    // frame reseeds and retunes. Jump frames are 4, 8, 12.
    const struct { int pendingIdx; int landIdx; double oldHz; double newHz; } jumps[] = {
        {4, 5, kA3, kD4}, {8, 9, kD4, kA4}, {12, 13, kA4, kA3}};
    for (const auto& j : jumps) {
        check(o[j.pendingIdx].vox &&
                  std::abs(cents(o[j.pendingIdx].tgt, static_cast<float>(j.oldHz))) <= 5.0,
              "H1: pending frame holds voiced, target frozen on old member");
        check(std::abs(cents(o[j.pendingIdx].det, static_cast<float>(j.newHz))) <= 20.0,
              "H1: detectedHz stream already reports the new note (live)");
        check(std::abs(cents(o[j.landIdx].tgt, static_cast<float>(j.newHz))) <= 5.0,
              "H1: target lands on the new member on the 2nd update");
    }
    check(trs.size() == 3, "H1: exactly 3 member transitions (one per jump)");
    check(std::abs(cents(o[17].tgt, static_cast<float>(kA3))) <= 5.0,
          "H1: -1200 c octave return accepted as a note change (A3, no error)");

    std::vector<NoteRow> rows;
    for (const NoteSpan& sp : spans)
        rows.push_back(analyzeSpan(o, trs, sp, 1.0f, /*admitIsStart=*/true));
    printRows("H1 seam per-note metrics:", rows);
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const NoteRow& r = rows[i];
        check(r.retuneMs >= 0.0 && r.retuneMs <= 21.4,
              "H1: every jump retunes within the 2-update cadence");
        check(r.transitions == (i == 0 ? 0 : 1),
              "H1: exactly one transition per note (no chatter)");
        check(r.maxResid >= 0.0 && r.maxResid <= 5.0,
              "H1: correction-ratio residual <= 5 c after landing");
    }
    return true;
}

bool testIntervalsAudio() {
    std::printf("H2. 300-1200 c interval jumps (real detector, 32 ms gaps)\n");
    const double kA3 = 220.0, kD4 = etHz(62), kA4 = 440.0;
    const struct { double hz; int chunks; } layout[] = {
        {kA3, 10}, {0.0, 3}, {kD4, 9}, {0.0, 3}, {kA4, 9}, {0.0, 3}, {kA3, 10}};
    const NoteSpan spans[] = {{0, 10, kA3, kA3, "A3"},
                              {13, 22, kD4, kD4, "D4"},
                              {25, 34, kA4, kA4, "A4"},
                              {37, 47, kA3, kA3, "A3"}};

    std::vector<float> sig;
    int noteIdx = 0;
    for (const auto& s : layout) {
        const std::size_t n = static_cast<std::size_t>(s.chunks) * kChunk;
        if (s.hz <= 0.0) {
            sig.resize(sig.size() + n, 0.0f);
        } else {
            corpus::Rng rng(0xD2E1117 + 0x9E37 * noteIdx);
            const double hz = s.hz;
            std::vector<float> v =
                voice(n, [hz](double) { return hz; }, 6, 0.3f, rng);
            edgeFades(v);
            sig.insert(sig.end(), v.begin(), v.end());
        }
        ++noteIdx;
    }

    dautotune::PitchCorrector dsp(kSr);
    dsp.setParameters(0, 0, 1.0f, 35.0f, 0.0f);
    const std::vector<Obs> o = runAudio(dsp, sig);
    const std::vector<Trans> trs = memberTransitions(o);

    check(trs.size() == 3, "H2: exactly 3 member transitions end to end");
    std::vector<NoteRow> rows;
    for (const NoteSpan& sp : spans)
        rows.push_back(analyzeSpan(o, trs, sp, 1.0f, /*admitIsStart=*/false));
    printRows("H2 audio per-note metrics:", rows);
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const NoteRow& r = rows[i];
        check(r.retuneMs >= 0.0 && r.retuneMs <= 21.4,
              "H2: every jump retunes <= 2 updates after first detection");
        check(r.transitions == (i == 0 ? 0 : 1),
              "H2: exactly one member transition per note");
        check(r.maxResid >= 0.0 && r.maxResid <= 10.0,
              "H2: correction-ratio residual <= 10 c after landing");
    }
    const std::size_t last = o.size() - 1;
    check(std::abs(cents(o[last].tgt, static_cast<float>(kA3))) <= 5.0,
          "H2: octave-down return lands on A3 (interval, not octave error)");
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
        {"F1 staccato seam", testStaccatoSeam},
        {"F2 staccato audio", testStaccatoAudio},
        {"G scale-boundary vibrato", testVibrato},
        {"H1 intervals seam", testIntervalsSeam},
        {"H2 intervals audio", testIntervalsAudio},
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

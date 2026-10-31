// D5 harness unit tests — synthetic fixtures ONLY (card G2-D5, HARD-3).
//
// These tests validate the loader/parser/metric MACHINERY on tiny generated
// WAVs with known answers. They are deliberately NOT real-voice evidence:
// nothing here says PASS about real voices. Fixture table (known answers):
//
//   fixture              | known answer                        | metric under test
//   ---------------------+-------------------------------------+-------------------
//   sha256 vectors       | FIPS 180-4 test strings             | digest correctness
//   wav round-trip       | samples back within 1 LSB           | writeWav16/readWav16
//   wav reject           | stereo / 8-bit / 96k / >15 s        | schema enforcement
//   manifest good/bad    | 2 rows parse; 3 bad rows rejected   | JSONL parser
//   annotation good/bad  | 2 segs parse; overlap/bad-row reject| CSV parser
//   quiet sine           | pump std ~0 dB                      | pumpDepth floor
//   step pump +-2 dB     | pump std = 2.000 dB exactly         | pumpDepth recovery
//   AM pump m=0.3 @2 Hz  | std of 20log10(1+m cos), numeric    | pumpDepth recovery
//   sine 220 Hz          | f0 220 +-0.5 Hz                     | measureF0
//   full chain 285 Hz    | out ~= D4 293.66, corr ratio > 0.5  | R1 machinery
//   formant voice        | F1/F2 centroids near 700/1200 +-60  | bandCentroid
//   impulse probe        | delay in the D4 class (2.27-4.17ms) | impulseCentroid

#include "PitchCorrector.h"
#include "corpus_gen.h"
#include "corpus_loader.h"
#include "d5_measure.h"
#include "metrics.h"
#include "sha256.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace {

int gFailures = 0;

void check(bool ok, const std::string& what) {
    std::printf("  %s: %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (!ok) ++gFailures;
}

const std::filesystem::path kFix = std::filesystem::current_path() / "d5_fixture";

std::string fix(const std::string& name) { return (kFix / name).string(); }

void resetFixtures() {
    std::error_code ec;
    std::filesystem::remove_all(kFix, ec);
    std::filesystem::create_directories(kFix / "clips");
    std::filesystem::create_directories(kFix / "annotations");
}

// --- fixtures -----------------------------------------------------------------

std::vector<float> sineN(std::size_t n, int sr, double f0, float amp) {
    std::vector<float> out(n);
    for (std::size_t i = 0; i < n; ++i)
        out[i] = amp * static_cast<float>(
            std::sin(2.0 * d5m::kPi * f0 * static_cast<double>(i) / sr));
    return out;
}

// Stepwise pump: gain alternates gUp/gDown every dwellSec (aligned to the 50 ms
// metric windows by using a dwell that is a multiple of them).
std::vector<float> stepPump(int sr, double dwellSec, float gUp, float gDown,
                            int dwells, double f0, float amp) {
    const std::size_t d = static_cast<std::size_t>(dwellSec * sr);
    const std::size_t n = d * static_cast<std::size_t>(dwells);
    std::vector<float> out = sineN(n, sr, f0, amp);
    for (int k = 0; k < dwells; ++k) {
        const float g = (k % 2 == 0) ? gUp : gDown;
        for (std::size_t i = static_cast<std::size_t>(k) * d;
             i < static_cast<std::size_t>(k + 1) * d; ++i)
            out[i] *= g;
    }
    return out;
}

// Sinusoidal amplitude pump: carrier * (1 + m cos(2 pi fm t)).
std::vector<float> amPump(int sr, double f0, float amp, double m, double fm,
                          double secs) {
    const std::size_t n = static_cast<std::size_t>(secs * sr);
    std::vector<float> out(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) / sr;
        out[i] = amp * static_cast<float>((1.0 + m * std::cos(2.0 * d5m::kPi * fm * t)) *
                                          std::sin(2.0 * d5m::kPi * f0 * t));
    }
    return out;
}

// Reference std of 20*log10(1 + m cos(theta)) over one period — dense numeric
// quadrature, independent of the metric's 50 ms windowing.
double refAmStdDb(double m) {
    constexpr int N = 4000;
    double mean = 0.0;
    std::vector<double> v(N);
    for (int i = 0; i < N; ++i) {
        const double th = 2.0 * d5m::kPi * i / N;
        v[i] = 20.0 * std::log10(1.0 + m * std::cos(th));
        mean += v[i];
    }
    mean /= N;
    double var = 0.0;
    for (const double x : v) var += (x - mean) * (x - mean);
    return std::sqrt(var / N);
}

void writeText(const std::string& path, const std::string& body) {
    FILE* f = std::fopen(path.c_str(), "wb");
    std::fwrite(body.data(), 1, body.size(), f);
    std::fclose(f);
}

// --- 1. SHA-256 -----------------------------------------------------------------

void testSha() {
    std::printf("1. SHA-256 (FIPS 180-4 vectors + file hash)\n");
    {
        d5sha::Sha256 s;
        check(s.hexDigest() ==
                  "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
              "sha256(\"\")");
    }
    {
        d5sha::Sha256 s;
        s.update("abc", 3);
        check(s.hexDigest() ==
                  "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
              "sha256(\"abc\")");
    }
    {
        d5sha::Sha256 s;
        const std::string msg =
            "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
        // Feed in odd-sized pieces to exercise the streaming path.
        for (std::size_t i = 0; i < msg.size(); i += 7)
            s.update(msg.data() + i, std::min<std::size_t>(7, msg.size() - i));
        check(s.hexDigest() ==
                  "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
              "sha256(56-byte vector, chunked)");
    }
    writeText(fix("hash_me.bin"), "abc");
    check(d5sha::fileSha256(fix("hash_me.bin")) ==
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
          "fileSha256 matches sha256(\"abc\")");
}

// --- 2. WAV --------------------------------------------------------------------

void testWav() {
    std::printf("2. WAV 16-bit PCM mono (round-trip + schema rejection)\n");
    const int sr = 48000;
    std::vector<float> x = sineN(static_cast<std::size_t>(0.5 * sr), sr, 220.0, 0.3f);
    std::string err;
    check(d5corpus::writeWav16(fix("ok.wav"), x, sr, &err), "writeWav16 writes");
    std::vector<float> y;
    int fs = 0;
    check(d5corpus::readWav16(fix("ok.wav"), &y, &fs, &err) && fs == sr,
          "readWav16 reads back, fs correct");
    bool close = y.size() == x.size();
    if (close)
        for (std::size_t i = 0; i < x.size(); ++i)
            if (std::abs(y[i] - x[i]) > 1.0f / 32767.0f + 1e-6f) close = false;
    check(close, "round-trip samples within 1 LSB");

    // Stereo file: hand-built minimal header, must be rejected.
    {
        const std::string p = fix("stereo.wav");
        std::vector<float> inter(static_cast<std::size_t>(0.1 * sr) * 2, 0.0f);
        // Patch writeWav16 output into stereo by writing a manual 2ch file.
        FILE* f = std::fopen(p.c_str(), "wb");
        const std::uint32_t dataBytes = static_cast<std::uint32_t>(inter.size() * 2);
        auto w32 = [&](std::uint32_t v) {
            for (int s = 0; s < 32; s += 8) std::fputc((v >> s) & 0xFF, f);
        };
        auto w16 = [&](std::uint16_t v) {
            for (int s = 0; s < 16; s += 8) std::fputc((v >> s) & 0xFF, f);
        };
        std::fwrite("RIFF", 1, 4, f); w32(36 + dataBytes); std::fwrite("WAVE", 1, 4, f);
        std::fwrite("fmt ", 1, 4, f); w32(16); w16(1); w16(2);
        w32(static_cast<std::uint32_t>(sr)); w32(static_cast<std::uint32_t>(sr * 4));
        w16(4); w16(16);
        std::fwrite("data", 1, 4, f); w32(dataBytes);
        for (std::size_t i = 0; i < inter.size(); ++i) w16(0);
        std::fclose(f);
        check(!d5corpus::readWav16(p, &y, &fs, &err), "stereo WAV rejected");
    }
    // Wrong rate: 96 kHz must be rejected (schema is 44100/48000 only).
    check(d5corpus::writeWav16(fix("hi96.wav"), x, 96000, &err),
          "writeWav16 96k fixture writes");
    check(!d5corpus::readWav16(fix("hi96.wav"), &y, &fs, &err),
          "96 kHz WAV rejected (schema rate)");
    // > 15 s must be rejected.
    std::vector<float> longx = sineN(static_cast<std::size_t>(15.2 * sr), sr, 220.0, 0.2f);
    check(d5corpus::writeWav16(fix("toolong.wav"), longx, sr, &err),
          "writeWav16 >15 s fixture writes");
    check(!d5corpus::readWav16(fix("toolong.wav"), &y, &fs, &err),
          ">15 s WAV rejected (schema bound)");
}

// --- 3. manifest + annotation ---------------------------------------------------

void testManifestAnnotation() {
    std::printf("3. manifest.jsonl + annotation CSV parse (good + malformed)\n");
    // Two valid rows: one with annotation, one with annotation:null.
    writeText(fix("manifest.jsonl"),
              "# comment line\n"
              "{\"clip_id\":\"clip_a\",\"file\":\"clips/a.wav\",\"sha256\":\"" +
                  d5sha::fileSha256(fix("ok.wav")) +
                  "\",\"fs\":48000,\"singer\":\"fx\",\"context\":\"long_tones\","
                  "\"technique\":\"straight\",\"vowel\":\"a\",\"source\":\"vocalset\","
                  "\"annotation\":\"annotations/a.csv\"}\n"
              "{\"clip_id\":\"clip_b\",\"file\":\"clips/a.wav\",\"sha256\":\"" +
                  d5sha::fileSha256(fix("ok.wav")) +
                  "\",\"fs\":48000,\"singer\":\"m2\",\"context\":\"scales\","
                  "\"technique\":\"vibrato\",\"vowel\":\"o\",\"source\":\"owner\","
                  "\"annotation\":null}\n");
    std::vector<d5corpus::ClipMeta> rows;
    check(d5corpus::loadManifest(kFix.string(), &rows) && rows.size() == 2,
          "manifest: 2 valid rows parsed");
    if (rows.size() == 2) {
        check(rows[0].clipId == "clip_a" && rows[0].context == "long_tones" &&
                  rows[0].annotation == "annotations/a.csv" &&
                  rows[1].annotation.empty(),
              "manifest: fields + null annotation decoded");
    }
    // Malformed lines must each be reported and fail the parse.
    writeText(fix("manifest.jsonl"),
              "{\"clip_id\":\"ok\",\"file\":\"clips/a.wav\",\"sha256\":\"" +
                  d5sha::fileSha256(fix("ok.wav")) +
                  "\",\"fs\":48000,\"singer\":\"fx\",\"context\":\"songs\","
                  "\"technique\":\"belt\",\"vowel\":\"e\",\"source\":\"vocalset\","
                  "\"annotation\":null}\n"
              "{\"clip_id\":\"bad_fs\",\"file\":\"clips/a.wav\",\"sha256\":\"" +
                  d5sha::fileSha256(fix("ok.wav")) +
                  "\",\"fs\":22050,\"singer\":\"fx\",\"context\":\"songs\","
                  "\"technique\":\"belt\",\"vowel\":\"e\",\"source\":\"vocalset\","
                  "\"annotation\":null}\n"
              "{not json at all}\n"
              "{\"clip_id\":\"escape\",\"file\":\"../a.wav\",\"sha256\":\"" +
                  d5sha::fileSha256(fix("ok.wav")) +
                  "\",\"fs\":48000,\"singer\":\"fx\",\"context\":\"songs\","
                  "\"technique\":\"belt\",\"vowel\":\"e\",\"source\":\"vocalset\","
                  "\"annotation\":null}\n");
    rows.clear();
    check(!d5corpus::loadManifest(kFix.string(), &rows) && rows.size() == 1,
          "manifest: bad fs / bad json / path escape rejected, good row kept");

    // Annotation CSV: header + 2 ordered rows.
    writeText(fix("ann_ok.csv"),
              "t_start_s,t_end_s,f0_hz,midi\n"
              "0.500,1.500,220.00,57\n"
              "1.700,2.600,246.94,59\n");
    std::vector<d5corpus::NoteSeg> segs;
    std::string err;
    check(d5corpus::loadAnnotation(fix("ann_ok.csv"), &segs, &err) &&
              segs.size() == 2 && std::abs(segs[1].f0Hz - 246.94) < 0.01,
          "annotation: 2 segments parsed");
    writeText(fix("ann_bad.csv"), "0.500,1.500,220.00,57\n1.400,2.000,246.94,59\n");
    check(!d5corpus::loadAnnotation(fix("ann_bad.csv"), &segs, &err),
          "annotation: overlapping segments rejected");
    writeText(fix("ann_zero.csv"), "0.500,1.500,0.0,57\n");
    check(!d5corpus::loadAnnotation(fix("ann_zero.csv"), &segs, &err),
          "annotation: f0<=0 rejected");
}

// --- 4. pump metric: known injected depths ---------------------------------------

void testPumpMetric() {
    std::printf("4. envelope pump metric (known injected depths)\n");
    const int sr = 48000;

    // Quiet steady sine: floor ~0 dB.
    const std::vector<float> q = sineN(static_cast<std::size_t>(2.0 * sr), sr, 220.0, 0.3f);
    const d5m::PumpDepth qp = d5m::pumpDepth(q.data(), q.size(), sr);
    std::printf("  quiet sine: std %.3f dB, p95 dEnv %.3f dB (%zu windows)\n", qp.stdDb,
                qp.p95DeltaDb, qp.windows);
    check(qp.stdDb < 0.15, "pump: quiet sine std < 0.15 dB");

    // Step pump: 0.5 s dwells alternating 0 dB / -4 dB over 8 dwells.
    // Exact expected std = 2.000 dB (two levels, equal dwell); transition
    // windows edge-align since 0.5 s is a multiple of the 50 ms window.
    const float gUp = 1.0f;
    const float gDown = static_cast<float>(std::pow(10.0, -4.0 / 20.0));
    const std::vector<float> sp = stepPump(sr, 0.5, gUp, gDown, 8, 220.0, 0.25f);
    const d5m::PumpDepth spp = d5m::pumpDepth(sp.data(), sp.size(), sr);
    std::printf("  step pump (4.0 dB p-p): std %.3f dB (expected 2.000), "
                "p95 dEnv %.3f dB\n",
                spp.stdDb, spp.p95DeltaDb);
    check(std::abs(spp.stdDb - 2.0) <= 0.05,
          "pump: step fixture recovers 2.0 dB std within 0.05");

    // AM pump m=0.3 at 2 Hz: expected std = std of 20log10(1+m cos) by dense
    // quadrature; the 50 ms window attenuates the 2 Hz modulation < 2 %.
    const double m = 0.3;
    const std::vector<float> am = amPump(sr, 220.0, 0.25f, m, 2.0, 4.0);
    const d5m::PumpDepth amp2 = d5m::pumpDepth(am.data(), am.size(), sr);
    const double ref = refAmStdDb(m);
    std::printf("  AM pump m=0.3 @2 Hz: std %.3f dB (reference %.3f), "
                "p95 dEnv %.3f dB\n",
                amp2.stdDb, ref, amp2.p95DeltaDb);
    check(std::abs(amp2.stdDb - ref) <= 0.10 * ref + 0.10,
          "pump: AM fixture recovers injected depth within 10% + 0.1 dB");

    // Input-corrected excess (the R3 report quantity): pumped - quiet.
    std::printf("  input-corrected pump excess (AM vs quiet): %+.2f dB\n",
                amp2.stdDb - qp.stdDb);
    check(amp2.stdDb - qp.stdDb > ref * 0.8,
          "pump: excess over quiet carrier is positive and near reference");
}

// --- 5. spectral machinery --------------------------------------------------------

void testSpectral() {
    std::printf("5. spectral machinery (measureF0, bandCentroid, full chain)\n");
    const int sr = 48000;
    const std::vector<float> s = sineN(static_cast<std::size_t>(0.4 * sr), sr, 220.0, 0.3f);
    const double f = d5m::measureF0(s.data(), static_cast<int>(s.size()), sr);
    std::printf("  sine 220: measured %.3f Hz\n", f);
    check(std::abs(f - 220.0) <= 0.5, "measureF0: sine 220 within 0.5 Hz");

    // Two-formant voice: centroids must land near the bumps.
    corpus::Rng rng(0xD5A11);
    const std::vector<double> w = d5m::formantVoice(220.0, 22);
    const std::vector<float> v =
        d5m::voice(static_cast<std::size_t>(1.0 * sr), sr, d5m::constF(220.0), 22, 0.3f,
                   rng, &w);
    const double f0v = d5m::measureF0(v.data() + sr / 2, static_cast<int>(0.3 * sr), sr);
    const double c1 = d5m::bandCentroid(v.data() + sr / 2, static_cast<int>(0.3 * sr), sr,
                                        f0v, 500.0, 900.0);
    const double c2 = d5m::bandCentroid(v.data() + sr / 2, static_cast<int>(0.3 * sr), sr,
                                        f0v, 1000.0, 1500.0);
    std::printf("  formant voice: f0 %.1f, F1 %.0f Hz (bump 700), F2 %.0f Hz "
                "(bump 1200)\n",
                f0v, c1, c2);
    check(std::abs(c1 - 700.0) <= 60.0 && std::abs(c2 - 1200.0) <= 60.0,
          "bandCentroid: F1/F2 within +-60 Hz of the bumps");

    // Full chain through the R1 machinery: sung 285 -> D4 293.66.
    corpus::Rng rng2(0xD5A12);
    const std::vector<float> sig = d5m::voice(
        static_cast<std::size_t>(1.2 * sr) / d5m::kChunk * d5m::kChunk, sr,
        d5m::constF(285.0), 6, 0.3f, rng2);
    const d5m::Rendered r = d5m::renderChain(sig, sr, {}, false);
    const std::size_t from = static_cast<std::size_t>(0.70 * sr);
    const double fout = d5m::measureF0(r.mono.data() + from, static_cast<int>(0.2 * sr), sr);
    const double fin = d5m::measureF0(sig.data() + from, static_cast<int>(0.2 * sr), sr);
    const double kD4 = 293.6647679;
    const double inErr = std::abs(d5m::cents(fin, kD4));
    const double outErr = std::abs(d5m::cents(fout, kD4));
    const double corr = (inErr - outErr) / std::max(1e-9, inErr);
    std::printf("  full chain 285->D4: in f0 %.2f (%.1f c off), out f0 %.2f "
                "(%.1f c off), correction ratio %.2f\n",
                fin, inErr, fout, outErr, corr);
    check(outErr <= 3.0, "full chain: output F0 within 3 c of D4 (D4-A parity)");
    check(corr >= 0.5, "full chain: correction ratio >= 0.5 on 52 c deviation");
}

// --- 6. delay machinery -------------------------------------------------------------

void testDelay() {
    std::printf("6. impulse-centroid delay machinery (D4 class 2.27-4.17 ms)\n");
    for (const int sr : {48000, 44100}) {
        const double d220 = d5m::impulseCentroidDelaySamp(sr, 220.0, 1.15f);
        const double d120 = d5m::impulseCentroidDelaySamp(sr, 120.0, 1.15f);
        const double ms220 = 1000.0 * d220 / sr, ms120 = 1000.0 * d120 / sr;
        std::printf("  sr %d: impulse delay %.0f samp (%.2f ms) @220 Hz, "
                    "%.0f samp (%.2f ms) @120 Hz\n",
                    sr, d220, ms220, d120, ms120);
        const double budget = sr / 220.0 + 1.0 + 96.0;
        check(d220 >= 0.0 && d220 <= budget && d120 >= 0.0 && d120 <= sr / 120.0 + 97.0,
              "impulse delay inside the D3 P+1 budget at both probes");
    }
}

}  // namespace

int main() {
    std::printf("D5 harness unit tests — SYNTHETIC FIXTURES ONLY\n");
    std::printf("These validate loader/parser/metric machinery; they are NOT "
                "real-voice evidence.\n\n");
    resetFixtures();
    testSha();
    std::printf("\n");
    testWav();
    std::printf("\n");
    testManifestAnnotation();
    std::printf("\n");
    testPumpMetric();
    std::printf("\n");
    testSpectral();
    std::printf("\n");
    testDelay();

    std::error_code ec;
    std::filesystem::remove_all(kFix, ec);

    if (gFailures) {
        std::printf("\nD5 HARNESS TESTS: %d FAILURES\n", gFailures);
        return 1;
    }
    std::printf("\nD5 HARNESS TESTS: ALL PASS\n");
    return 0;
}

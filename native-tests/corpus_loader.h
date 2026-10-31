#pragma once

// D5 real-voice corpus loader — schema lives in docs/corpus/README.md.
//
// Reads docs/corpus/manifest.jsonl (one flat JSON object per line), verifies
// each clip's SHA-256, decodes 16-bit PCM mono WAVs, and parses the per-clip
// annotation CSVs (t_start_s,t_end_s,f0_hz,midi). Test-only code — allocation
// and filesystem access are fine here (NOT the RT path).
//
// Discipline (card G2-D5, HARD-2): an absent or empty manifest is not an
// error — loadManifest returns an empty list and the R1–R5 batteries SKIP
// loudly. A present-but-corrupt corpus (bad JSON, SHA mismatch, wrong WAV
// format, >15 s, unsupported rate) is a hard error: every rejection is
// printed and loadManifest returns false.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace d5corpus {

// One manifest.jsonl row.
struct ClipMeta {
    std::string clipId;
    std::string file;        // path relative to the corpus dir, e.g. "clips/x.wav"
    std::string sha256;      // expected lowercase hex digest
    int fs = 0;              // 44100 or 48000
    std::string singer;
    std::string context;     // long_tones | scales | arpeggios | songs | exercises
    std::string technique;   // straight | vibrato | belt | lip_trill | ...
    std::string vowel;       // a | e | i | o | u | none
    std::string source;      // vocalset | owner | other:<desc>
    std::string annotation;  // annotation CSV path relative to corpus dir, "" = none
};

// One annotated note segment (annotation CSV row).
struct NoteSeg {
    double tStart = 0.0;  // seconds
    double tEnd = 0.0;
    double f0Hz = 0.0;    // annotated reference F0 (> 0; unvoiced gaps are
                          // simply absent from the CSV)
    int midi = 0;         // perceived note label
};

// A verified, decoded clip.
struct Clip {
    ClipMeta meta;
    std::vector<float> samples;  // float32 mono, [-1, 1]
    int fs = 0;
    std::vector<NoteSeg> notes;  // empty when meta.annotation == ""
};

// --- manifest ----------------------------------------------------------------

// Parses manifest.jsonl inside corpusDir. Returns true when the file is absent
// or empty (corpus not landed yet — callers then SKIP) and when every line
// parsed cleanly. Returns false on any malformed line (each error printed).
bool loadManifest(const std::string& corpusDir, std::vector<ClipMeta>* out);

// --- WAV ---------------------------------------------------------------------

// Decodes a 16-bit PCM mono WAV into [-1,1] floats. Enforces the schema:
// PCM format 1, mono, 16-bit, fs in {44100, 48000}, <= 15.0 s. On failure
// returns false and sets *err.
bool readWav16(const std::string& path, std::vector<float>* samples, int* fs,
               std::string* err);

// Writes a 16-bit PCM mono WAV (listening-pack export + fixture generation).
bool writeWav16(const std::string& path, const std::vector<float>& samples, int fs,
                std::string* err);

// --- annotations ---------------------------------------------------------------

// Parses t_start_s,t_end_s,f0_hz,midi rows (header line tolerated). Rows must
// be time-ordered and non-overlapping with t_end > t_start and f0_hz > 0.
bool loadAnnotation(const std::string& path, std::vector<NoteSeg>* out,
                    std::string* err);

// --- full clip load ------------------------------------------------------------

// Loads + verifies one clip: file exists, SHA-256 matches the manifest, WAV
// passes readWav16, annotation (if declared) parses. Returns false with *err
// set on any failure; the clip must be excluded from batteries.
bool loadClip(const std::string& corpusDir, const ClipMeta& meta, Clip* out,
              std::string* err);

// Resolves meta.file / meta.annotation against corpusDir; rejects absolute
// paths and ".." escapes.
bool resolveCorpusPath(const std::string& corpusDir, const std::string& rel,
                       std::string* out);

}  // namespace d5corpus

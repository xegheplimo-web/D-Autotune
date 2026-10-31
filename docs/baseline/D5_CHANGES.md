# D5 — Real-voice validation harness (card G2-D5, GPT round-4 D-015)

GPT round-4 verdict: G2 host-verified, **commercial release BLOCKED until
real-voice evidence**. This card lands the *license-vetted measurement
harness* only — no real singing is committed by this change (no network
audio access; synthesizing "singing" is forbidden). The VocalSet subset
(≥20 clips, ≥4 singers) + annotations + manifest land afterwards, matching
the schema in `docs/corpus/README.md`; the CI gate then produces the first
real numbers automatically.

`app/src/main/cpp/PitchCorrector.cpp/.h` **byte-identical** before/after:

- `PitchCorrector.cpp` = `ba78ed0bde310a781845bdefaec2840ebf0b055a5eb67bcae815ea80c136f83c`
- `PitchCorrector.h`   = `f0beab77c0ee1c3fe53353a82295c99c0656a08da43490f19d7f1d33f75929be`

## Harness design (all inside `native-tests/`)

| file | role |
|---|---|
| `sha256.{h,cpp}` | FIPS 180-4 SHA-256 for clip integrity (manifest pins each clip's digest) |
| `corpus_loader.{h,cpp}` | `manifest.jsonl` parser (flat JSON/line), `readWav16` (PCM16 mono, 44.1/48 k, ≤15 s), `writeWav16`, annotation CSV parser, `loadClip` (path-escape reject + SHA verify + WAV + annotation) |
| `d5_measure.h` | header-only shared metrics: `measureF0`, `goertzel`, `bandCentroid`, `maxStep`, `envDbSeries`/`pumpDepth`, `impulseCentroidDelaySamp`, `renderChain` (D4 host path: amount 1.0, retune 35 ms, C major, reverb 0, chunk 512, stereo) + per-chunk `detectedHz/voiced/targetHz` trace |
| `test_d5_harness.cpp` | machinery unit tests on synthetic fixtures (always runs) |
| `test_d5_realvoice.cpp` | batteries R1–R5 on `docs/corpus/`; `D5_CORPUS_DIR` env override supported |
| `d5_listening_pack.cpp` | randomized A/B pack renderer + key writer |

Corpus root is a compile define (`native-tests/CMakeLists.txt` →
`docs/corpus`); `D5_CORPUS_DIR` env var overrides it for scratch corpora /
mirrors.

## Skip discipline (HARD-2)

Absent or empty `manifest.jsonl` → every battery prints
`SKIP — corpus absent (0 clips)` and the binary exits 0 (warning, not
failure). A present-but-corrupt corpus (malformed line, SHA-256 mismatch,
non-schema WAV, >15 s, bad annotation, path escape) **fails** the binary —
real evidence cannot silently downgrade to a skip.

## Real-voice battery status — ALL SKIP (0 clips)

| battery | metric | gate | status |
|---|---|---|---|
| **R1** output-F0 | per annotated note segment, middle-60% window, ~120 ms sub-windows: \|out−annotated f0\| (applied shift), \|out−snap target\| (residual; snap target = median of `targetHz()` trace in window — honest when a sung note is off-scale), \|in−snap target\| (sung deviation), correction ratio (in−out)/in per segment with in-err ≥ 5 ¢ | report (median/p95/worst) + det% informational | **SKIP — corpus absent (0 clips)** |
| **R2** formants | F1 [500,900] / F2 [1000,1500] band centroids on sustained ≥0.3 s segments, out vs in | ±60 Hz | **SKIP — corpus absent (0 clips)** |
| **R3** pumping | ≥0.5 s `long_tones` segments, 60 ms edge trim: std of 50 ms dB envelope + p95 \|Δenv\|, input-corrected excess out−in | report; **D6 trigger printed if excess > 3 dB** | **SKIP — corpus absent (0 clips)** |
| **R4** artifacts | max \|dy\| at annotated onsets/offsets (±46 ms window) vs input — D3 rule out ≤ 1.25·in+0.01; sustained glitch-free check vs 2× input slew bound | battery-C rule | **SKIP — corpus absent (0 clips)** |
| **R5** delay | impulse-centroid delay (driven spike, spike−reference difference centroid over [t0, t0+2P]) per corpus rate | D3 P+1 budget + D4 class 2.27–4.17 ms | **SKIP — corpus absent (0 clips)** |
| listening | ≥3-person blind test on the rendered pack | human verdict rules in `docs/corpus/README.md` | **BLOCKED_HUMAN** — pack generator ready, listening is a human task, not claimed |

Grep check: no line in this section claims a real-voice PASS.

## Unit-test fixture table (synthetic — machinery validation only)

| fixture | known answer | measured | result |
|---|---|---|---|
| SHA-256 `""` / `"abc"` / 56-byte chunked | FIPS vectors | exact match | PASS |
| `fileSha256` of a written fixture | = `sha256("abc")` | exact match | PASS |
| WAV round-trip 0.5 s sine | samples within 1 LSB | exact | PASS |
| WAV rejects | stereo / 96 kHz / >15 s | each rejected | PASS |
| manifest good/bad | 2 rows parse; bad fs, non-JSON, `../` escape rejected | exact | PASS |
| annotation good/bad | 2 segs; overlap and f0≤0 rejected | exact | PASS |
| quiet sine | pump std ≈ 0 | 0.000 dB | PASS |
| step pump ±2 dB (0.5 s dwells) | std = **2.000 dB** | 2.000 dB | PASS |
| AM pump m=0.3 @2 Hz | std of 20·log10(1+m·cos), quadrature = 1.892 dB | 1.858 dB | PASS (≤10 %+0.1 dB) |
| sine 220 Hz | f0 220 | 220.000 Hz | PASS |
| formant voice (bumps 700/1200) | centroids near bumps | F1 710 / F2 1178 | PASS (±60 Hz) |
| full chain 285 → D4 | out ≈ 293.66, ratio ≥ 0.5 | 293.58, −0.5 ¢, ratio 0.99 | PASS |
| impulse probe 220/120 Hz ×2 rates | D3 P+1 budget | 180/330 samp @48k; 165/303 @44.1k | PASS |

The impulse-centroid probe uses the **spike−reference difference** (the
alternative the D3 code itself sketched): the naive 3·floorE threshold goes
blind at 44.1 kHz/120 Hz (image smears below the tone floor → −1). The
difference method reproduces D3's 180 samp @48k/220 Hz exactly and now
measures at all four (rate, f0) points.

## Mechanics exercise (NOT real-voice evidence)

To shake out dead code paths before the corpus lands, the binaries were
once pointed at a scratch corpus of five synthetic harmonic-voice WAVs in
the disposable build tree (never committed, never claimed as voice
evidence). Path exercise results: loader verified all 5 SHA-256 +
annotations; R1 residual-vs-snap-target median 0.2 ¢, correction ratio
0.99; R2 worst \|dc\| 44/52 Hz; R3 excess +0.01 dB; R5 = 3.75 ms at both
rates (D4 class). **R4 tripped** on the synthetic onsets — the engine's
UV/PSOLA onset path produced per-sample edges ~2–7× the input's 5 ms-fade
slew (the >4×-envelope transient guard only engages above the 0.05 floor /
on amplitude, not slew). That is honest machinery behavior worth knowing
when real VocalSet onsets land; it is reported here as a *path-exercise
observation*, not a real-voice defect finding.

## Blind-listening pack (`d5_listening_pack`)

- Selects ≥5 clips deterministically — first clip per distinct singer,
  then fill to 8 (max singer diversity per the ≥4-singer mandate).
- Per clip: full-chain render, output RMS-matched to input (loudness
  fairness), pair scaled ≤ 0.89 peak.
- Randomized file names `pair_NN_{A,B}.wav` into
  `docs/corpus/listening_pack/` (gitignored — generated), roles from a
  fixed-seed shuffle; committed key `docs/corpus/listening_key.csv`
  records `pair_id,slot,role,clip_id`.
- Rating sheet template: `docs/corpus/listening_sheet.md`
  (naturalness 1–5, artifacts y/n, A/B preference).
- Pack exercised end-to-end on the scratch corpus: 5 pairs + key written.
  The 3-person listening itself is **BLOCKED_HUMAN**.

## CI wiring

`native-dsp` job gains `Real-voice corpus gate (D5)`: re-runs
`ctest -R 'd5_realvoice|d5_listening_pack'` then prints an explicit
`::notice::` when `docs/corpus/manifest.jsonl` is absent (or the declared
clip/WAV counts when present). `d5_realvoice` also POST_BUILD re-runs so
its table lands in the build log like the D0–D4 reports. Gate doc:
`docs/CI_GATES.md` §7.

## D6 trigger rule (restated — printed by R3, not acted on here)

> **pump excess > 3 dB** (input-corrected std of the 50 ms dB envelope on
> real sustained `long_tones` vowels) → orchestrator dispatches the
> root-cause investigation (card D6). This task measures only; the pumping
> fix is explicitly out of scope.

## Notes / deviations

- `.gitignore` +4 lines for `docs/corpus/listening_pack/` (generated WAVs
  must never be committed; the key and sheet are). Deviation from the
  file-scope list, recorded here.
- `.devin/` toolchain-setup hook fired at session start (`.devin/config.json`
  missing) — **declined**: `.devin/**` is in this card's forbidden scope,
  so no config or dismissal file was written.
- `PitchCorrector` is untouched; the full chain is exercised through the
  public API exactly as `test_d4_e2e.cpp` does (`setParameters(0,0,1,35,0)`,
  512-frame chunks, stereo out).
- Scope check: `git diff --stat` → `.github/workflows/ci.yml`,
  `.gitignore`, `docs/CI_GATES.md`, `native-tests/CMakeLists.txt` modified;
  new files under `native-tests/` (7) and `docs/corpus/` (README,
  listening sheet, header-only key, 2×`.gitkeep`) + this report.
- **ctest: 7/7 → 10/10 PASS** (`pitch_corrector`, `d0_baseline`,
  `d1_improvement`, `d2_tracking`, `d3_shifter`, `d4_e2e`, `ping_math`,
  `d5_harness`, `d5_realvoice`, `d5_listening_pack`).

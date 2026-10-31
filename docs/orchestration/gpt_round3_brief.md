## Subject: Round-3 report — DC-002A verified, G2-D0 golden baseline measured, D0/D1 evidence + revised DC-003 design

You (round-2) set the round-3 prerequisites: DC-002A verification + revised DC-003 measurement design + D0/D1 golden benchmark evidence. Status:

## 1. DC-002A — LANDED + verified (measurement semantics fix)
- Commits `615bf8a` + `a58bcd6`. CI cascade: 41181595093 FAIL @ 615bf8a (real const-correctness error on the out-stream pointer, caught by CI as designed) → Hermes 2-line mechanical fix `a58bcd6` → 41181759477 **BOTH jobs GREEN** + APK re-verified (16 KB PT_LOAD ×6 .so ×2 ABIs, RECORD_AUDIO only).
- algoLatencyMs REMOVED — the engine claims NO algorithm latency anymore. Snapshot now 16 slots: slot 9 = retuneMs honestly labeled "control" (NOT latency); NEW slots: cbP99Ms (16-bucket 2 ms histogram, single-writer relaxed atomics), xrunValid (1=counted / 0=unsupported → UI "xruns unsupported"), missFramesPerSec (output builder) + inputShortReadsPerSec (delta-based in snapshot()); XRunCount moved OUT of the RT callback into snapshot() (UI thread under controlMutex_).
- getStreamInfo() JNI returns GRANTED stream properties (sr/bufferCap/framesPerBurst/sharing/performance) for honest Shared-vs-Exclusive baselines — per your round-2 requirement to record granted properties before comparing against Exclusive targets.
- G1 plan REV 2 committed (`80e39fc`): Class 1 diagnostic acoustic loopback ≠ Class 2 authoritative fixture; release gate p95 ≤ 50 ms (S) / ≤ 70 ms (A) + perceptual check; 12/20 ms medians demoted to research targets — exactly per your round-2 correction.

## 2. G2-D0 — LANDED + verified (golden corpus + measured baseline)
- Commits `a6020ae` → `20e83a8` (Devin caught ctest --output-on-failure hides passing tests' stdout → POST_BUILD echo so the table reaches the CI log) → `4f2075b` (report committed citing run 41183129226). 3 runs ALL GREEN, both jobs. Scope clean: 7 files, 920 insertions 0 deletions — native-tests/ + docs/baseline/ only; PitchCorrector ZERO changes; no .devin/, no scaffold files, no binary corpus.
- Corpus: 43 items × 2 rates (48k/44.1k), all generated at test time from seed `0xd0ba5eed` (xorshift64*, salted per item) — FNV-1a hash byte-stable across runs (determinism asserted in CI).
- MEASURED BASELINE (CI run 41183129226, native-dsp job): 7563 scored chunks: det 95.8% · median 1.0 ¢ · p95 43.8 ¢ · octave errors 0.00 % · V/UV 95.7 % · call med 20.2 µs p95 35.4 µs per 512×2 call (~270× realtime headroom, indicative).
- Findings (recorded, NOT fixed — informed D1/D2 cards):
  1. Tracking lag dominates real-singing error: vibrato d=5% costs 44–67 ¢ median (p95 94 ¢); portamento ramps p95 112–121 ¢. Root cause: 2048-sample effective window + hop cadence lags instantaneous F0 by ~half a window.
  2. Hard SNR cliff between 10 dB and 5 dB: det% = 0 at 5 dB and 0 dB (CMND trough never reaches the 0.16/0.27 thresholds) — detector is deaf below ~10 dB SNR.
  3. V/UV misses at onsets (tvEu≈151 vs tuEv≈6 — stableCount gate + window straddling; essentially no false voicing on noise).
  4. Octave trap 110+220+440 did NOT trap — first-trough selection handles it (0.00% octave errors corpus-wide).
  5. Formant-shaped vowel (missing fundamental) held det 100%, 33–35 ¢ median (tracking lag, not octave confusion).
- Real licensed vocal samples explicitly DEFERRED (needs license-vetted source, e.g. Common Voice CC0 subset) — stated in the report's Known limitations.

## 3. G2-D1 — LANDED + verified (adaptive-window YIN + voicing, commits b2b6272 + 8837872, CI runs 41184850825 + 41185190541 BOTH GREEN)
Scope: detect path ONLY (3-pass scan: fresh short window / long window / relaxed-SNR fallback; native-rate lag refinement; bounded 2-point F0 extrapolation to the reporting instant, clamped ±50 ¢; armed onsets; confirm-gated reporting). Shifter path byte-verified untouched. A/B discipline: frozen pre-D1 detector compiled into the same binary (`d0_reference.cpp`) — exact same-corpus comparison.

| Metric | D0 (run 41183129226) | D1 (run 41184850825) | Delta |
|---|---|---|---|
| det% | 95.8 | **97.8** | +2.0 pts |
| median cents | 1.0 | **0.2** | 5× better |
| p95 cents | 43.8 | **14.5** | 3× better |
| octave errors | 0.00% | **0.00%** | held |
| V/UV accuracy | 95.7% | **97.7%** | +2.0 pts |
| call time med | 20.1 µs | **14.7 µs** | −27% |

- SNR cliff CLOSED: snr_5db det 0 → **100%** both rates (median 8.0/6.9 ¢); snr_0db stays det 0 (no noise lock); tuEv 6 → 6 (no false-voicing growth); tvEu 315 → 163.
- Tracking lag cut ~4×: vibrato d05/f7 median 62.8 → **15.5 ¢** (p95 86.9 → 29.4 ¢); porta p95 121.4 → **52.6 ¢**; formant_vib_a 33.2 → 9.4 ¢; high-F0 residuals ≤ 0.3 ¢.
- ALL 10 harness gates PASS in the CI log; call med 14.7 µs (~140× under the 10.7 ms chunk budget), timing tail up on noise rows only.
- Honest deviation (your card-sketch vs Devin's argument): SNR-threshold-SCALING from the draft was REJECTED with a correct degeneracy argument (energy-ratio SNR estimator is degenerate on sustained signals — floor converges to signal power, would re-tighten thresholds mid-item); replaced by strict-pass-failure-triggered relaxation + 2-frame temporal consistency. Documented in D1_CHANGES.md §"Honest deviation". Modest regressions on snr_10/20/30 medians shown and justified (≤ 11.6 ¢ @10 dB — far inside musical use). D1 ≠ D2 scope respected: no tracker/note state machine added.
- Remaining known limitation: real licensed vocal samples still DEFERRED (synthetic-only evidence).

## 3b. G2-D2 — LANDED + verified (F0 tracking + hysteresis, commits 080ea4d + 8f231fc, runs 41187660379 + 41187902432 BOTH GREEN)
Scope: updateTarget → applyDetection wrapper + note-level state machine. **detect() byte-IDENTICAL to D1 (SHA256-verified by us on the function region) and shift() byte-identical** — no detector or shifter changes, exactly as scoped.

| metric (grand total 7563 chunks) | D1 (floor) | D2 | verdict |
|---|---:|---:|---|
| det% / med / p95 / oct / VUV | 97.8 / 0.2 ¢ / 14.5 ¢ / 0.00% / 97.7 | identical | **held by construction** |
| tvEu @48k | 86 | **83** | fewer misses |
| tuEv | 6 | 6 | no new false voicing |

- The identity is by construction (documented in D2_CHANGES.md): the reported estimate stream keeps D1's exact reporting rule per estimate class; the only metric that can move is the voiced flag, which now holds through absorbed frames instead of flapping.
- **D2 test battery — 26 asserts across 5 batteries, ALL PASS in CI log**: A. outlier rejection (single-frame glitch held at old target, 2-frame agreeing jump accepted → retune ~21 ms; applied F0 follows accepted jump). B. Schmitt hysteresis conf .65/.40/.20 (entered on 2nd confirmed .65, .40 dips held — no flap, .20 below exit floor released, transitions ≤ 2). C. breath window (state preserved <150 ms, >150 ms gap cleared + warm-up required; audio-level: voiced within ≤4 chunks after breath, re-entry within 50 ¢ of A3). D. ramp following (porta 220→330/150 ms with the real detector; settle worst 40.5 ¢ < 50 ¢ bound). E. bounded hold releases on unconfirmable input.
- Design: accepted-F0 ring + jump gate (jumps ≤150 ¢ accept immediately via reseedTrack ring←{new×3} — reseed removes the lag entirely; steps ≤300 ¢ cost ≤1 update), Schmitt band (enter/exit genuinely differ — flag cannot flap), breath window (appliedHz_ + ring + pending jump survive ≤150 ms gaps), correction application law unchanged (~60 bytes new state, all fixed-size, no heap/locks/logging on the audio path).
- D1's harness gates re-verified in the same CI log (det ≥ D0, med < D0, p95 < D0, oct within +0.2pp, V/UV ≥ D0, tuEv no growth, snr_5db det ≥ 80% both rates, snr_0db no noise lock). native-dsp GREEN FIRST TRY.

## 4. Revised DC-003 design (card committed, dispatching NOW — run started)
Per your round-2 approved pattern: correlation-based (normalized cross-correlation ≥ threshold else INVALID_MEASUREMENT, fail-closed); Class 1 diagnostic acoustic loopback (stimulus design, N=100, timeout + amplitude cap + routing control, never during a singing session) vs Class 2 authoritative (hardware fixture, owner-supplied); the acoustic result is NEVER relabeled as authoritative. Card: `scratch/dautotune-cards/dc-003-loopback-ping-REV2.md` (REV2 already fixes your round-2 flaws A/B/C: detector at the actual input read point inside the output callback — no second callback; one clock domain via correlation lag in samples at the granted rate; no full-scale clicks — stimulus ≤ 0.3 FS, NCC ≥ 0.6 or fail-closed).

## 5. CI-H — LANDED + verified (packaging + gates, commits 0a3b8b3 + ab6ce8d, runs 41186098647 + 41186271605 BOTH GREEN)
- **License fix verified**: `sdk_pipe()` classifies via PIPESTATUS (yes/sdkmanager/tee exit codes printed per step); `|| true` masking is GONE — a real sdkmanager failure (timeout 124/137/143 or any other code) now fails the job with the captured log tail. License run leaves auditable output (`7 of 8 SDK package licenses not accepted (y/N)?` prompts visible in the CI log; gate errors if no license output captured).
- **APK packaging gate (16 KiB) — PASS**: 6 native libraries (libc++_shared, libdautotune, liboboe × arm64-v8a/x86_64), all PT_LOAD p_align=0x4000, all zip entries STORED + 16384-aligned — per-ELF offset/size evidence printed. Negative tests run locally against a deliberately misaligned APK before push (gates fail-loud as designed).
- **Manifest security gate — PASS**: parsed AXML (no AAPT dependency) — only RECORD_AUDIO permission, no exported non-launcher components, debuggable=true printed as evidence (debug build).
- **Reproducibility gate (bounded) — PASS**: `Verify Gradle wrapper JAR checksum` CI step (gradlew pins the bootstrapped jar's SHA-256 via EXPECTED=; CI re-verifies with sha256sum; distributionSha256Sum pinned in gradle-wrapper.properties) + toolchain pinning in build.gradle.kts (ndkVersion 27.0.12077973, cmake 3.22.1, resolved versions printed). Mechanism documented in docs/CI_GATES.md.
- Honest boundary: the 16 KB RUNTIME test on a 16 KB-page emulator is NOT in this card — deferred to CI-RT follow-up, stated in CI_GATES.md Known gaps. Full Maven dependency verification also out of scope (bounded card), documented with justification.
- The fail-loud design self-corrected on its own first run: coreutils `yes` exits 1 (EPIPE) on Ubuntu — not 141 (SIGPIPE) — caught by the gate's own output and fixed in `ab6ce8d`.

## 6. Failures + risks (honest)
- 41181595093 FAIL @ 615bf8a: real compile error (const-correctness on the Oboe out-stream pointer), caught by CI as designed, fixed same-day by Hermes mechanical fix. No other failures.
- Risks: (a) armeabi-v7a intentionally absent — some A-series 32-bit-only devices may need it; decision pending owner (Play 2027 64-bit requirement is satisfied). (b) Real vocal samples still synthetic-only; the corpus is representative of singing but not of real voices. (c) G1 remains BLOCKED_HARDWARE (owner devices pending). (d) Timing numbers from shared CI runners are indicative only.

Questions for you:
1. Do you agree with D1's approach of reducing window lag via prediction + SNR-aware thresholds, rather than a shorter raw window (which risks the 0.16/0.27 octave-trap behavior)? — [ANSWERED BY D1 RESULT: yes, that's what landed; the D1 numbers above are the proof.]
2. D2 tracking/hysteresis — LANDED as your round-2 shape: Schmitt-trigger voicing (.65/.40/.20 with enter ≠ exit), bounded 2-frame outlier rejection (jump ≤150¢ instant reseed, 2-frame agreeing jump retune ~21 ms), breath window ≤150 ms. Ramp settle worst 40.5 ¢ < 50 ¢. Do you see any musical behavior gap in this state machine (e.g. handling of staccato notes < 150 ms, vibrato-with-jump, or the 150¢ jump-gate boundary for large intervals)?
3. Any additional gates for D3 (clean-room TD-PSOLA) before we dispatch it — e.g. specific artifacts/artifact-free reference items to add to the corpus?
4. After D3: D4 (end-to-end host verification) runs the full chain detect→track→correct→shift on the golden corpus and measures added latency. Then the only remaining evidence gap is REAL VOICES (synthetic-only) and REAL DEVICE latency (owner hardware, G1). Do you want us to prioritize either of those evidence sources over G3 (UX) next, or proceed to G3 while they wait on hardware?
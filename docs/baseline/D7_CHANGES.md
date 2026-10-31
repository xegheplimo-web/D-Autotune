# D7 — R1 residual diagnosis + detector-coverage classification + R4 1.10× warning (card G2-D7)

Base `73f515b` (D6 landed `bf6c3e9`). Per GPT round-5 verdict
(`docs/orchestration/gpt_round5_reply.md`), first item: diagnose the
remaining R1 residual against the **instantaneous** internal target and
the annotation — not dismiss it as reference disagreement — classify the
real-voice detector coverage, and add a nonblocking R4 >1.10× warning.

**Diagnosis-only card. `app/src/main/cpp/**` is untouched** — no
reproducible engine defect surfaced (case `e` count = 0), so no D8 fix
was triggered. All new code lives in `native-tests/` (test-side
instrumentation; the `DAUTOTUNE_DETECT_DEBUG` probe pattern from D6 was
reused in `.local-test-build/probe/` for the old-vs-new engine trace).

## Task 1 — R1 residual-window diagnosis

### Method

For every scored R1 window (120 ms, 60 ms hop, middle-60% of annotated
segments) the harness now records, per window: delay-corrected output F0
(R5 impulse-centroid delay, 165 samples @44.1 kHz), input F0 at the same
time, output-window normalized-correlation score `nc`, output RMS, the
**instantaneous** `targetHz()` at the aligned input time, the
segment-median target (the reference the R1 metric scores against), the
annotation F0, the count of target steps inside the window
(`segSteps`), one primary cause, and a confidence.

Worst-window set = **all windows above the per-singer p75** of
`|out − tgtMed|` (361 windows of 1445). One mutually-exclusive primary
cause per window:

| cause | meaning |
|---|---|
| `a` | vibrato/reference disagreement — output tracks the instantaneous target (or passes input through in a det-gap); the *segment-median* reference is smeared by target oscillation or det gaps |
| `b` | retune smoothing lag — output genuinely mid-flight between pre/post-change targets |
| `c` | target change inside the scored window — median reference unrepresentative; output tracks the instantaneous target |
| `d` | measurement-limited window (`measureF0` unreliable on the frame) |
| `e` | genuine engine defect — output disagrees with BOTH instantaneous target and annotation reproducibly |

### Cause counts (windows > per-singer p75)

| singer | p75 (¢) | n | a | b | c | d | e |
|--------|---------|---|---|---|---|---|---|
| f1 | 12.2 | 81 | 32 | 35 | 9 | 5 | **0** |
| f3 | 7.6 | 84 | 40 | 23 | 21 | 0 | **0** |
| m1 | 30.6 | 77 | 25 | 52 | 0 | 0 | **0** |
| m3 | 24.0 | 119 | 7 | 109 | 1 | 2 | **0** |
| **all** | — | **361** | **104** | **219** | **31** | **7** | **0** |

R1 residual (unchanged gate): median 5.8 ¢, p95 57.8 ¢, worst 103.4 ¢;
octave tail 0/1445 windows ≥600 ¢. Per-singer p95: f1 39.7 / f3 28.3 /
m1 67.1 / m3 62.1 ¢.

### Representative worst windows (time-aligned values)

Columns: output F0 / input F0 (delay-corrected), instantaneous target,
segment-median target, annotation F0, residual vs each (¢), `segSteps`
= target steps in window, class, confidence.

| singer | clip | t (s) | out | in | tgt_inst | tgt_med | annot | e_inst | e_med | e_ann | steps | cls |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| m3 | scales_o | 6.49 | 207.2 | 217.5 | 220.0 | 220.0 | 213.7 | 103.4 | 103.4 | 53.4 | 2 | b |
| m3 | scales_o | 6.43 | 208.6 | 208.0 | 196.0 | 220.0 | 213.7 | 107.9 | 92.1 | 42.1 | 2 | b |
| m3 | scales_a | 6.65 | 235.2 | 234.6 | 220.0 | 246.9 | 235.8 | 115.4 | 84.6 | **4.6** | 6 | b |
| m3 | long_a | 3.02 | 130.4 | 127.8 | 130.8 | 123.5 | 127.1 | 5.1 | 94.9 | 44.9 | 22 | a |
| m1 | long_a | 8.83 | 347.8 | 342.0 | 349.2 | 329.6 | 339.3 | 6.9 | 93.1 | 43.1 | 25 | a |
| m1 | long_a | 4.86 | 258.8 | 260.9 | 261.6 | 246.9 | 254.2 | 19.0 | 81.0 | 31.0 | 19 | a |
| m1 | scales_a | 3.09 | 157.5 | 157.4 | 0.0 | 164.8 | 158.3 | −1.0 | 79.1 | **9.1** | 0 | a |
| f3 | scales_o | 4.65 | 550.2 | 550.7 | 587.3 | 523.3 | 548.0 | 113.0 | 87.0 | **7.0** | 2 | b |
| f3 | scales_i | 2.03 | 344.8 | 340.7 | 349.2 | 329.6 | 339.3 | 21.8 | 78.2 | 28.1 | 1 | c |
| f1 | long_e | 5.54 | 504.7 | 513.6 | 493.9 | 523.3 | 523.2 | 37.6 | 62.4 | 62.4 | 18 | b |
| f1 | long_a | 2.16 | 267.5 | 268.2 | 261.6 | 261.6 | 261.6 | 38.2 | 38.2 | 38.1 | 8 | d |

(Full per-window dump: `D5_R1_DIAG=1 ./d5_realvoice`.)

### Findings

- **`e` = 0 — no reproducible engine defect.** Every window's output is
  either tracking the instantaneous target (`a`, `c`), slewing between
  two consecutive snapped targets (`b`), or a measurement-limited frame
  (`d`, all ≤38 ¢ and isolated).
- The dominant cause is `b` (219/361, 61%) — retune smoothing lag,
  concentrated on m3 (109) and m1 (52) where target steps are dense.
  Typical signature: `tgt_inst`/`tgt_med` differ (median caught the
  pre-change side) while `out` sits between the two targets —
  e.g. m3 `scales_o` 6.43 s: out 208.6 Hz between 196.0/220.0 targets.
  Several `b` rows are within 10 ¢ **of the annotation**
  (m3 `scales_a` 6.65 s: e_ann 4.6 ¢; f3 `scales_o` 4.65 s: e_ann 7.0 ¢)
  — the *output pitch* is faithful to the sung note while the engine's
  snapped target steps through the change.
- `a` (104/361, 29%): two sub-patterns. (i) **vibrato-snap smear** —
  segments where the target oscillates ±1 semitone (segSteps 18–25)
  around the median; output tracks instantaneous target within ~25 ¢
  (e.g. m1 `long_a` 8.83 s: e_inst 6.9 ¢ vs e_med 93.1 ¢). (ii)
  **det-gap passthrough** — windows spanning an unvoiced stretch where
  output passes input through (out≈in≈annot to ~10 ¢) while the median
  target from neighboring detected chunks misrepresents the window.
  Both are reference artifacts: the output itself is correct.
- `c` (31): real mid-window target changes; output tracks instantaneous
  target (e_inst ≤ ~24 ¢ on high-confidence rows).
- `d` (7): isolated single-window `measureF0` deviations, all ≤38 ¢ —
  measurement noise floor, not engine behavior.
- **Conclusion**: the remaining R1 residual is expected behavior — the
  57.8 ¢ p95 is dominated by reference-vs-instantaneous-target spread
  (`a`) and real retune slew (`b`) inside 120 ms windows that straddle
  note changes. The earlier "corpus vibrato vs reference-F0"
  attribution was imprecise: the disagreement is primarily between the
  **segment-median** reference and the instantaneous target/output, not
  between output and annotation.

## Task 2 — detector coverage classification (86.0% real-voice)

2,294 non-detected voiced chunks (of 16,421; det = 86.03%):

| singer | context | n | S | T | B | M |
|--------|---------|---|---|---|---|---|
| f1 | long_tones | 98 | 63 | 0 | 29 | 6 |
| f1 | scales | 171 | 41 | 33 | 62 | 35 |
| f3 | long_tones | 62 | 50 | 0 | 12 | 0 |
| f3 | scales | 42 | 23 | 10 | 0 | 9 |
| m1 | long_tones | 626 | 34 | 22 | 520 | 50 |
| m1 | scales | 1,033 | 48 | 158 | 799 | 28 |
| m3 | long_tones | 102 | 57 | 1 | 43 | 1 |
| m3 | scales | 160 | 26 | 23 | 3 | 108 |
| **total** | | **2,294** | **342** | **247** | **1,468** | **237** |

S = silence-adjacent (onset/offset edge); T = transition (annotated
pitch step / glissando); B = breath/low-energy; M = flagged
"genuinely-missed" (interior, periodic nc≥0.9, rms≥0.01).

- **B split**: 1,467 low-energy interior chunks (rms < 0.008, at/below
  the detector's own 4e-5 energy floor → rms≈0.0063) — periodic but too
  quiet for the detector's energy gate; 1 aperiodic/frication chunk.
  The m1 rows dominate (1,319) — breathy male voice, consistent with
  the 86% figure being mostly an energy-floor story, not tracking loss.
- **M = 237 flagged chunks — NOT a reproduced defect.** 233/237 have
  the engine `voiced` flag **still held** (`estVoiced=1`): they are
  1–2-chunk tentative-hold stretches where the provisional estimate is
  unconfirmed, not dropped notes. The 4 hard drops (`estVoiced=0`): two
  quiet/marginal chunks (near the energy floor), and two single-hop
  loud interior drops on m3 — `scales_a` t=8.71 was previously an
  **old +1,166 ¢ octave-up phantom** (its removal is correct), and
  `scales_i` t=4.42 is a genuine 1-hop miss inside a tentative stretch.
  No reproducible pattern → report-only per the card.

### The 87.3% → 86.0% drop (required honesty row)

An old-vs-new trace probe (`.local-test-build/probe/trace_detect.cpp`,
pre-D6 engine vs current, same chunks) reproduces the figure exactly:
**old 14,338/16,421 = 87.32%; new 14,127/16,421 = 86.03%; net −211**.

Per-chunk flips: 267 lost (1→0), 56 gained (0→1). Attribution of the
267 lost:

| group | chunks | explanation |
|---|---|---|
| old +~1,200 ¢ phantom detections | 33 | D6 octave-veto correctly removed the phantom octave-UP reports that *inflated* the old det% |
| tentative-hold bookkeeping | 232 | `estVoiced=1`, `estHz=0`: D6's changed candidate values (subharmonic flip + vetoed jumps) break the 2-agree confirmation chain → 1–2-hop unreported stretches while voiced holds |
| hard drops | 2 | single-hop; see M above |

Run lengths of lost chunks: 113 isolated, 59×2, 4×3, 3×4, 2×6 —
dominated by 1–2-hop confirmation events, not dropouts.

**Side observation for a future card** (report-only): new detector
telemetry emits 161 sub-octave-ish candidates (<0.62× annotation) vs
old 11, while super-octave (>1.6×) fell 114 → 32. Example:
m3 `scales_i` ~7.4–7.6 s — the new engine emits ~119 Hz candidates vs
annotated 236 Hz on rough H2-dominant stretches, but the tracker veto
holds `tgt=247` throughout (the old tracker dipped to the 123 snap).
Output is unaffected — R1 octave-tail is 0/1445 and the veto prevents
reseeds — but `detectedHz()` telemetry now leans sub-octave where the
old telemetry leaned super-octave. Candidate for a D8+ detector-polish
card; **not a tracking defect**.

## Task 3 — R4 >1.10× nonblocking warning

- Hard gate unchanged: `out > 1.25 × in + 0.01` → fail (0/432 amplified;
  worst 1.17×).
- New diagnostic: `out > 1.10 × in + 0.01` → `WARN` line with clip,
  timestamp, in/out step amplitudes, and ratio; never fails.
- Current corpus: **0 boundaries inside the warn band** — the worst
  1.17× boundary is a soft-onset step absorbed by the +0.01 absolute
  tolerance. The warning exists and is exercised by the band counter
  print; documented in `docs/CI_GATES.md` §7.

## Files touched

- `native-tests/d5_measure.h` — shared helpers: `rmsOf`, `NcScan`/
  `ncScan`, `peakNc`; `measureF0` refactored onto the shared scan (no
  behavior change — same outputs, R1 numbers identical).
- `native-tests/test_d5_realvoice.cpp` — render cache; R1 per-window
  records + p75 worst-window diagnosis + cause classifier + tallies +
  `D5_R1_DIAG` dump; coverage classifier + `D5_COV_DIAG` table + missed
  flags; R4 warn band + counter.
- `docs/CI_GATES.md` — §7: one sentence for the R4 >1.10× nonblocking
  warning.
- `docs/baseline/D7_CHANGES.md` — this file.

**Engine files touched: none.** `PitchCorrector.{h,cpp}` SHA-256
unchanged from D6 (`0d11c14…`, `c1afc18…`); corpus loader / sha256 /
d2_reference / corpus data untouched.

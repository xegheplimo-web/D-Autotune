# D6 — Real-voice defect root-cause + fix (card G2-D6, D-016)

Base `b7bd4a1`. CI run **41199096037** (main @ e70719c) was RED — the D5
gate working as designed, not a flake: native-dsp FAIL ("D5 REAL-VOICE:
3 FAILURES, 0 warnings"), android PASS. Local repro produced identical
numbers (EXIT=1; 20/20 clips usable, 0 rejected; 16421 voiced chunks;
1445 scored F0 windows; 217 sustained windows; 432 boundaries). This
card restores green via root-cause fixes — no gate was weakened.

`PitchCorrector.{h,cpp}` SHA-256 (byte-freeze lifted by this card):

| file | before (HEAD @ b7bd4a1) | after |
|---|---|---|
| `PitchCorrector.cpp` | `ba78ed0bde310a781845bdefaec2840ebf0b055a5eb67bcae815ea80c136f83c` | `0d11c146fad3214c0ca9e2c32b43690416da240137112598eca9816dd62118b0` |
| `PitchCorrector.h`   | `f0beab77c0ee1c3fe53353a82295c99c0656a08da43490f19d7f1d33f75929be` | `c1afc18c0da8ea7240aec0a48a1dfe27a2b855c48d832cece9fe07e3b4f72ede` |

(The card's stated h-hash `f0a992b0…29be` did not match HEAD — the
re-verified value above is the true before-hash.)

## Diagnosis

Per-window instrumentation (scratch probes in `.local-test-build/probe/`,
compiling the engine TU with `-DDAUTOTUNE_DETECT_DEBUG` — the debug
fields exist only under that macro; production has no surface/cost)
split every ≥600 ¢ residual window into measurement vs engine causes.

### R1 octave tail — TWO different causes, only one in the engine

| class | evidence | verdict |
|---|---|---|
| female F5 windows (f1 ~707 Hz, f3 ~700 Hz) | `measureF0` returned ~350 Hz on BOTH input and output; output spectrum has energy at ~700 Hz, ~none at ~350 | **measurement artifact** — `measureF0(hiHz=600)` capped minLag at 73; the true ~62-sample period is out of range → autocorrelation returns 2P. Engine was NOT producing octave-low audio. |
| `m3_straight_scales_o` 9.27–9.51 s | input ~285 Hz; detector emits sustained ~547–580 Hz winners; tracker reseeds; output acorr confirms real ~2× audio | **engine defect — octave-UP lock** (opposite of the suspected sub-octave direction): the first-trough YIN scan takes the half-period dip on H2-dominant male voice; ~2 s runs of agreeing ~2× candidates reseed the track. |
| `m3_straight_scales_i` 7.36 s | input ~236 Hz; confident ~118.5 Hz winners reseed the track | **engine defect — octave-DOWN phantom**: a direct 2·P winner, same reseed path. |
| residual windows | after both fixes: 24 → 6 → 0 | — |

Chunk-level: 131 m3 + 16 f1 + 7 m1 tracker-excursion chunks were real
engine events; the ~107 female windows were the measurement cap.

### R2 formants — measurement-limited, not engine damage

Of the failing windows: F1 884 Hz = the empty-band case (f0≈455 Hz → no
harmonic inside [500,900] → grid centroid returns 0, compared against a
real output centroid); F2 452–482 Hz = band-edge membership flips
(6·f0 straddling 1500 Hz for a few-Hz f0 delta between input/output).
Only the genuinely octave-locked `scales_o` segment showed real
envelope damage — resolved by the engine fix itself. The remaining
residual is the estimator's artifact → fix the measurement, per the
card's rule.

### R4 boundary clicks — passthrough-lag teleport, not input slew

All 5 amplified boundaries were silence→tone onsets where the input's
own step was soft (0.0026–0.0410). Internal trace: the max output step
lands on the detection-update sample — `voiced` flips 0→1,
`lastPeriod_` jumps to the new note's period, and the unvoiced-path
`delayed(0.5·lastPeriod_)` read point teleports 10–40 samples in a
single instant. Output step 0.021–0.062 while `uvGain_` is still 0 →
pure lag discontinuity, amplified up to 11.53× vs the input's soft
onset. The >4×-envelope/0.05 transient guard correctly does not fire on
soft onsets — the artifact is ours, not an onset the guard missed.

## Fixes

### Engine — `PitchCorrector.cpp/.h`

1. **Detector subharmonic check** (`detect()`): after the winning pass,
   scan the 1.70–2.35×-lag band for a deeper trough. Flip to the longer
   lag only when ALL hold: (a) winner marginal (CMND ≥ 0.06 — confident
   winners never second-guessed); (b) 2× trough ≥2× deeper AND ≤0.10
   absolute (equal-depth marginals stay); (c) real spectral energy at
   f0/2 ≥10% of the winner-frequency energy (H2-dominant voice: ~20–40%;
   clean phantom position: ~2–3%). Requires `yinTrough` to fill the
   curve past the early-exit trough — `found` records the first trough
   with identical return semantics; the curve is now always complete.
   Bounded extra cost: the full-CMND scan was already the worst case.
2. **Tracker octave-phantom veto** (`applyDetection()`): detector now
   emits `DetOut.subE` (Goertzel energy at hz/2) and `DetOut.octHi`
   (min CMND in the 0.43–0.58·lag band). On the pending-jump second
   agree, only when the candidate is >±1050 ¢ off the median:
   - UP (candidate ≈ 2×·med): veto iff EITHER pair member has
     `subE ≥ 0.005` — a real note persisting an octave below means the
     candidate is its harmonic. Pair-level evidence closes the
     single-sub-threshold escape (phantom streams read ~0.006–0.056
     with rare dips; real jumps read 0.000–0.003 on both members).
   - DOWN (candidate ≈ med/2): veto iff EITHER member has
     `0 ≤ octHi ≤ 0.16` — the tracked octave-above period still
     correlates → phantom fundamental. (Absolute candidate-side energy
     is unusable down-ward: rumble under the note reads nonzero at the
     phantom frequency — measured 0.0061–0.0075.)
   - Seam detections carry -1 → never veto (D2 battery H keeps its
     ±1200 ¢ reseed-on-second-update semantics). Non-octave jumps are
     never gated — 300–1200 ¢ intervals keep the 2-update cadence.
3. **R4 passthrough lag slew** (`psolaShift()`): `passLag_` slews toward
   `0.5·lastPeriod_` at the V/UV crossfade rate instead of re-anchoring
   — the alignment converges inside the crossfade, no single-sample
   teleport, no added latency. Real-time safe: no allocation, no locks.

### Measurement — `d5_measure.h` / `test_d5_realvoice.cpp`

4. **`measureF0` octave-safe pick**: `hiHz` default 600→1000 Hz (the
   corpus sings to ~719 Hz — the 600 cap silently halved every F5
   window); then a multi-candidate rule: local peaks ≥0.5·best are
   demoted if they sit on a harmonic position (nc at a ±3% band around
   k·lag, k=2..4, ≥0.6·best AND Goertzel energy at that frequency) or
   lack own-frequency energy (<0.004 floor); the survivor with max
   periodicity wins; first-strong-peak fallback retained for weird
   waveforms (period-doubled output reads honestly).
5. **R2 → LPC envelope centroid**: `lpcCoeffs` (autocorr +
   Levinson-Durbin, order 24) + `lpcBandCentroid` (|1/A(e^jw)|² on a
   5 Hz grid) on a centered 60 ms `lpcFrame`. The envelope has no
   harmonic membership → F0-independent, cannot edge-flip.
6. **R1 octave-tail gate added**: 0 scored windows with |out-vs-snap
   residual| ≥ 600 ¢ (the octave signature; 6× margin over the max
   legitimate ~100 ¢ snap shift). `CI_GATES.md` §7 text updated to
   match.

## Results — real corpus (20 clips, 16421 voiced chunks)

| battery | before (CI 41199096037) | after | gate |
|---|---|---|---|
| R1 out-vs-snap residual | female p95 **~1205 ¢** tail (per-singer below) | med 5.8, p95 57.8, **worst 103.4 ¢** | **0 windows ≥600 ¢: PASS** |
| R1 per-singer p95 (¢) | f1 **1204.9**, f3 **1201.2**, m1 71.5, m3 68.5 | f1 39.7, f3 28.3, m1 67.1, m3 62.1 | — |
| R1 detector det% (info) | 87.3% of 16421 chunks | 86.0% | informational |
| R2 F1/F2 worst \|Δ\| | **884 / 482 Hz** | **41 / 31 Hz** | ±60 Hz: PASS |
| R3 pump excess | +0.03 dB std, +0.17 dB p95 | +0.03 dB std, +0.17 dB p95 | no trigger, unchanged |
| R4 amplified boundaries | **5/432, worst 11.53×** | **0/432, worst 1.17×** | PASS |
| R5 delay class | 165 samp = 3.75 ms | 165 samp = 3.75 ms | D4 class, unchanged |
| summary | **3 FAILURES** | **COMPLETE (0 warnings)** | EXIT=0 |

Synthetic regression — all 10 ctest targets green: pitch_corrector,
d0_baseline, d1_improvement, d2_tracking (incl. battery H octave-reseed
semantics), d3_shifter, d4_e2e, ping_math, d5_harness, d5_realvoice,
d5_listening_pack. `powershell scripts/test-dsp-windows.ps1` full pass.
`d2_reference.{h,cpp}` untouched.

## Listening pack

`d5_listening_pack` regenerated post-fix (same seed → same pair
assignment): `docs/corpus/listening_key.csv` **unchanged** vs `b7bd4a1`
— not re-committed. Pack WAVs stay gitignored. Blind listening remains
BLOCKED_HUMAN (3 real listeners; owner orchestrates).

## Deviations

- `.orchestrator/*`, `docs/orchestration/gpt_round5_brief.md`:
  control-plane tracking for the D6 dispatch (D-016/Gate-18 records),
  pre-modified by the orchestrator — committed with the task.
- `.gitignore`: added `g2-d6-run1.log` under the existing run-log block
  (convention).
- `CI_GATES.md` §7: gate text updated for the new R1 octave-tail gate +
  R2 LPC estimator (justification: this document).
- Probe sources live in `.local-test-build/probe/` (gitignored scratch);
  the debug fields they read (`dbgWinnerHz_`…`dbgOctHi_`) are compiled
  only under `-DDAUTOTUNE_DETECT_DEBUG` — zero shipped surface.
- `yinTrough` no longer early-exits: worst-case cost was already the
  full scan; the curve fill is required by the 2×-band check. No heap,
  no locks anywhere in the audio path.

Run log: `g2-d6-run1.log` (gitignored), ends `EXIT=0`.

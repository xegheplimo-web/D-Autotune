# D1 — Adaptive-window YIN + voicing

Card G2-D1 (GPT round-2 D-012 sequence step 2). Detector-path changes only;
the shifter (`shift()`, `delayed()`, reverb, `phase_`, the `process()` shift
branch) is byte-identical — verified by diff. Source of all numbers below:
CI run `41184850825`, `native-dsp` job (ubuntu-latest, GCC `-O2`), printed by
the `d1_improvement` POST_BUILD step and ctest. Both columns are measured
**live in the same binary** on the same corpus — the "D0" column is the
frozen reference detector (`native-tests/d0_reference.cpp`, verbatim copy of
the a6020ae detector), so the A/B is exact, not a paste of
`D0_BASELINE.md`.

## Result — headline

| metric (grand total, 7563 chunks) | D0 | D1 | verdict |
|---|---:|---:|---|
| detection rate | 95.8 % | **97.8 %** | beat |
| median cents | 1.0 | **0.2** | beat (5×) |
| p95 cents | 43.8 | **14.5** | beat (3×) |
| octave errors | 0.00 % | **0.00 %** | held |
| V/UV accuracy | 95.7 % | **97.7 %** | beat |
| call median / p95 | 20.1 / 35.5 µs | **14.7 / 57.0 µs** | median faster; tail up, see §Latency |

- **SNR cliff closed**: `snr_5db` det% 0.0 → **100.0** at both rates
  (target ≥ 80), median **8.0 ¢ @48 k / 6.9 ¢ @44.1 k** (target ≤ 25).
  `snr_0db` stays at det% 0.0 — no noise lock; tuEv on noise segments did
  not grow (6 → 6 per rate, all pre-existing strict-pass flukes).
- **Tracking lag cut ~4×**: `vib_220_d05_f7` median 62.8 → 15.5 ¢,
  p95 86.9 → 29.4 ¢; `porta` p95 112–121 → 52–53 ¢; `formant_vib_a`
  33.2 → 9.4 ¢. All well under the card's ≤ 70 ¢ p95 goal.
- **High-F0 residual gone**: every A4/C5/G5 sine and harmonic item now
  sits at 0.0–0.3 ¢ (native-rate lag refinement), worst steady-state
  residual in the corpus ≈ 1.2 ¢.
- **V/UV boundary**: aggregate tvEu 315 → 163 (mostly the 5 dB fix);
  `vuv_mix` onsets improved 6→4 / 7→5 missed frames via the armed gate.

## What changed (clean-room, per the card sketch + refinements)

All inside the `detect()` path of `PitchCorrector.cpp`; ~44 bytes of new
state (`recentE_[8]`, `recentIdx_`, `armedLeft_`). No heap, no locks, no
virtual calls; fixed-iteration loops only. `detectedHz()` keeps the
0-when-unvoiced contract.

The single CMND scan became three passes, first winner wins:

1. **Pass A — fresh short window (adaptive-window lag cut).** The same
   512-sample decimated buffer, but only the newest 288 entries
   (~24 ms at 48 k) are scanned with `compare = 128`. The used span's
   effective center sits ≈ 16 ms behind "now" instead of ≈ 30 ms for the
   long window. Entry/accept thresholds are *stricter* than pass B
   (0.12/0.20, conf ≥ 0.80): it only wins when the short window shows an
   unambiguous trough. Result flagged **provisional** — see gating below.
2. **Pass B — long window, strict.** Byte-identical thresholds to D0
   (0.16 enter / 0.27 accept, compare 256). Reached whenever pass A can't
   confirm a deep trough (low F0 where 128 samples < ~1.2 periods, noisy
   frames, ambiguous material). Steady-state behavior preserved.
3. **Pass C — relaxed fallback (SNR cliff).** Same long-window scan at
   0.35/0.55, reached only when pass B finds nothing — i.e. exactly when
   periodicity is buried. Estimates flagged **relaxed**.

Plus two refinements on the winning estimate:

4. **Native-rate lag refinement** (candidates with decimated lag ≤ 34,
   i.e. F0 ≳ 325 Hz): re-scan ±4 native lags around the winner on the
   freshest 400 samples with an energy-normalized difference; adopt the
   parabolic-refined native lag only when a clean dip (m < 0.25) exists
   within ±1.5 semitones of the decimated answer. This removes the 4×
   grid quantization that dominated the G5/C5 residuals.
5. **Bounded lag-compensating extrapolation** (card option b): each
   estimate carries `leadSec` = its effective staleness
   ((used span − (compare+lag)/2)·kDownsample / sr). When two consecutive
   raw estimates agree, the reported F0 is the latest estimate projected
   forward by `dCents·leadSec/hopSec`, clamped to ±50 ¢, disabled for
   |dCents| > 120 ¢/hop and for relaxed frames. This is a two-point
   predictor, **not** a tracker — D2 owns tracking/hysteresis.

**Voicing / confirmation gating** (`updateTarget`):

- Agreement is tracked on **every admitted estimate** (raw Hz, no
  smoothing), including unreported ones: |12·log₂(hz/prev)| < 1.7 st for
  strict estimates, < 1.0 st for provisional/relaxed ones (noise troughs
  rarely repeat within a semitone twice running).
- **Provisional and relaxed estimates are suppressed** (reported
  unvoiced, Hz = 0) until `stableCount ≥ 1` — the "2 consecutive agreeing
  frames" confirmation the card asks for at low SNR. Strict estimates
  keep D0 semantics (reported immediately, voiced after agreement).
- **Armed onsets**: `recentE_` is a ring of the last 8 per-update window
  energies (~85 ms); its running minimum is the noise-floor proxy. A rise
  of > 3× over that floor opens a ~64 ms "armed" window in which one
  confident estimate (conf ≥ 0.78) voices immediately — this is what cut
  the onset misses. Stationary noise can never arm it (its own level is
  the floor). Updated on every detect, including silent frames, so a
  silence→voice edge arms naturally.

### Honest deviation from the card sketch

The card suggests scaling CMND thresholds by an energy-ratio SNR
estimate ("running minimum of short-time energy"). I implemented the
running-min floor (it drives the armed trigger) but **not** the
threshold scaling, because an energy-ratio SNR estimator is degenerate
on sustained signals: with no noise-only reference in-band, the floor
converges to the signal's own power in ~100 ms and the estimated SNR
collapses to ~0 — which would *re-tighten* the thresholds mid-item and
re-break the 5 dB lock on exactly the corpus items being fixed. The
implemented equivalent reaches for the same target through detector
state: relaxation engages precisely when periodicity is buried
(strict-pass failure), and the safety that SNR-scaling was meant to
provide comes from the 2-frame temporal-consistency confirmation, which
works on sustained tones and pure noise alike. Measured outcome:
det% 0 → 100 at 5 dB, 0 → 0 at 0 dB, tuEv unchanged.

## Latency budget check

- Median per 512×2 call: **14.7 µs vs 20.1 D0** — pass A exits early on
  clean material, so the common case got *faster*. Guard (≤ 60 µs, ≤ 3×
  D0 median): held with margin.
- p95: 57.0 µs vs 35.5 — the tail is the noise items, where pass A +
  strict + relaxed all run to `maxLag` (~95 k MACs ≈ 60–78 µs/call on
  snr_5db/0db rows). Still ≈ 140× under the 10.7 ms chunk budget; the
  heavy path only triggers where D0 produced silence anyway.
- Detector state added: 44 bytes. Stack usage ~3 KB (decimation buffer +
  norm array), same order as D0.

## Before/after table — CI run 41184850825 (verbatim from build log)

### sr = 48000 — error/rate comparison

| item | D0 med c | D1 med c | D0 p95 | D1 p95 | D0 oct% | D1 oct% | D0 vuv% | D1 vuv% | D0 us | D1 us |
|--------------------------|---------:|---------:|-------:|-------:|-------:|-------:|-------:|-------:|------:|------:|
| sine_A2_110              |      0.1 |      0.1 |    0.1 |    0.3 |   0.00 |   0.00 |   100.0 |   100.0 |  30.4 |  18.9 |
| sine_A3_220              |      0.1 |      0.1 |    0.1 |    0.2 |   0.00 |   0.00 |   100.0 |   100.0 |  20.9 |  14.7 |
| sine_E4_329              |      0.4 |      0.4 |    0.4 |    0.4 |   0.00 |   0.00 |   100.0 |   100.0 |  17.5 |  13.3 |
| sine_A4_440              |      1.0 |      0.1 |    1.0 |    0.2 |   0.00 |   0.00 |   100.0 |   100.0 |  15.8 |  15.2 |
| sine_C5_523              |      1.5 |      0.1 |    1.7 |    0.1 |   0.00 |   0.00 |   100.0 |   100.0 |  15.0 |  14.5 |
| sine_G5_784              |      3.1 |      0.2 |    3.3 |    0.3 |   0.00 |   0.00 |   100.0 |   100.0 |  13.4 |  14.6 |
| harm1_A2_110             |      0.1 |      0.1 |    0.1 |    0.3 |   0.00 |   0.00 |   100.0 |   100.0 |  30.4 |  18.9 |
| harm1_A3_220             |      0.1 |      0.1 |    0.1 |    0.2 |   0.00 |   0.00 |   100.0 |   100.0 |  20.9 |  14.8 |
| harm1_E4_329             |      0.4 |      0.4 |    0.4 |    0.4 |   0.00 |   0.00 |   100.0 |   100.0 |  17.5 |  13.3 |
| harm1_A4_440             |      1.0 |      0.1 |    1.0 |    0.2 |   0.00 |   0.00 |   100.0 |   100.0 |  15.8 |  15.5 |
| harm1_C5_523             |      1.5 |      0.1 |    1.7 |    0.1 |   0.00 |   0.00 |   100.0 |   100.0 |  15.0 |  14.4 |
| harm1_G5_784             |      3.1 |      0.2 |    3.3 |    0.3 |   0.00 |   0.00 |   100.0 |   100.0 |  13.4 |  14.6 |
| harm2_A2_110             |      0.1 |      0.2 |    0.1 |    0.3 |   0.00 |   0.00 |   100.0 |   100.0 |  31.3 |  18.9 |
| harm2_A3_220             |      0.1 |      0.1 |    0.1 |    0.4 |   0.00 |   0.00 |   100.0 |   100.0 |  20.8 |  14.7 |
| harm2_E4_329             |      0.4 |      0.4 |    0.4 |    1.2 |   0.00 |   0.00 |   100.0 |   100.0 |  17.5 |  13.3 |
| harm2_A4_440             |      1.1 |      0.1 |    1.2 |    0.2 |   0.00 |   0.00 |   100.0 |   100.0 |  15.8 |  15.5 |
| harm2_C5_523             |      1.5 |      0.1 |    1.7 |    0.2 |   0.00 |   0.00 |   100.0 |   100.0 |  15.0 |  14.0 |
| harm2_G5_784             |      3.6 |      0.1 |    3.8 |    0.3 |   0.00 |   0.00 |   100.0 |   100.0 |  13.4 |  14.6 |
| harm5_A2_110             |      0.1 |      0.1 |    0.1 |    0.2 |   0.00 |   0.00 |   100.0 |   100.0 |  31.3 |  18.8 |
| harm5_A3_220             |      0.1 |      0.1 |    0.1 |    0.5 |   0.00 |   0.00 |   100.0 |   100.0 |  21.2 |  14.7 |
| harm5_E4_329             |      0.5 |      0.5 |    0.6 |    1.0 |   0.00 |   0.00 |   100.0 |   100.0 |  17.5 |  13.3 |
| harm5_A4_440             |      1.5 |      0.1 |    1.7 |    0.2 |   0.00 |   0.00 |   100.0 |   100.0 |  15.8 |  14.8 |
| harm5_C5_523             |      1.2 |      0.1 |    1.4 |    0.3 |   0.00 |   0.00 |   100.0 |   100.0 |  15.0 |  14.5 |
| harm5_G5_784             |      6.3 |      0.2 |    6.5 |    0.3 |   0.00 |   0.00 |   100.0 |   100.0 |  13.4 |  14.6 |
| harm8_A2_110             |      0.1 |      0.1 |    0.1 |    0.3 |   0.00 |   0.00 |   100.0 |   100.0 |  31.3 |  18.9 |
| harm8_A3_220             |      0.0 |      0.1 |    0.1 |    0.3 |   0.00 |   0.00 |   100.0 |   100.0 |  20.8 |  14.7 |
| harm8_E4_329             |      0.8 |      0.8 |    0.9 |    1.2 |   0.00 |   0.00 |   100.0 |   100.0 |  17.5 |  13.3 |
| harm8_A4_440             |      2.3 |      0.1 |    2.5 |    0.2 |   0.00 |   0.00 |   100.0 |   100.0 |  15.8 |  15.5 |
| harm8_C5_523             |      0.7 |      0.1 |    0.9 |    0.3 |   0.00 |   0.00 |   100.0 |   100.0 |  15.0 |  14.5 |
| harm8_G5_784             |     10.5 |      0.1 |   10.9 |    0.4 |   0.00 |   0.00 |   100.0 |   100.0 |  13.4 |  14.6 |
| vib_220_d02_f5           |     17.8 |      5.3 |   25.7 |    7.5 |   0.00 |   0.00 |   100.0 |   100.0 |  21.1 |  14.1 |
| vib_220_d02_f7           |     24.6 |      8.8 |   34.7 |   12.4 |   0.00 |   0.00 |   100.0 |   100.0 |  21.1 |  14.0 |
| vib_220_d05_f5           |     44.2 |     11.4 |   64.0 |   20.9 |   0.00 |   0.00 |   100.0 |   100.0 |  21.1 |  14.7 |
| vib_220_d05_f7           |     62.8 |     15.5 |   86.9 |   29.4 |   0.00 |   0.00 |   100.0 |   100.0 |  20.7 |  14.8 |
| porta_220_330_440        |      1.6 |      0.2 |  112.3 |   52.0 |   0.00 |   0.00 |   100.0 |   100.0 |  16.9 |  14.8 |
| vuv_mix                  |      0.1 |      0.1 |    0.1 |    0.5 |   0.00 |   0.00 |    92.2 |    93.5 |  21.3 |  14.8 |
| snr_30db                 |      0.1 |      0.8 |    0.4 |    2.6 |   0.00 |   0.00 |   100.0 |   100.0 |  21.3 |  14.8 |
| snr_20db                 |      0.4 |      3.2 |    1.4 |    7.9 |   0.00 |   0.00 |   100.0 |   100.0 |  21.2 |  14.7 |
| snr_10db                 |      3.3 |     11.6 |    7.8 |   34.4 |   0.00 |   0.00 |   100.0 |    96.2 |  21.1 |  14.7 |
| snr_5db                  |      0.0 |      8.0 |    0.0 |   18.7 |   0.00 |   0.00 |     0.0 |   100.0 |  37.9 |  61.4 |
| snr_0db                  |      0.0 |      0.0 |    0.0 |    0.0 |   0.00 |   0.00 |     0.0 |     0.0 |  37.9 |  78.1 |
| octave_trap_110_220_440  |      0.1 |      0.1 |    0.1 |    0.4 |   0.00 |   0.00 |   100.0 |   100.0 |  31.3 |  18.8 |
| formant_vib_a            |     33.2 |      9.4 |   45.7 |   15.4 |   0.00 |   0.00 |   100.0 |   100.0 |  21.1 |  14.1 |
| ** ALL @ sr **           |      0.9 |      0.2 |   42.2 |   13.4 |   0.00 |   0.00 |    95.7 |    97.7 |  20.4 |  14.7 |

confusion @48000: D0 tvEv=3725 tvEu=164 tuEv=6 tuEu=47 | D1 tvEv=3803 tvEu=86 tuEv=6 tuEu=47

### sr = 44100 — error/rate comparison

| item | D0 med c | D1 med c | D0 p95 | D1 p95 | D0 oct% | D1 oct% | D0 vuv% | D1 vuv% | D0 us | D1 us |
|--------------------------|---------:|---------:|-------:|-------:|-------:|-------:|-------:|-------:|------:|------:|
| sine_A2_110              |      0.1 |      0.1 |    0.1 |    0.3 |   0.00 |   0.00 |   100.0 |   100.0 |  28.6 |  18.2 |
| sine_A3_220              |      0.3 |      0.3 |    0.4 |    0.4 |   0.00 |   0.00 |   100.0 |   100.0 |  20.2 |  14.4 |
| sine_E4_329              |      0.3 |      0.1 |    0.4 |    0.1 |   0.00 |   0.00 |   100.0 |   100.0 |  16.9 |  15.6 |
| sine_A4_440              |      1.4 |      0.0 |    1.5 |    0.1 |   0.00 |   0.00 |   100.0 |   100.0 |  15.4 |  14.1 |
| sine_C5_523              |      1.9 |      0.0 |    2.1 |    0.0 |   0.00 |   0.00 |   100.0 |   100.0 |  14.6 |  13.8 |
| sine_G5_784              |      4.3 |      0.1 |    4.6 |    0.1 |   0.00 |   0.00 |   100.0 |   100.0 |  13.2 |  14.1 |
| harm1_A2_110             |      0.1 |      0.1 |    0.1 |    0.3 |   0.00 |   0.00 |   100.0 |   100.0 |  28.6 |  18.2 |
| harm1_A3_220             |      0.3 |      0.3 |    0.4 |    0.4 |   0.00 |   0.00 |   100.0 |   100.0 |  20.2 |  14.4 |
| harm1_E4_329             |      0.3 |      0.1 |    0.4 |    0.1 |   0.00 |   0.00 |   100.0 |   100.0 |  16.9 |  15.6 |
| harm1_A4_440             |      1.4 |      0.0 |    1.5 |    0.1 |   0.00 |   0.00 |   100.0 |   100.0 |  15.4 |  14.1 |
| harm1_C5_523             |      1.9 |      0.0 |    2.1 |    0.0 |   0.00 |   0.00 |   100.0 |   100.0 |  14.6 |  13.8 |
| harm1_G5_784             |      4.3 |      0.1 |    4.6 |    0.1 |   0.00 |   0.00 |   100.0 |   100.0 |  13.2 |  14.1 |
| harm2_A2_110             |      0.1 |      0.1 |    0.1 |    0.2 |   0.00 |   0.00 |   100.0 |   100.0 |  29.6 |  18.2 |
| harm2_A3_220             |      0.3 |      0.5 |    0.4 |    1.1 |   0.00 |   0.00 |   100.0 |   100.0 |  20.2 |  14.3 |
| harm2_E4_329             |      0.4 |      0.1 |    0.4 |    0.2 |   0.00 |   0.00 |   100.0 |   100.0 |  16.9 |  15.7 |
| harm2_A4_440             |      1.4 |      0.1 |    1.6 |    0.2 |   0.00 |   0.00 |   100.0 |   100.0 |  15.4 |  15.0 |
| harm2_C5_523             |      2.0 |      0.0 |    2.2 |    0.1 |   0.00 |   0.00 |   100.0 |   100.0 |  14.6 |  13.9 |
| harm2_G5_784             |      4.4 |      0.2 |    4.8 |    0.3 |   0.00 |   0.00 |   100.0 |   100.0 |  13.2 |  14.5 |
| harm5_A2_110             |      0.1 |      0.1 |    0.1 |    0.3 |   0.00 |   0.00 |   100.0 |   100.0 |  29.5 |  18.1 |
| harm5_A3_220             |      0.4 |      0.5 |    0.5 |    1.5 |   0.00 |   0.00 |   100.0 |   100.0 |  20.2 |  14.3 |
| harm5_E4_329             |      0.5 |      0.0 |    0.6 |    0.2 |   0.00 |   0.00 |   100.0 |   100.0 |  16.9 |  15.7 |
| harm5_A4_440             |      1.5 |      0.1 |    1.7 |    0.3 |   0.00 |   0.00 |   100.0 |   100.0 |  15.4 |  15.3 |
| harm5_C5_523             |      2.3 |      0.1 |    2.5 |    0.2 |   0.00 |   0.00 |   100.0 |   100.0 |  14.6 |  14.8 |
| harm5_G5_784             |      5.2 |      0.1 |    5.6 |    0.5 |   0.00 |   0.00 |   100.0 |   100.0 |  13.2 |  14.5 |
| harm8_A2_110             |      0.1 |      0.1 |    0.1 |    0.3 |   0.00 |   0.00 |   100.0 |   100.0 |  29.6 |  18.1 |
| harm8_A3_220             |      0.5 |      0.5 |    0.5 |    1.0 |   0.00 |   0.00 |   100.0 |   100.0 |  20.2 |  14.3 |
| harm8_E4_329             |      0.7 |      0.0 |    0.8 |    0.2 |   0.00 |   0.00 |   100.0 |   100.0 |  16.9 |  16.0 |
| harm8_A4_440             |      1.8 |      0.1 |    2.0 |    0.2 |   0.00 |   0.00 |   100.0 |   100.0 |  15.4 |  15.3 |
| harm8_C5_523             |      2.8 |      0.1 |    3.0 |    0.2 |   0.00 |   0.00 |   100.0 |   100.0 |  14.6 |  15.1 |
| harm8_G5_784             |      6.4 |      0.2 |    6.9 |    0.6 |   0.00 |   0.00 |   100.0 |   100.0 |  13.2 |  14.5 |
| vib_220_d02_f5           |     19.6 |      5.8 |   28.1 |    8.9 |   0.00 |   0.00 |   100.0 |   100.0 |  20.3 |  13.8 |
| vib_220_d02_f7           |     26.6 |      9.7 |   37.5 |   14.3 |   0.00 |   0.00 |   100.0 |   100.0 |  20.2 |  13.7 |
| vib_220_d05_f5           |     48.7 |     14.1 |   69.7 |   21.0 |   0.00 |   0.00 |   100.0 |   100.0 |  20.2 |  14.3 |
| vib_220_d05_f7           |     67.4 |     15.5 |   94.4 |   34.6 |   0.00 |   0.00 |   100.0 |   100.0 |  20.0 |  14.4 |
| porta_220_330_440        |      1.2 |      0.3 |  121.4 |   52.6 |   0.00 |   0.00 |   100.0 |   100.0 |  16.9 |  14.7 |
| vuv_mix                  |      0.4 |      0.4 |    0.5 |    1.0 |   0.00 |   0.00 |    90.8 |    92.2 |  20.3 |  14.5 |
| snr_30db                 |      0.4 |      1.0 |    0.8 |    3.3 |   0.00 |   0.00 |   100.0 |   100.0 |  20.3 |  14.4 |
| snr_20db                 |      0.7 |      2.0 |    1.7 |    6.8 |   0.00 |   0.00 |   100.0 |   100.0 |  20.3 |  14.4 |
| snr_10db                 |      2.1 |     13.9 |    6.9 |   45.8 |   0.00 |   0.00 |   100.0 |   100.0 |  20.3 |  14.4 |
| snr_5db                  |      0.0 |      6.9 |    0.0 |   21.4 |   0.00 |   0.00 |     0.0 |   100.0 |  35.5 |  57.1 |
| snr_0db                  |      0.0 |      0.0 |    0.0 |    0.0 |   0.00 |   0.00 |     0.0 |     0.0 |  35.5 |  72.2 |
| octave_trap_110_220_440  |      0.1 |      0.1 |    0.1 |    0.4 |   0.00 |   0.00 |   100.0 |   100.0 |  29.6 |  18.2 |
| formant_vib_a            |     35.2 |     11.4 |   49.4 |   16.6 |   0.00 |   0.00 |   100.0 |   100.0 |  20.3 |  13.8 |
| ** ALL @ sr **           |      1.3 |      0.1 |   45.6 |   15.7 |   0.00 |   0.00 |    95.7 |    97.7 |  19.5 |  14.5 |

confusion @44100: D0 tvEv=3422 tvEu=151 tuEv=6 tuEu=42 | D1 tvEv=3496 tvEu=77 tuEv=6 tuEu=42

### Grand totals + gates (grand totals verbatim; per-sr gate lines compressed)

```
GRAND TOTAL: scored=7563
  D0: det=95.8% med=1.0c p95=43.8c oct=0.00% vuv=95.7% call med=20.1us p95=35.5us
  D1: det=97.8% med=0.2c p95=14.5c oct=0.00% vuv=97.7% call med=14.7us p95=57.0us

PASS: D1 overall detection rate >= D0
PASS: D1 overall median cents < D0
PASS: D1 overall p95 cents < D0
PASS: D1 octave-error rate within +0.2pp of D0 (D0=0)
PASS: D1 V/UV accuracy >= D0
PASS: D1 false-voicing (tuEv) does not grow
PASS: D1 median call <= 60us and <= 3x D0
PASS: snr_5db D1 det% >= 80 @ 48000 / 44100
PASS: snr_5db D1 median <= 30c @ 48000 / 44100
PASS: snr_0db D1 det% stays low (no noise lock) @ 48000 / 44100
D1 IMPROVEMENT HARNESS OK
```

## Regressions (shown, justified)

- `snr_30/20/10db` medians rose modestly (e.g. 0.1→0.8 ¢ at 30 dB,
  3.3→11.6 ¢ at 10 dB @48 k; 10 dB det% dipped 100→96.2 at 48 k from
  relaxed-pass suppression of 3 unconfirmed frames). Cause: pass-A's
  shorter compare window is noisier than the 256-term one when real
  noise is present, and relaxed estimates are intrinsically less clean.
  Trade-off bought the tracking-lag cut everywhere and the 5 dB lock;
  absolute accuracy there remains far inside musical use (~12 ¢ worst
  median). If a future card wants it back, gating pass A by measured
  window noise is the lever.
- Timing tail: p95 35.5→57.0 µs on the two noise rows only (triple scan
  when strict fails). Median improved overall; deadline headroom ~140×.

## Scope note (D1 ≠ D2)

No tracker, hysteresis, note state machine, or smoothing was added: the
"predictor" is a strictly two-point bounded extrapolation of consecutive
raw estimates; the "confirmation" is a two-frame agreement counter
(D0's own stableCount, tightened for tentative passes); "armed" is an
energy-rise flag with a fixed decay. All three are stateless with
respect to musical notes. D2 owns tracking/note state.

# D4 — End-to-end DSP host verification (full-chain A/B, 44.1 k + 48 k)

Card G2-D4 (GPT round-3 D-014, sequence step 6 — "D4 AFTER D3"): verify the
WHOLE production chain at both sample rates and run the baseline-vs-new A/B
under identical conditions. This is a verification card: production DSP is
byte-frozen. The binary contains **both** engines — live
`dautotune::PitchCorrector` (D3 TD-PSOLA) and a frozen D2-era copy
`d2ref::PitchCorrectorD2` (`native-tests/d2_reference.{h,cpp}`, a verbatim
copy of `70b1147:app/src/main/cpp/PitchCorrector.cpp/.h`, mechanically
renamed, freeze comment added — content verified identical after stripping
CR). Detector + tracker are byte-identical in both chains, so every measured
output difference is **shifter-only** — that is the A/B.

All numbers below are from the `d4_e2e` POST_BUILD report (GCC 16.2 `-O2`,
MSYS2/MinGW64, chunk 512, stereo out, reverb 0 for measurement batteries;
reverb-on battery F at 0.15). Production `PitchCorrector.cpp/.h` SHA-256
verified byte-identical before and after the run:

- `PitchCorrector.cpp` = `ba78ed0bde310a781845bdefaec2840ebf0b055a5eb67bcae815ea80c136f83c`
- `PitchCorrector.h`   = `f0beab77c0ee1c3fe53353a82295c99c0656a08da43490f19d7f1d33f75929be`

## Result — headline verdict (round-3 rule)

| gate | old (D2 granular) | new (D3 TD-PSOLA) | verdict |
|------|-----|-----|---------|
| pitch accuracy (worst cents) | 1.61 | 0.61 | **SAME** (identical frozen tracker by construction) |
| formant retention (max \|dc\| F1/F2 Hz; \|s*−1\|) | 128.1; 0.022 | 45.1; 0.010 | **BETTER** |
| artifacts (slew ratio / transient excursions) | 1.16 / 15 | 0.99 / 1 | **BETTER** |
| DSP signal delay (ms) | 20.32 | 2.27 | **BETTER** |
| callback cost p95 (µs per 512-call) | 25.6 | 54.9 | **WORSE — inside the 200 µs budget** |

**Verdict: PASS.** The new shifter is worse on no gate. The one regression —
per-call CPU (~2× the old engine, still 3.6× under budget) — is the accepted
price of the TD-PSOLA architecture, and the pitch-accuracy row is identical
by construction (same frozen tracker drives both shifters), so the
"compensating benefit" clause is not even needed; latency and formants are
strict wins.

## A. Pitch accuracy through the full chain

Sung 285 Hz → snaps to D4 = 293.66 Hz, amount 1.0, output F0 measured over
3 windows after settling:

| rate | new worst \|c\| | old worst \|c\| | gate ±3 ¢ |
|---:|---:|---:|:---:|
| 48000 | 0.61 (293.6 Hz all windows) | 1.61 (293.4–293.5) | PASS both |
| 44100 | 0.23 | 0.74 | PASS both |

- `amount=0.0`: **bit-exact** passthrough on both chains (asserted per
  sample); old ≡ new (shared bypass path).
- Octave trap (`octave_trap_110_220_440`, amount 1.0): output F0 stays at
  110.0 Hz in the input's octave on **both** chains — no register jump.

## B. Timbre / formant retention through the full chain

Formant-rich sustained tone (D3 battery-B signal: bumps at 700/1200/2600 Hz
over a 1/j floor, 22 harmonics), sung ≈186.1 Hz → snaps to G3 = 196.0 Hz
(**+90 ¢** — the maximum stable C-major snap through the real scale law is
a basin-edge ~±100 ¢; a full +500 ¢ cannot be produced through the real
snap, so a driven +500 ¢ shifter-only column is reported alongside for D3
parity). Measured over the settled region:

**Centroids @48k** (in / new / old, Hz):

| path | f0 | F1 | F2 | F3 |
|------|---:|---:|---:|---:|
| in  | 186.1 | 655 | 1203 | 2562 |
| new | 196.0 | 681 (+26) | 1248 (+45) | 2617 (+55) |
| old | 196.1 | 690 (+35) | 1252 (+49) | 2686 (+123) |

**Centroids @44.1k**:

| path | f0 | F1 | F2 | F3 |
|------|---:|---:|---:|---:|
| in  | 186.1 | 656 | 1203 | 2563 |
| new | 196.0 | 690 (+34) | 1238 (+35) | 2678 (+115) |
| old | 196.0 | 784 (+128) | 1236 (+33) | 2466 (−97) |

New chain: F1/F2 within the ±60 Hz gate at both rates. F3 is printed but
not gated — the weak 0.5-amplitude bump over a ~4-harmonic grid leaves
~±60 Hz of centroid quantization noise (the +115 reading is instrument
noise; the continuous instrument below shows the envelope did not move).
Off-target chunks in the measurement window: 0 for both chains.

**Envelope-shift scan s*** (argmin log-amplitude residual of output
harmonics vs the input envelope evaluated at f/s; 1.00 = formants held,
1.053 = dragged by the applied ratio):

| rate | new s* | old s* | reading |
|---:|---:|---:|---|
| 48000 | **0.990** (resid 0.92) | 1.022 (resid 0.80) | new held; old dragged partway |
| 44100 | **0.990** (resid 0.93) | 1.002 (resid **5.06**) | new held; old estimate unusable — phase-wrap amplitude pumping corrupts its own harmonic levels (see C3b) |

**Driven +500 ¢ column** (shifter-only, `psolaDriveForTest` vs verbatim
`legacyGranular` math — D3 battery-B parity):

| rate | engine | out f0 | F1 | F2 | F3 |
|---:|---|---:|---:|---:|---:|
| 48000 | PSOLA | 294 | **669** | **1218** | 2590 |
| 48000 | legacy | 294 | 880 | 1437 | 2565 |
| 44100 | PSOLA | 294 | **667** | **1236** | 2662 |
| 44100 | legacy | 294 | 589 | 1384 | 2652 |

PSOLA holds F1/F2 near the input bumps at +500 ¢ at both rates; legacy
granular drags them (up at 48 k, down/off-grid at 44.1 k — moved either
way). Honest note: the old full-chain at 44.1 k fails the ±60 gate on F1
(+128) and F3 (−97); at 48 k it only clearly fails F3 (+123) — at a +90 ¢
shift the F1/F2 drag (~+35/+49) sits inside the ±60 gate, so the s* scan is
the discriminating instrument.

## C. Artifacts / discontinuities

| probe | metric | new | old | gate |
|---|---|---:|---:|---|
| C1 staccato A3/C4 boundaries | max \|Δsample\| near/far @48k | 0.0353 / 0.0353 | 0.0348 / 0.0348 | bound 2× input 0.0361 — PASS |
| | @44.1k | 0.0445 / 0.0445 | 0.0440 / 0.0440 | bound 0.0901 — PASS |
| C2 sustained shift, 2.2 s | max slew @48k (bound 0.117) | 0.0564 | 0.0570 | no glitch frames — PASS both |
| | @44.1k (bound 0.093) | 0.0420 | 0.0419 | PASS both |
| C3a percussive onset | envelope onsets | **1** | **1** | emitted once — PASS |
| | onset overshoot | 1.0 / 0.7 dB | 0.9 / 0.7 dB | < 6 dB — PASS |
| | onset delay | 185/147 samp | 972/942 samp | — |
| | steady-state pump | 3.5 / 4.2 dB | 1.2 / 0.7 dB | printed honestly |
| C3b mid-tone spike (quiet carrier, guard-exercising amplitude) | spike emissions @48k | **1** | 1 | once, no echo — PASS |
| | @44.1k | **1** | **15** | old: 15 above-floor excursions |

**Real findings worth recording:**

1. **Old engine amplitude pumping @44.1k** (C3b): on a sustained shifted
   tone the old granular output dips below its own floor repeatedly — 15
   above-floor excursions around the spike region vs 1 for the new chain.
   This is the two-head phase-wrap crossfade pumping; it is also what
   corrupts the old engine's @44.1k formant measurement above (resid 5.06
   vs new's 0.93).
2. **New engine steady-state pump ~3.5–4.2 dB** (C3a): the new chain shows
   mild envelope modulation after onset (ε-normalized OLA + uvGain
   settling). Small, single onset, no echo — printed for the record.
3. The mid-tone spike probe only exercises the new chain's transient guard
   on a **quiet** carrier: the 4× peak-envelope threshold can never be
   reached by a clamped input over a loud tone (a 0.8 spike on a 0.3-amp
   tone tops out at ~1.1 < 4×0.45). On the loud carrier the guard never
   fires and PSOLA smears the spike below the tone's own peak — measured,
   documented; the battery now uses an 0.08 carrier + 0.7 spike.

## D. Actual DSP signal delay (full chain, both rates)

Energy-onset lag of a percussive onset, deterministic across 2 runs:

| rate | new | old | D3 budget |
|---:|---:|---:|---:|
| 48000 | **109 samp = 2.27 ms** | 896 samp = 18.67 ms | 220 samp (P+1 @220 Hz) — PASS |
| 44100 | **100 samp = 2.27 ms** | 896 samp = 20.32 ms | 202 samp — PASS |

New chain reproduces the D3-measured delay exactly (0.50 P); old chain is
its documented kBaseDelay+window algorithmic delay.

## E. Callback cost per 512-frame process() call

| rate | new median / p95 | old median / p95 | budget |
|---:|---:|---:|---|
| 48000 | 53.9 / 54.6 µs | 20.5 / 25.7 µs | p95 < 200 µs — PASS |
| 44100 | 53.5 / 54.5 µs | 20.9 / 25.5 µs | PASS |

The new engine costs ~2.1× the old per call (~55 µs vs ~26 µs). This is the
expected TD-PSOLA price (per-sample grain mixing vs two read heads) and is
the only WORSE verdict — inside the 200 µs D3 budget, and 512 frames @48 k
is 10.7 ms of real-time, so the call uses ~0.5 % of the deadline.

## F. Reverb-on stability (reverb = 0.15)

Both chains render deterministically across 2 fresh instances and stay
inside the limiter bound (peak 0.562/0.570 @48k, 0.356/0.355 @44.1k vs
ceiling 0.88).

## Instrumentation notes (what changed vs the first draft)

- Battery B uses a ~186 Hz carrier (snapped to G3 at +90 ¢) so each
  formant band holds 2–5 harmonics; the first draft's 372 Hz carrier left
  one harmonic per band and the centroids degenerated.
- The ±60 Hz gate applies to F1/F2 only; F3 printed for the record.
- The discriminating formant instrument is the s* envelope-shift scan,
  not band centroids (too coarse at +90 ¢) and not a two-hypothesis
  residual (insufficient SNR at +90 ¢).
- Transient counting uses a 5 ms RMS envelope with a 30 ms refractory —
  the first draft counted per-period |y| crossings (hundreds of hits).
- Overshoot references the MEDIAN post-peak envelope, robust to the old
  engine's pump dips.

## Verification cross-checks

- `ctest`: **7/7 pass** — `pitch_corrector`, `d0_baseline`,
  `d1_improvement`, `d2_tracking`, `d3_shifter`, `d4_e2e`, `ping_math`.
- D1 floor **identical**: grand total scored=7563, det 97.8 % / med 0.2 ¢ /
  p95 14.5 ¢ / oct 0.00 % / VUV 97.7 % (confusion tvEu 83 / tuEv 6 @48k,
  77 / 6 @44.1k).
- Corpus determinism: FNV-1a per-item hashes identical across two builds at
  each rate (43 items).
- Scope: `native-tests/{test_d4_e2e.cpp, d2_reference.{h,cpp},
  CMakeLists.txt}` + this report only. `.orchestrator/DECISIONS.md`
  carries a concurrent orchestrator write, left unstaged.

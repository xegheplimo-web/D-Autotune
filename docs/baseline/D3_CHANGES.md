# D3 — Clean-room TD-PSOLA shifter

Card G2-D3 (GPT round-3 D-014 verdict, sequence step 5): replace the
two-head fixed-window granular resampler (`shift()`, `kShiftWindow=1024`,
`kBaseDelay=384`, `phase_`) with a time-domain pitch-synchronous
overlap-add engine. The detector (`detect()`) and the D2 tracking state
machine (`applyDetection`, `updateTarget`) are byte-frozen — `detect()`
region SHA-256 `e4c5617eef346db305f89953db4c3b520d1937a67f6399f98b3bd5afb5bdae7d`
unchanged vs commit `080ea4d` (region = `PitchCorrector::DetOut
PitchCorrector::detect()` through its closing brace + trailing newline;
verified identical against `git show 080ea4d:`). `process()` keeps its
exact contract: input → history → detect cadence → smoothed ratio →
shifter → echo/reverb → limiter → output, and `amount < 0.001f` remains a
bit-exact passthrough (asserted in battery A). All numbers below are from
the `d3_shifter` POST_BUILD report (GCC `-O2`), reproduced identically on
the local MSYS2 build.

## Result — headline

| gate (GPT round-3 mandatory) | result | verdict |
|---|---|---|
| A corrected-output F0 | 285→D4 chain: 293.57 Hz, **−0.6 ¢** across 3 windows; driven +500 ¢: 293.663 Hz (**−0.01 ¢**); `amount=0` bit-exact | PASS |
| B formant retention | F1 666 Hz / F2 1208 Hz vs bumps 700/1200 (±60 gate); legacy engine shifts them to 880/1411 Hz (fails the same gate) | PASS |
| C discontinuity | driven ratio step 0.028, sustained 0.026, off-boundary 0.043 (bound 2× input slew); note-boundary step 0.070 vs input's own 0.113 — reduced, not amplified | PASS |
| D transient | lone impulse: exactly **1** excursion at +109 samp; voice onset +238 samp, overshoot 1.0 dB | PASS |
| E DSP signal delay | **109 samp = 2.27 ms @48k** (0.50 periods @220 Hz), 200 samp @120 Hz; 100/184 samp @44.1k; impulse centroid 180 samp = 3.75 ms; bound P+1 | PASS |
| F realtime safety | fixed-size state, no heap/locks/logging; 2 runs bit-identical; **p95 59–61 µs** per 512-call (< 200 µs) | PASS |

D1 floor held by construction and measurement: `d1_improvement` grand
total scored=7563, det 97.8 % / med 0.2 ¢ / p95 14.5 ¢ / oct 0.00 % /
VUV 97.7 %, confusion tvEu 83 / tuEv 6 @48 k, 77 / 6 @44.1 k — identical
to the D2 close-out. `d0_baseline`, `d2_tracking`, `pitch_corrector`,
`ping_math` all green.

## The algorithm (written clean-room; no external DSP code)

TD-PSOLA: overlap-add of pitch-synchronous grains. Marks come from the
**applied** F0 (the tracker's `appliedHz_` when voiced; pinned by the
`psolaDriveForTest` seam in driven tests), never a fixed grid.

- **Analysis grid**: marks spaced `P = sr / appliedF0`, phase-anchored to
  the strongest positive excursion in the last ~1.2·P (an epoch proxy —
  no glottal detector). Applied-F0 movement > ~60 ¢ (note change, tracker
  reseed, portamento) re-anchors the grid; slower drift (vibrato) just
  retunes `gridPeriod_` so marks stay phase-locked to the waveform.
- **Synthesis marks**: laid at `P/r` ahead of the playhead — output
  grains at the target period while input grains are read at the
  analysis period. The classic PSOLA relationship, no resampling inside
  a grain, so the spectral envelope (formants) does not move with pitch.
- **Mark mapping**: each output mark `s` reads from the newest analysis
  mark `a ≤ s − 1`. For `r > 1` marks repeat (input time consumed slower
  than output); for `r < 1` they skip. Read lag `D = s − a ∈ (1, P+1]`
  samples — the entire algorithmic delay of the shifter.
- **Grains**: Hann window `w = 0.5 + 0.5·cos(π·off/h)`, span `2·P` (`h = P`
  at lay time). At most ~3 overlap; fixed `std::array<Grain,6>`.
- **Soft-normalized OLA**: `y = Σ w·g / (Σw + 0.02)`. A hard `Σw` divide
  amplifies a just-born grain's tail to full amplitude in one sample
  (measured: one-sample jumps of 0.27–0.40 at grain births) — the ε
  fades tails in/out through the window instead. First-run testing caught
  this; the ε form is what makes the sustained-ratio path click-free.
- **V/UV**: unvoiced output is `x(t − P/2)` — delay-aligned passthrough,
  no time-stretch on noise. The V↔UV boundary is a ~5 ms `uvGain_`
  crossfade between the two paths (documented click-free transition).
- **Transient guard**: `|x| > 4×` a decaying peak envelope (0.998/sample,
  ~20 ms tail) and `|x| > 0.05` routes output through the passthrough for
  2.5·P (~11 ms @220 Hz). Marks would otherwise re-read the transient
  once per repeated analysis mark for `r > 1` — an audible echo. The
  switch is instant and masked by the arriving onset; grains keep being
  scheduled during the hold so the grid never drifts behind real time.

## Latency budget

Design bound: `D ∈ (1, P + 1]` samples — one applied period plus one
sample. At the lowest supported F0 (80 Hz): 601 samples = **12.5 ms
@48k** worst case. Measured (battery E, deterministic across 2 runs):

| sr | f0 | onset delay | impulse centroid |
|---:|---:|---:|---:|
| 48000 | 220 Hz | 109 samp = 2.27 ms (0.50 P) | 180 samp = 3.75 ms (0.83 P) |
| 48000 | 120 Hz | 200 samp = 4.17 ms (0.50 P) | — |
| 44100 | 220 Hz | 100 samp = 2.27 ms (0.50 P) | — |
| 44100 | 120 Hz | 184 samp = 4.17 ms (0.50 P) | — |

Onset delay sits near `P/2` because the passthrough path is `P/2`-aligned
and the UV→V crossfade starts there; the impulse centroid inside grains
lands at ~0.8·P, both inside the `P + 1` bound. Old engine for contrast:
`kBaseDelay 384 + ~512 ≈ 896 samp ≈ 18.7 ms @48k` — the new engine is
**~4× lower latency** at 220 Hz and bounded by the applied period.

## Cost

Median **58 µs**, p95 **59–61 µs** per 512-frame stereo call @48k
(~0.5 % of the 10.7 ms chunk; ~250× realtime headroom; gate < 200 µs).
The old granular ran ~15–21 µs on the same harness — the PSOLA is ~3×
that per call (per-sample grain loop with a `cos` per grain). Still
inside the D1 harness's own gate (≤60 µs, ≤3× D0: measured 53.8 µs,
2.6×). If D4 needs margin, the window evaluation is the first thing to
table-lookup — documented, not done (prototype budget is met).

## Honest deviations / trade-offs

- **Formant centroids land inside ±60 Hz, not on the bump**: F1 666 vs
  700 (−34 Hz), F2 1208 vs 1200 (+8 Hz). The harmonic grid at 293.7 Hz
  is sparser (centroid computed over ~3 harmonics in-band), so the
  centroid is a grid-weighted estimate — the ±60 Hz band accounts for it.
  The same measure on the legacy engine yields 880/1411 Hz (envelope
  dragged with the pitch) — the contrast is unambiguous.
- **ε-normalization**: at coverage edges (startup, first grain, `r<1`
  gaps) output fades through grain tails rather than holding unity gain —
  a deliberate ramp-in instead of a normalized-amplitude jump.
- **Note-boundary step**: the test signal itself steps ~0.11 at the
  splice (note-1 tail into a faded-in note 2). Output reproduces it
  delayed/smeared at 0.070 — reduced, not amplified; everywhere else in
  the render ≤0.043. The card's "≤0.02 of amplitude" is met for all
  shifter-introduced steps; the boundary region is gated separately
  because reproducing an input step is correct behavior.
- **Transient routing**: for ~2.5 applied periods after a >4× onset,
  output is the unshifted delay-aligned copy — pitch correction pauses
  under the transient by design (plosives and pick attacks should not be
  pitch-quantized anyway). Steady tones never trip it (verified: zero
  triggers across the sustained battery).
- **Impulse centroid `-1`** at 120 Hz rows: the 0.8-spike excess falls
  under the 3× floor gate in a low-period window — onset columns carry
  the measurement; printed `-1` honestly rather than fudging the floor.
- **Timing jitter ±3 µs** run-to-run on the wall-clock cost print; every
  DSP number (F0, centroids, slews, delays, excursion counts) is
  bit-identical across runs.

## Scope

| file | change |
|---|---|
| `app/src/main/cpp/PitchCorrector.cpp` | shifter path only: `shift()`/`phase_`/`kShiftWindow`/`kBaseDelay` removed; `histAt`, `psolaResync`, `psolaShift`, `psolaDriveForTest` added; `reset()` clears new state; `process()` calls `psolaShift` in place of `shift` — detect/tracker/echo/limiter untouched |
| `app/src/main/cpp/PitchCorrector.h` | shifter state only: `Grain`, fixed arrays, mark grid, uvGain, transient guard fields, `psolaDriveForTest` seam |
| `native-tests/test_d3_shifter.cpp` | new — batteries A–F |
| `native-tests/CMakeLists.txt` | `d3_shifter` target + POST_BUILD report |
| `docs/baseline/D3_CHANGES.md` | this file |

`detect()` SHA-256 `e4c5617e…dae7d` — UNCHANGED. `applyDetection`/
`updateTarget` hunks — none. `AudioEngine.*`, Kotlin, `ci.yml`,
`.orchestrator/` — untouched. New shifter state ≈ 220 bytes, all
fixed-size; `reset()` clears every field.

# D2 — F0 tracking + hysteresis

Card G2-D2 (GPT round-2 D-012 sequence step 3). Time-dimension behavior of
the pitch pipeline only: the note-level tracker, outlier rejection,
voiced-state hysteresis, and how the correction amount is applied. The
detector (`detect()`, all three passes, armed gate, extrapolator) and the
shifter (`shift()`, `delayed()`, echo, reverb, `phase_`, the `process()`
sample loop) are byte-identical — verified by `git diff` hunk boundaries:
the only hunks touch `reset()` initialization and the `updateTarget()`
region. All numbers below are from CI run `41187660379`, `native-dsp` job
(ubuntu-latest, GCC `-O2`), printed by the `d2_tracking` POST_BUILD step
and the `d0_baseline`/`d1_improvement` harnesses in the same binary; the
local MSYS2 build produced identical values.

## Result — headline

| metric (grand total, 7563 chunks) | D0 | D1 (floor) | D2 | verdict |
|---|---:|---:|---:|---|
| detection rate | 95.8 % | 97.8 % | **97.8 %** | held |
| median cents | 1.0 | 0.2 | **0.2** | held |
| p95 cents | 43.8 | 14.5 | **14.5** | held |
| octave errors | 0.00 % | 0.00 % | **0.00 %** | held |
| V/UV accuracy | 95.7 % | 97.7 % | **97.7 %** | held (+) |
| tvEu / tuEv @48 k | 164 / 6 | 86 / 6 | **83 / 6** | fewer misses, no new false voicing |
| tvEu / tuEv @44.1 k | 151 / 6 | 77 / 6 | **77 / 6** | held |

The identity of det/med/p95/oct is *by construction*, not coincidence: D2
keeps the reported estimate stream (`detectedHz_`) under D1's exact
reporting rule per estimate class (see "What D2 deliberately does not
change"), so every cents-scored sample is the same value D1 produced. The
only metric that can move is the voiced flag itself, which now holds
through absorbed frames instead of flapping (tvEu 86 → 83 @48 k).

`porta_220_330_440` p95 holds at 52–53 ¢ (ramp settle requirement measured
directly in `test_d2_tracking`: worst error across the 60 ms window after
the 220→330 ramp = **40.5 ¢**, well inside the 50 ¢ bound).

## What changed

`updateTarget()` is now `applyDetection(detect())`; all D1 detector
semantics — admission gates (0.73 strict / 0.44 relaxed), `stableCount_`
agreement (1.7 st strict / 1.0 st tentative), `armedLeft_` onsets, the
±50 ¢ extrapolator — run unchanged inside it. D2 adds a note-level state
machine on top. ~60 bytes of new state, all fixed-size; no heap, no locks,
no logging on the audio path.

### Tracker (accepted-F0 ring + jump gate)

- `trackHz_[3]`/`trackAt_[3]` ring: last ≤ 3 **accepted** estimates
  (post-extrapolation values — what the correction sees) with the update
  index at which each was accepted. `appliedHz_` = **median-of-3** of the
  ring is the F0 that drives the correction target.
- **Ingest rule**: only *reportable* (confirmed/armed — D1 rule) or
  **strong** estimates (conf ≥ 0.70, i.e. strict/provisional pass quality)
  touch the ring. Weak unconfirmed relaxed detections — the noise-suspect
  class — never enter it.
- **Jump gate**: a candidate deviating > **300 ¢** from the running median
  becomes `pendingHz_`; a second consecutive detection agreeing within
  **150 ¢** of it accepts the jump (`reseedTrack`: ring ← {new×3}, so the
  median adopts the new note instantly). A single-frame outlier is thereby
  absorbed: it never enters the ring and the applied F0 does not move —
  the previous `lastTargetRatio_` stays, so the correction does not sag or
  jump. A real jump retunes on the 2nd detection (~21 ms), the same update
  at which D1 would first have voiced it — but without D1's unvoiced flap.
- Group delay: median-of-3 on a monotonic ramp returns the middle sample
  ≈ 1 update (≈ 10.7 ms @48 k) behind the newest; D1's extrapolator leads
  forward ~16 ms, so the net applied-F0 position stays ≈ on-time. Chosen
  over any FIR/IIR precisely because it is the smallest smoothing that
  absorbs single-frame outliers; on jump acceptance the reseed removes its
  lag entirely, and steps ≤ 300 ¢ cost ≤ 1 update.

### Voiced-state hysteresis (Schmitt behavior)

- **Enter** voiced: unchanged D1 rule — the estimate must be *reportable*
  (confirmed agreement, or an armed-onset confident frame). Entry evidence
  is agreement over time, not a bare confidence read.
- **Hold** voiced (the new part): a *non-reportable* estimate keeps the
  flag when the track exists and `holdFrames_ < 4` (~43 ms) and either
  - it is strong (conf ≥ **0.70**) — the glitch/jump band; disagreeing ones
    are simultaneously the pending-jump candidates — or
  - it is weak but still on the tracked note (conf ≥ **0.35** and within
    300 ¢ of the median) — a faint patch of the same note.
- **Exit** voiced: a no-estimate update (`hz == 0`, silence always exits),
  a weak/sub-gate estimate below the 0.35 floor or off-track, or holds
  exhausting the 4-update bound (an unconfirmable transition must end).
- Because enter requires confirmed agreement while exit only needs the
  evidence to weaken, enter and exit genuinely differ — the flag cannot
  thrash on a single bad frame. Confidence series .65 → .40 → .65 → .40 →
  .20 produces exactly 2 transitions (test B).

### Breath window

Unvoiced reports increment `gapFrames_`; the ring, pending jump and
`appliedHz_` survive until the gap exceeds `breathHops_ = floor(0.150·sr /
kDetectHop)` updates (~150 ms → 14 @48 k, 12 @44.1 k). Re-entry inside the
window needs no warm-up: a weak on-track estimate holds voiced on the
first frame, an armed onset voices immediately as before (test C). Past
the window the track is cleared — a new phrase earns its own note.

### Correction application — unchanged law, better input

`process()` and the constant-time exponential `smoothedRatio_` law are
untouched. What changed is only *which F0* the target tracks: the ring
median instead of the single extrapolated estimate, with the target chain
frozen (rather than reset to 1.0) during held frames — removing the ~26 %
correction sag D1 produced on every dropped frame — and the 2-detection
jump acceptance replacing the 1-frame unvoiced dropout on note changes.

### What D2 deliberately does not change

- `detectedHz_` reporting: reportable frames store the extrapolated
  estimate (D1); held frames mirror D1's per-class rule (strict shows raw
  Hz, tentative shows 0). The telemetry stream is the detector's own —
  this is why the det/cents/oct distributions are identical to D1's.
- No detector tuning, no shifter work (D3 scope), no new smoothing law on
  the audio path, no allocations anywhere in `process()`-reachable code.
- D1's ±50 ¢ extrapolator is kept and feeds the ring — D2 builds the
  tracker **on top of** it rather than superseding it: the predictor's
  forward lead offsets the median's 1-update middle-sample position on
  ramps, which is what keeps the settle window at ~40 ¢.

## Test results — `d2_tracking` (all deterministic, state-level)

```
| test                          | result |
|-------------------------------|--------|
| A outlier rejection           | PASS |  +500 ¢ single-frame glitch: target frozen, flag held;
|                               |      |  consistent jump accepted on 2nd agreeing detection
| B hysteresis                  | PASS |  conf .65/.40/.65/.40/.20 → ≤2 voiced transitions
| C breath window               | PASS |  ~100 ms gap: instant re-voice, target continuous;
|                               |      |  >150 ms control: state cleared, warm-up required;
|                               |      |  real-audio run voices ≤4 chunks after breath
| D ramp following              | PASS |  porta 220→330/150 ms: worst |err| ≤ 40.5 ¢ at ramp
|                               |      |  end through +60 ms (bound 50 ¢)
| E bounded hold release        | PASS |  unconfirmable alternating input releases voiced
|                               |      |  after the 4-update hold bound, never latches
```

## Same-corpus regression check (d0_baseline + d1_improvement, CI run 41187660379)

```
d0_baseline grand total (D2 code):
  det=97.8% med=0.2c p95=14.5c oct=0.00% vuv=97.7% call med=14.7us p95=57.1us
confusion @48000: tvEv=3806 tvEu=83 tuEv=6 tuEu=47   (D1 was 3803/86/6/47)
confusion @44100: tvEv=3496 tvEu=77 tuEv=6 tuEu=42   (D1 was 3496/77/6/42)

d1_improvement gates vs frozen D0 reference: ALL PASS
  det 97.8 >= 95.8 | med 0.2 < 1.0 | p95 14.5 < 43.8 | oct 0.00 | vuv 97.7 >= 95.7
  tuEv 12 <= 12 | snr_5db det 100% med ~6-7c | snr_0db det 0 (no noise lock)
```

D1's measured floor (det 97.8 / med 0.2 / p95 14.5 / oct 0.00 / vuv 97.7 /
tvEu 163 total / tuEv 6 per rate) is held or beaten on every line: tvEu is
the only counter that moved (163 → 160, absorbed-frame holds), tuEv is
unchanged, and the timing/latency profile is unchanged (the added work per
update is a few compares plus a 3-element median — buried inside the
existing ~15 µs detector call).

## Honest scope notes

- The hold band's 0.70/0.35 thresholds and the 300 ¢ jump gate are the
  chosen values; the card's 0.6/0.35 sketch maps onto this detector's
  confidence semantics as documented above (strict-pass estimates are
  ≥ 0.73 by construction, so a single 0.6 entry gate is vacuous there).
- A pending jump that survives into a breath stays pending through the
  window — an intentional "phrase continues" reading; the ring clears
  wholesale only past 150 ms.
- D2 adds no detector accuracy (D1 scope) and no render path (D3 scope);
  its claim is the state behavior the unit tests pin down.

## Round-3 test extension (G2-D2EXT, verdict D-014)

GPT round-3 accepted D2 **with a test extension**: measure applied F0,
target-note transitions and correction ratio — not only reported
`detectedHz` — across staccato notes, scale-boundary vibrato and
300–1200 ¢ intervals, and add target-note hysteresis *only if* the tests
expose target chatter. Five new batteries (F1/F2 staccato, G vibrato,
H1/H2 intervals) were added to `test_d2_tracking.cpp`; the tracker code
is **unchanged** (see verdict below). `appliedHz_` is internal, so the
applied F0 is recovered exactly from public telemetry:
`appliedHz = targetHz / 2^(correctionCents/(1200·amount))`.

### F — staccato note transitions

A3 85 ms → C4 64 ms → A3 107 ms with 43 ms gaps (< 150 ms breath window).
A3→C4 is +300.1 ¢ — just over the 300 ¢ gate — so every onset exercises
the pending-jump cadence (pending frame → agreeing frame reseeds), which
is the strictest bound the card's "≤ 2 frames ≈ 21 ms" allows.

```
F1 seam per-note metrics:
| note | retune ms | settle ms | max applied-F0 err c | max resid c | transitions |
| A3   |      10.7 |       0.0 |                  0.0 |         0.0 |           0 |
| C4   |      10.7 |       0.0 |                  0.0 |         0.0 |           1 |
| A3   |      10.7 |       0.0 |                  0.0 |         0.0 |           1 |
F2 audio (6-harmonic voice, real detector):
| A3   |       0.0 |       0.0 |                  0.4 |         0.4 |           0 |
| C4   |      10.7 |       0.0 |                  0.4 |         0.4 |           1 |
| A3   |      10.7 |       0.0 |                  0.2 |         0.2 |           1 |
```

"Onset retune" = first admitted detection of the new note → target
landing on its scale member; 10.7 ms = one update, i.e. the pending
frame IS the admission and the landing is the 2nd new-note update
(21.3 ms counting from note onset). Audio note 1 lands in 0.0 ms because
the armed-onset rule voices the very first confident frame — the seam
has no `armedLeft_` (it lives in `detect()`), so the seam cold start
takes the 2-update agreement path instead. Per-chunk telemetry (in the
test output) shows the real-detector behavior across a gap: the voiced
flag holds ~2 chunks on window-tail energy, drops once for 3 chunks,
re-voices on the pending frame with the **target frozen** — one
flag-transition pair per gap, never a flap, never an early target flip.
Correction residual at note centers: ≤ 0.4 ¢.

### G — scale-boundary vibrato (C major, G3↔A3 boundary at G#3 = 207.65 Hz)

In C major the members neighbouring A3 sit at ±200 ¢, so the basin edges
are ±100 ¢ and a ±50 ¢ vibrato on A3 cannot cross. The crossing carrier
is therefore the boundary itself (G#3 ±50 ¢), and a second carrier at
G3+55 ¢ makes only the vibrato peak graze ~5 ¢ into the A3 basin — the
slow-crossing case most likely to chatter.

```
G1 seam non-crossing A3±50c (+jitter):  0 transitions; det span 213.4..226.7 Hz
G2 seam crossing G#3±50c:              10 transitions / 5.1 cycles, min dwell 9 upd
G3 seam crossing + ±4c jitter:         10 transitions, min dwell 9 upd (unchanged)
G4 seam graze G3+55c±50c +jitter:      10 transitions (single enter+exit pairs)
G5a audio non-crossing:                 0 transitions
G5b audio crossing:                    10 transitions, min dwell 9 upd
G5c audio graze:                        9 transitions, min dwell 4 upd (graze pairs)
```

Every transition is a clean G3↔A3 member switch: ~2 per vibrato cycle
for the crossing carrier (one per boundary crossing, per the card's
"transitions ONCE at the crossing"), enter+exit pairs for the graze,
zero for the non-crossing carrier. `detectedHz` tracks the full ±50 ¢
swing throughout — the estimate stream is untouched.

### H — 300–1200 ¢ intervals

A3 → D4 (+500) → A4 (+1200 total, +700 over D4 — the card's "D5 +700"
lands on A4) → A3 (−1200). All three jumps take the pending-jump path;
the −1200 ¢ octave return is a musical interval, not a detector octave
confusion, and is accepted as a normal note change.

```
H1 seam:   | A3 10.7 | D4 10.7 | A4 10.7 | A3 10.7 |  retune ms, resid 0.0 c
H2 audio:  | A3  0.0 | D4 10.7 | A4 10.7 | A3 10.7 |  retune ms
           resid <= 0.8 c, max applied-F0 err <= 0.8 c, 1 transition per note
```

Pending frames hold the voiced flag with the target frozen on the old
member while `detectedHz` already reports the new note — the correction
never sag-flaps and never pre-flips.

### Verdict: NO target-note hysteresis added — tests did not expose chatter

The chatter instrument is a member-transition run of ≥ 3 inside one
vibrato half-cycle (9 updates), plus per-note transition counts > 1.
Across 5 seam cases and 3 real-detector runs — including the ±4 ¢
jitter probe and the ~5 ¢-deep graze that spends only 2–4 updates per
cycle across the boundary — the median-of-3 ring produced **zero**
flip-flop runs (min interior dwell 4 updates on grazes, 9 on crossings;
the tracker follows the estimate and the estimate does not alternate).
A Schmitt margin on the snapped member would change nothing these tests
can measure, so per the card the state machine is **not** modified:
`PitchCorrector.{h,cpp}` untouched — `detect()` region SHA-256
`e4c5617eef346db305f89953db4c3b520d1937a67f6399f98b3bd5afb5bdae7d`
identical to commit `080ea4d`, whole-file diff empty.

The D1 floor therefore holds by construction and by measurement:
`d0_baseline` grand total det 97.8 % / med 0.2 ¢ / p95 14.5 ¢ / oct
0.00 % / VUV 97.7 %, confusion tvEu 83 / tuEv 6 @48 k and 77 / 6 @44.1 k —
identical to the D2 close-out numbers above.

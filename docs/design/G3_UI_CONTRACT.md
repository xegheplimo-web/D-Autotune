# G3 — UI Architecture & Interaction Contract (DESIGN ONLY — no code)

**Status**: PREPARATION per GPT round-4 (D-015): "G3 UX PREPARATION GO. Hermes may design UI architecture
and interaction contracts immediately. Full Jetpack Compose implementation remains gated on real-voice
smoke-test PASS." **This document contains no implementation.** Ownership per §9: Devin implements after
the real-voice gate; Hermes reviews each card.

**Grounded in** (read before changing): `MainActivity.kt` (399 lines, programmatic framework UI, no
AndroidX — deliberate), `NativeAudio.kt` (43 lines — the complete engine contract), `G0_AUDIT.md` §streams,
DC-002A snapshot semantics, DC-003 REV2 diagnostic semantics, CI-H packaging gates.

---

## 1. Architecture decision

**Target**: single `ComponentActivity` + Jetpack Compose + one `ViewModel`, Material3, dark theme only (v1).
UI talks to the engine ONLY through the existing `NativeAudio` object — no JNI additions in G3.

**Trade-off, stated honestly**: the current code avoids AndroidX on purpose (framework-only, 1 Activity,
`Activity` not `AppCompatActivity`). Compose REQUIRES AndroidX (compose-bom, activity-compose, lifecycle,
material3) — an intentional dependency reversal, accepted because:
- Compose is the maintained UI path; the current programmatic framework UI is a prototype shell (G0 audit
  finding), not a design anyone should keep scaling.
- CI-H gates already police APK size + packaging; Compose changes are visible there.
- Runtime risk stays bounded: UI never enters the audio path (the engine callback is untouched); a UI
  defect can only cost redraws, not audio.

**Non-goals (v1)**: landscape/tablet layouts, dynamic theming, widget, background processing (the app stays
foreground-only by policy), cloud features.

## 2. Information architecture

```
onboarding overlay (first launch only, skippable)
 └─ LiveScreen (root, always the entry)
     ├─ top: safety banner strip (contextual: headphones required / permission / engine error)
     ├─ pitch panel: detected Hz · target Hz · correction cents + pitch strip-chart
     ├─ transport: big START/STOP (single primary action)
     ├─ tuning summary chip row (key · scale · amount% · speed ms · reverb%) → opens TuningSheet
     └─ toolbar: Diagnostics (opens DiagnosticsScreen)
TuningSheet (bottom sheet, adjustable while live)
DiagnosticsScreen (DC-002A telemetry + stream info + DC-003 loopback run control)
```

Rationale: the current single-scroll screen mixes monitor, tuning, safety, and a hidden debug entry.
Singing users need ONE glance (pitch + status + transport); tuning is occasional; diagnostics is rare.
Bottom sheet keeps tuning reachable while live (the engine accepts `configure()` mid-stream by design).

## 3. State machine (authoritative)

```
Idle ──(start granted: wired output ✓ + RECORD_AUDIO ✓)──► Starting
Starting ──(NativeAudio.start()==true)──► Live
Starting ──(false)──► Idle(Error[lastError])
Live ──(user stop)──► Idle
Live ──(snapshot[7] < 0.5)──► Idle(Error["engine stopped"])
Live ──(wired output removed)──► Idle(Message["tai nghe đã ngắt"])   [unchanged safety rule]
Idle(Error) ──(user retry)──► Starting
```

- **Diagnostic loopback runs ONLY from Idle** (DC-003 REV2 rule, kept): `measuring` blocks `tryStart`
  and vice versa — the state machine keeps this as a disjoint mode, not a hidden flag.
- `onStop` (user leaves app) → same as user stop: foreground-only policy unchanged.
- Screen stays on during Live and during a diagnostic run (`FLAG_KEEP_SCREEN_ON` semantics preserved).

## 4. Data contracts

**Snapshot contract (verbatim from NativeAudio.kt)**: `DoubleArray[16]`, polled at **250 ms** on the main
thread — the UI NEVER invents a faster pitch display from this data.
- Slots: `[0] detectedHz [1] targetHz [2] correctionCents [3] voiced [4] sampleRate [5] missingFrames
  [6] processedFrames [7] running [8] xruns [9] retuneMs (control param, NOT latency — label it as such)
  [10] cbMaxMs [11] cbAvgMs [12] cbP99Ms [13] xrunValid [14] missingFramesPerSec [15] inputShortReadsPerSec`.
- Display mapping: `voiced<0.5` → "—" for pitch/target/correction (never freeze the last value).
- **Pitch strip-chart**: scrolling time series of `[2]` (correction cents) and voiced-gaps, drawn from the
  same 250 ms samples (4 Hz honest cadence). An interpolated 60 fps "needle" is FORBIDDEN in v1 — it would
  fabricate pitch motion that never existed. A high-cadence UI ring buffer is listed as a **D7 engine
  candidate** and is explicitly out of G3 v1.

**Tuning contract**: `configure(root, scale, amount, retuneMs, reverb)` is callable any time (the engine
reads control params per callback); sliders apply live. Ranges stay the engine's: amount 0–100%,
speed 5–150 ms (slider step ≥5 ms), reverb 0–25%. Defaults unchanged: 90% / 35 ms / 0%.
`root` 0–11, `scale` 0=Major 1=Minor 2=Chromatic — same indices as today.

**Diagnostics contract (DC-002A + DC-003 REV2 semantics preserved verbatim)**:
- DC-002A telemetry card: stream info (rate/burst/sharing/perf both directions), missing-frames + xrun
  counts with `xrunValid` gate, input short-read rate, callback percentiles (max/avg/p99).
- DC-003 loopback run: arm only when **Idle** AND **no external audio output connected**
  (`hasExternalAudioOutput()` rule) AND RECORD_AUDIO granted; chirp peak cap ≤0.30 FS (UI passes ≤0.25),
  NCC threshold 0.6, 250 ms search window, 100 cycles — identical to today's long-press parameters.
- Fail-closed display: `state != 4` or 0 valid cycles → "INVALID_MEASUREMENT · đo không hoàn tất" (never
  render a half measurement). Mid-run device change → immediate cancel with reason (routeCallback rule).
- Labels are non-negotiable: the speaker→mic number is **CHẨN ĐOÁN loa→mic**, never "độ trễ mic→DSP→tai
  nghe"; `authoritativeLatencyMs()` = MEASUREMENT_UNAVAILABLE without the G1 fixture; the E2E latency gate
  belongs to G1 hardware only. `retuneMs` is labeled "(control)", never a latency.

**Permission contract**: RECORD_AUDIO is the only runtime permission. Denial on Live attempt → inline
banner + system dialog re-request on user action (same `onRequestPermissionsResult` flow, but from the
Compose layer). Denial on diagnostic → same, then cancel diagnostic.

**Error contract**: `lastError()` text surfaces verbatim in the safety banner with a retry action; no
error swallowing, no silent stop.

## 5. Component inventory (Compose, v1)

| Component | Role | Key behavior |
|---|---|---|
| `SafetyBanner` | contextual top strip | states: headphones-required / permission-needed / engine-error / measuring; one action max |
| `PitchPanel` | 3 metrics + strip-chart | text values from snapshot; chart is canvas, 4 Hz |
| `TransportButton` | start/stop | primary; disabled while diagnostic running (mutual exclusion) |
| `TuningChipRow` | summary + opens sheet | chip shows current values; live-updating |
| `TuningSheet` | 2 pickers + 3 sliders | applies on change via `configure()`; shows unit suffixes; a11y: contentDescription per control |
| `DiagnosticsScreen` | telemetry + loopback | read-only telemetry card; loopback card with arm/cancel button honoring §4 rules |
| `OnboardingOverlay` | first-run safety | headphones + volume guidance + mic permission primer; skippable, remembered in prefs |

Visual language: keep the current identity (navy `rgb(10,16,32)` background, mint `rgb(115,233,198)`
accent, white/muted text) as the Material3 dark color scheme seed — no redesign of the brand during G3.
Typography/spacing migrate to Material3 tokens. Localization: keep Vietnamese as default; add `values-en`
strings (currently Vietnamese-only) — new keys under `strings.xml`, no hardcoded strings in Composables.

## 6. Testing contract (G3-D testing card, post-gate)

- Robolectric/Compose-host tests for: state-machine transitions, snapshot display mapping (voiced-gap
  "—" rule, `xrunValid=0` branch), sheet ranges/clamps, banner states. Runs in CI (no device needed).
- Runtime verification (real device) stays with G1 — not blocked by G3, just unavailable until hardware.
- Pack-check: APK size delta vs current build recorded in the G3 report (CI-H reproducibility gate
  re-run required).

## 7. Gate map & sequencing

1. **NOW (Hermes)**: this document. No Kotlin/Compose changes.
2. **After real-voice PASS (D-015 gate)**: Devin implements G3 in bounded cards —
   G3-a skeleton (activity/activity-compose/material3 wiring + LiveScreen parity with today's function),
   G3-b TuningSheet + DiagnosticsScreen, G3-c strip-chart + onboarding + en strings, G3-D tests.
   Each card: CI green + functional parity checklist + no engine-file changes.
3. **G1 device work**: unaffected by G3 sequencing (BLOCKED_HARDWARE, independent).

## 8. Risks / open questions

- **APK size**: compose-bom adds ~2–4 MB; CI-H gate must re-baseline. Mitigation: R8/minify already on
  release; record before/after in G3-a.
- **250 ms pitch display** may feel sluggish for singers (they perceive ~30–50 Hz visual feedback as
  "real-time"). This is honest engineering (no fabricated motion), and D7 (engine-side UI ring) is the
  correct fix — priced and proposed separately, not smuggled into G3.
- **Bottom sheet while live**: sheet drag + slider drags run on the main thread while the UI polls at
  250 ms — verify jank in G1; if jank is real, snapshot poll moves to a side thread with immutable state
  delivery (design already tolerates this: the contract is data, not the poller).
- **`getStreamInfo()` after start** — keep the existing one-shot read + display pattern.
- **Onboarding skip persistence** needs prefs (DataStore adds another dependency — use `SharedPreferences`,
  framework-only, to stay minimal).

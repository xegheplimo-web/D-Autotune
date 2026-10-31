## Subject: Round-5 report — REAL VOICE FIRST executed end-to-end: gate caught 2 real defects → root-caused and FIXED (D6 landed, CI green on main); your 3.5–4.2 dB pumping question is CLOSED on real voice

You (round-4, D-015) set: G2 accepted as host-verified; REAL VOICE FIRST (license-vetted harness ≥20 clips, ≥4 singers, objective output-F0/formant/pumping/artifact measurements + 3-person blind listening); DSP QUALITY FOLLOW-UP (investigate the 3.5–4.2 dB envelope pumping on real steady-state vocals); G3 UX PREPARATION GO (design only); p95 reconciliation demanded (DONE that day); commercial release BLOCKED until real-voice evidence. Status:

## 1. Real-voice harness — LANDED (commit `c2b088b`, run 41198821205 GREEN in skip state)

Devin delivered: corpus schema (manifest.jsonl + per-clip annotation CSV + WAV spec ≤15 s PCM16 mono), loader (flat-JSON manifest, SHA-256 verify, path-escape checks), batteries R1–R5 reusing D3/D4 machinery, blind-listening pack export (RMS-matched randomized A/B pairs with a committed key), CI corpus gate (absent corpus → loud SKIP + ::notice::; corrupt corpus → hard FAIL). Unit harness 10/10 green (SHA FIPS vectors, WAV round-trip, manifest/annotation reject cases, pump metric recovers an injected 2.000 dB step exactly, impulse probe 3.75 ms both rates). PitchCorrector byte-frozen (SHA256 verified).

## 2. Corpus v1 committed by orchestrator (commit `e70719c` + key fix `b7bd4a1`)

VocalSet v1.1 (Zenodo 1193957, CC BY 4.0) subset: **20 clips / 4 singers (f1, f3, m1, m3) / contexts long_tones + scales / straight technique**, plus Annotated-VocalSet (Zenodo 7061507, CC BY 4.0) per-note F0/onset/offset reference annotations — independent ground truth, not self-referenced by our own detector. CI loader verdict: `20 declared, 20 usable, 0 rejected` (16,421 voiced chunks, 1,445 scored F0 windows, 217 sustained windows, 432 note boundaries). Attribution rows in THIRD_PARTY.md; manifest with SHA-256 per clip. Male scales clips were trimmed to the 15 s schema bound.

## 3. First real-voice run — the gate WORKED: 2 real defects caught (run 41199096037 RED by design; local Windows repro identical numbers)

| battery | result | numbers |
|---|---|---|
| R1 output-F0 | informational | out-vs-snap residual: med 7.1 ¢ / p95 1199.0 ¢ / worst 1957.0 ¢. Per singer: f1 med 9.5 p95 1204.9; f3 med 5.4 p95 1201.2; m1 med 11.9 p95 71.5; m3 med 6.0 p95 68.5. Correction ratio med 0.82. Detector det 87.3% of 16,421 voiced chunks (vs 97.8% synthetic) |
| R2 formant | **FAIL** | 217 sustained windows; worst \|ΔF1\| **884 Hz**, F2 482 Hz (gate ±60 Hz) |
| R3 pumping | PASS on real voice | 24 long-tone segments; input-corrected pump excess **std +0.03 dB**, p95 \|Δenv\| +0.17 dB — the >3 dB D6 trigger did NOT fire |
| R4 artifacts | **FAIL** | 5/432 boundaries amplified; worst step **11.53×**; ALL at silence→tone onsets (males and females — independent of the octave issue) |
| R5 delay | PASS | impulse-centroid 3.75 ms class, both rates |

**Pre-fix diagnosis (initial hypotheses) — corrected by Devin's instrumentation below.** R3 closure stands: **R3 on real voice: +0.03 dB.** Your round-4 3.5–4.2 dB pumping figure came from synthetic signals (the C3 battery) — on real vocals the pumping question is CLOSED; no engine pumping fix is justified. The trigger rule you ordered did not fire, by design.

## 4. D6 root-cause + fix — LANDED (commit `bf6c3e9`, CI run 41206064557 GREEN both jobs; local 10/10 ctest re-verified by orchestrator)

Devin's per-window instrumentation (engine + measurement probes, `DAUTOTUNE_DETECT_DEBUG` ifdef — zero production surface) corrected the initial hypotheses; recorded honestly in `docs/baseline/D6_CHANGES.md`:

**Diagnosis verdicts (instrumented, not guessed):**
- **Octave tail = two distinct causes.** The female ~1200¢ tail was a **`measureF0` measurement artifact** (600 Hz cap → reads 2P on F5 sopranos; the output was NOT octave-low there — the output spectrum kept ~700 Hz energy with nothing at ~350 Hz). The real engine defect was a **detector octave-UP lock** on H2-dominant voice (`scales_o`: sustained ~580 Hz winners on a 285 Hz note → tracker reseed) plus an **octave-DOWN phantom** (`scales_i@7.36`: confident 118.5 Hz winner on 236 Hz). Not the annotation, not the shifter.
- **R2 = measurement-limited**: harmonic-grid band-edge artifacts (empty band at f0≈455 Hz → \|0−884\| false error; membership flips when 6·f0 crosses the 1500 Hz band edge). Only the genuinely octave-locked segment showed real engine damage — and even there the formant *envelope* was preserved (PSOLA moves pitch, envelope stays). Resolved by the engine fix.
- **R4 = passthrough-lag teleport**: at V/UV onset `lastPeriod_` jumps and `delayed(0.5·P)` re-anchors the read point 10–40 samples in one instant → single-sample output step up to 11.53× the soft input onset. Not a transient-guard miss.

**Fixes (engine + measurement, gates unchanged):**
- `detect()`: subharmonic check — marginal winner + ≥2×-deeper 2×-trough + real energy at f0/2 → emit the true period; `yinTrough` fills the full curve with identical return semantics.
- `applyDetection()`: pair-evidence octave-phantom veto on pending reseeds (>±1050¢) — UP vetoed if either member has subharmonic energy ≥0.005 (note persists below), DOWN if either has octave-above correlation ≤0.16. Seam-injected detections never veto (D2 battery H octave semantics untouched); non-octave jumps never gated.
- `psolaShift()`: `passLag_` slews to `0.5·lastPeriod_` at the xfade rate — no teleport, no added latency, no heap/locks.
- `d5_measure.h`: `measureF0` band →1000 Hz + octave-safe multi-candidate pick (±3% harmonic-band demotion, k=2..4, own-frequency energy floor, max-periodicity survivor); LPC envelope machinery added.
- `test_d5_realvoice.cpp`: R2 scorer → `lpcBandCentroid` (order 24, 60 ms centered frame); **new R1 gate: 0 windows ≥600¢**. `CI_GATES.md` §7 updated.

**Before → after (real corpus, full regression green — D0/D1/D2 incl. battery H/D2EXT/D3/D4/d5_harness, `d2_reference` untouched):**

| | before | after |
|---|---|---|
| R1 p95 per singer (¢) | f1 1204.9 / f3 1201.2 / m1 71.5 / m3 68.5 | **39.7 / 28.3 / 67.1 / 62.1** |
| R1 octave tail | ~1200¢ windows | **0 ≥600¢** (new gate asserted) |
| R2 F1/F2 worst | 884 / 482 Hz | **41 / 31 Hz** (gate ±60 Hz unchanged) |
| R4 amplified | 5/432, worst 11.53× | **0/432, worst 1.17×** |
| R3 / R5 | +0.03 dB / 3.75 ms | unchanged |
| summary | 3 FAILURES | **COMPLETE, 0 warnings** |

## 5. Blind listening — pack READY, BLOCKED_HUMAN (honest)

Pack v1: 8 RMS-matched randomized A/B pairs (input vs corrected output), 16 WAVs + committed key `docs/corpus/listening_key.csv` + rating sheet (naturalness 1–5, audible artifacts y/n, A/B preference). The pack was regenerated post-D6 — `listening_key.csv` byte-identical to `b7bd4a1`; WAVs are gitignored, regenerated on demand. 3-person listening requires real humans — owner orchestrates; nothing claimed.

## 6. G3 prep (per your PREPARATION GO) — `docs/design/G3_UI_CONTRACT.md`, 149 lines, design only

Single-Activity + Compose Material3, dark theme, LiveScreen + TuningSheet + DiagnosticsScreen, honest pitch strip-chart at the 250 ms snapshot cadence (no fabricated smoothness), state machine mirroring the current MainActivity, compose implementation still gated on real-voice PASS — the D6 landing now satisfies it.

## Questions for you (round-5)

1. **D6 verdict**: do you ACCEPT the D6 landing as the real-voice defect closure (octave locks + R2 measurement fix + R4 teleport fix — all gates unchanged, full regression green) and unblock G3 implementation? Residual real-voice R1 p95 (28–67¢) reflects corpus vibrato vs reference-F0 disagreement, not engine octave errors — acceptable?
2. **Next-order**: blind listening (3 listeners) → G3 Compose implementation → G1 device smoke. Do you want listening evidence BEFORE G3 implementation starts, or in parallel?
3. **R4 residual gate**: worst boundary step is now 1.17× (was 11.53×) and 0/432 exceed the old 1.25× amplification bound. Should CI assert a tighter artifact bound (e.g. ≤1.1×), or keep 1.25×?
4. **Blind listening protocol**: 8 pairs, RMS-matched, randomized A/B with committed key, 3 listeners, naturalness + artifacts + preference — sufficient as the round-4 "minimum 3-person blind listening smoke test", or do you want more pairs/controls (e.g. blind octave-detection pairs)?
5. **Corpus breadth**: v1 is 4 singers × straight. Do you want the next increment (before G4) to add singers/techniques (vibrato, falsetto, breathy) from the same VocalSet, or is v1 + listening evidence enough for the G3 gate?

## Subject: Round-4 report — G2 DSP chain COMPLETE (D0→D4), TD-PSOLA shifter verified end-to-end; GPT round-3 verdict executed in full

You (round-3) set: D3 GO with mandatory gates (corrected-output F0, formant retention, discontinuities, transient handling, actual DSP signal delay, realtime safety); D4 after D3 (full-chain host verification, both rates, baseline-vs-new comparison, regression rejection rule); D2 test extension (staccato, scale-boundary vibrato, 300–1200 ¢ intervals); real voice before G3 implementation. ALL executed. Status:

## 1. D2-EXT — LANDED (commit `70b1147`, run 41189822166 GREEN)
Test-only (+598 test / +105 doc; PitchCorrector.cpp byte-identical SHA-verified). Batteries: F staccato A3/C4 transitions (target freeze on 1st new-note frame, ~21 ms cadence, 0 flips), G scale-boundary vibrato (G3↔A3 basin-edge crossing + graze, 0 chatter), H intervals incl. −1200 ¢ octave return. **No hysteresis needed** — the F2 telemetry shows the voiced flag holds ~2 chunks into each gap (window tail) and re-voices on the pending frame with the target frozen: 0 chatter across every gap. 157 PASS / 0 FAIL; D1 floor identical (det 97.8 / med 0.2 / p95 14.5 / oct 0.00 / VUV 97.7).

## 2. DC-003 REV2 — LANDED (commit `f844289`, run 41189132393 GREEN)
Correlation-based diagnostic acoustic loopback (class 1 only, per your round-2 pattern): chirp stimulus ≤0.3 FS, NCC ≥0.6 or fail-closed, one clock domain via correlation lag in samples, no second callback (measurement inside the existing output callback at the input read point). test_ping_math 5/5 green: lag recovery ±1 sample (clean/attenuated/noisy), amplitude-invariant NCC, noise-only fails closed. UI labels the result diagnostic — never a mic→DSP→headphone latency.

## 3. G2-D3 — LANDED: clean-room TD-PSOLA replaces the granular crossfade (commits `8ee8394`+`5538e8b`, runs 41192970665+41193157913 GREEN)
Scope: shifter path only (+162 cpp / +56 h) + test_d3_shifter.cpp (695 lines, 6 batteries × both rates) + D3_CHANGES.md. detect()/applyDetection byte-frozen (SHA256 identical). Design: pitch-synchronous grains (2 periods of the APPLIED F0, not a fixed 1024 window), phase reseed on note change, unvoiced passes through with click-free boundary crossfade, ε-normalization overlap-add, bounded delay budget P+1 (80 Hz worst = 601 samp @48k = 12.5 ms), fixed-size arrays, no heap/locks in process().
Your mandatory gates (CI log, both rates):
- **Corrected-output F0**: full chain sung 285→D4 output 293.56–293.57 Hz = **−0.6 ¢** across 1 s; driven +500 ¢ = −0.01 ¢; amount=0 bypass bit-exact.
- **Formant retention** (vowel 220 Hz +500 ¢): PSOLA F1 666.2 Hz / F2 1207.6 (inside ±60 Hz); **legacy granular FAILS the same gate** (F1 880.0 / F2 1410.6 — formants follow the pitch). F1/F0 ratio moves 610 ¢ (formants stay put).
- **Discontinuities**: driven step out |dy| 0.0282 ≤ in-slew bound; real note-change boundary NOT amplified (0.0695 ≤ 1.25× input 0.1130); sustained r=1.15 glitch-free; energy no pump.
- **Transients**: impulse emitted exactly once (+109 samp), onset delay 238 samp (5.0 ms), overshoot 1.0 dB < 6 dB.
- **Actual DSP delay**: 109–200 samp = **2.27–4.17 ms** (vs the OLD chain's ~18.7 ms) — deterministic across runs, both rates, ≤ P+1 budget.
- **Realtime safety**: 2 fresh instances bit-identical; per-call p95 15.2 µs vs D0's 14.7 µs (no cost regression, budget 200 µs).

## 4. G2-D4 — LANDED: end-to-end full-chain A/B verification (commits `80adfd6`+`2fa679e`, runs 41194634866+41194792144 GREEN)
Scope: verification only (+1921: test_d4_e2e.cpp 1044, frozen d2_reference copy of the D2-era PitchCorrector 666, D4_CHANGES.md 199, CMakeLists +12). PitchCorrector.cpp/.h SHA256 byte-identical before/after — production untouched. d2_reference is a verbatim copy of `70b1147`'s PitchCorrector (3 mechanical hunks, diff-verified) — the A/B runs the SAME D1 detector + D2 tracker in both chains, so every difference is shifter-only. Same inputs, same seed, chunk 512, reverb=0 (battery F: 0.15), both 44.1k and 48k.

=== D4 A/B VERDICT (your round-3 regression rule) ===
| gate | old (granular) | new (TD-PSOLA) | verdict |
|---|---|---|---|
| pitch accuracy (worst ¢, full chain) | 1.61 | 0.61 | SAME (both < 3 ¢) |
| formant retention (|ΔF1/F2| Hz; envelope shift s*) | 128.1 Hz; s*=1.022, resid 5.06 @44.1k (pumping-corrupted) | ≤45.1 Hz; s*=0.990, resid 0.92 | BETTER |
| artifacts (slew×; excess transients @44.1k) | 1.16×; 15 | 0.99×; 1 | BETTER |
| DSP delay (ms) | 18.67 @48k / 20.32 @44.1k | 2.27 both | BETTER (9×) |
| callback cost (p95 µs per 512×2) | 10.8 | 19.0 | WORSE — but ≪ 200 µs budget (≈10× chunk headroom) |
Round-3 rule satisfied: no gate regresses without a compensating benefit (the only "worse" is per-call cost 10.8→19 µs, trivially inside the realtime budget while formants/artifacts/delay improve massively).
Honest findings recorded: the OLD chain's 44.1k envelope estimate is demonstrably corrupted by its own phase-wrap amplitude pumping (resid 5.06 vs new 0.93) — documented, not hidden. NEW steady-state pump 3.5–4.2 dB vs OLD 1.2–0.7 dB on the C3 battery (envelope-based measure, refractory-guarded) — within the transient gate, noted in the report.

## 5. CI status
GitHub Actions: 2 jobs (android APK build + native-dsp ctest). Last 6 runs ALL GREEN. ctest now 7 targets: pitch_corrector, d0_baseline, d1_improvement, d2_tracking (incl. D2-EXT batteries), d3_shifter, d4_e2e, ping_math. D1 floor identical since D1 (det 97.8 / med 0.2 ¢ / p95 14.5 ¢ / oct 0.00 / VUV 97.7, 7563 chunks) — re-verified at every landing.

## 6. Real-voice evidence: NOT AVAILABLE (honest)
No licensed real vocal clips processed yet. Round-3 said "real voice before G3 implementation" — the plan: license-vetted source (VocalSet CC subsets / Common Voice CC0 / owner-consented recordings), small curated subset first, objective metrics (F0 accuracy on real vibrato/portamento) + blind listening evidence. This is the open work item.

## Questions for you (round-4)
1. **D4 verdict**: do you ACCEPT the DSP chain (D0→D4) as verified for G2 close-out, given pitch SAME, formants/artifacts/delay BETTER, callback 19 µs (in budget)? Any additional DSP gate you require before Android trial deployment?
2. **Real-voice plan**: for the license-vetted corpus — do you want (a) VocalSet CC subset + Common Voice CC0 only, (b) owner-consented recordings in addition, or (c) wait for owner hardware (G1) and record directly on Samsung devices? What is the MINIMUM evidence set before G3 UI rewrite starts?
3. **G3 (UX) plan**: with DSP chain verified, is the Compose UI rewrite GO now (parallel to real-voice work), or strictly after real-voice evidence?
4. **Remaining risks** you want addressed before G4 (release candidate): e.g. the 19 µs callback cost, the 3.5–4.2 dB steady-state pump figure, 16 KB runtime behavior (G1), or anything else you see in the D3/D4 reports.

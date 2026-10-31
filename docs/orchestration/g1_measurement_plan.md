# G1 Measurement Plan — Real-device latency & xrun qualification (2026-10-31, REV 2 per GPT round-2)

Owner-facing plan. References: GPT round-1 verdict (docs/orchestration/gpt_g0_consultation_reply.md), GPT round-2 verdict (docs/orchestration/gpt_round2_reply.md §3 — REVISE), G0 audit items #14/#26, directive §6.

## Status
- BLOCKED_HARDWARE: no Android device on this host. Owner must supply Samsung A-series + S-series + wired headset (GPT directive §6).
- DC-002 landed (db65a0f) but GPT round-2 REJECTED its algoLatencyMs semantics — DC-002A (measurement semantics + telemetry) must land BEFORE any G1 evidence is collected. DC-003 is revised per GPT's approved design (correlation-based, two measurement classes) — card `dc-003-loopback-ping-REV2.md`.
- This plan is REV 2: the old plan's single "loopback ping" method and the 12/20 ms medians as release gates were corrected by GPT round-2.

## Method (per GPT round-2 approved design — TWO measurement classes, never conflated)

### Class 1 — Diagnostic acoustic loopback (phone speaker → phone mic)
- Purpose: partial audio I/O path check, anomaly detection, jitter analysis. Partially exercises the I/O chain on real hardware.
- Stimulus: low-amplitude identifiable signal (band-limited pulse train / chirp, peak ≤ 0.3 FS), repeated N=100.
- Detection: normalized cross-correlation against the reference stimulus; NCC ≥ threshold (≈0.6) else INVALID_MEASUREMENT (fail-closed).
- Result: median/p95 round-trip of the SPEAKER→MIC path + per-cycle NCC peaks. NOT a mic→DSP→headphone latency claim.
- Constraints: timeout per cycle, amplitude cap, routing control (wired headset present → acoustic path invalid → INVALID_MEASUREMENT with reason), measurement mode never runs during a singing session (dedicated debug entry only).

### Class 2 — Authoritative end-to-end latency (the ONLY source for the Live gate)
- Requires a common reference clock: calibrated hardware loopback fixture (analog loopback + calibrated capture, or a second device with shared clock) comparing mic input to headphone output.
- If the fixture is absent, the app reports MEASUREMENT_UNAVAILABLE (fail-closed) — the acoustic loopback result is NEVER relabeled as authoritative.
- This class produces the G1 gate evidence on the owner's devices.

## Acceptance thresholds (REV 2 — GPT round-2 correction)
- **Release gate (Live DSP)**: p95 total round-trip ≤ 50 ms on the selected Samsung S-series and ≤ 70 ms on the selected A-series, PLUS a perceptual check while singing. Approved only after real-hardware measurement (Class 2).
- **Research targets (NOT release gates)**: median < 12 ms (S-series) / < 20 ms (A-series) for suitable audio paths — stretch goals only; failing to reach them does not block release.
- xruns: 0 during a 10-minute sustained session at 48 kHz; xrun=0 must be distinguished from "xrun counter unsupported" (DC-002A).
- Callback duration: max < 50% of the buffer period; p99/histogram telemetry (DC-002A) — deadline utilization reported.
- Thermal: no xrun inflation after 10 min.
- Latency budget: DSP latency (measured by reference-signal method — DC-003 revised) + device round-trip documented; the 40 ms "hard tune" product budget is a design goal pending the same evidence.

## Sequencing (updated per GPT round-2)
1. DC-002A (measurement semantics + telemetry + granted stream properties) — Devin, before any G1 evidence.
2. DC-003 REV2 (correlation-based measurement mode, class 1 implemented in-app; class 2 = hardware fixture, owner-supplied) — Devin, after DC-002A.
3. G2 host development (D0→D4) proceeds in parallel — GO per GPT round-2; no device needed.
4. Owner supplies devices → G1 test session: Class 1 diagnostics first, then Class 2 with the fixture; results recorded with numbers.
5. G1 verdict: release gate = p95 ≤ 50 ms (S) / ≤ 70 ms (A) + perceptual check; research targets reported separately.

## Known unknowns (recorded, not fabricated)
- Whether the mic path accepts the output's sample rate on real Samsung devices (fails closed today with a clear error).
- Actual xrun behavior, thermal, and latency on real devices.
- Whether Exclusive mode (MMMAP/AAudio) is available on the test devices (fallback ladder: Exclusive → LowLatency → Shared). GPT round-2: the current code requests Shared mode on both streams — record ACTUAL granted stream properties (DC-002A) before comparing against Exclusive targets.
- Calibrated hardware loopback fixture availability (Class 2) — owner-supplied; without it, only Class 1 diagnostics are possible.

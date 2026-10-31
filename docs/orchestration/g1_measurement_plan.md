# G1 Measurement Plan — Real-device latency & xrun qualification (2026-10-31)

Owner-facing plan. Reference: GPT round-1 verdict (docs/orchestration/gpt_g0_consultation_reply.md), G0 audit item #14/#26, directive §6.

## Status
- BLOCKED_HARDWARE: no Android device on this host. Owner must supply Samsung A-series + S-series + wired headset (GPT directive §6).
- DC-002 (in flight) supplies the instrumentation: xruns, algoLatencyMs, callback max/avg. WITHOUT it G1 is unexecutable even with hardware.

## Method (app-internal loopback ping — the standard, no external hardware)
1. App writes a click into the output stream (single-sample impulse or short burst).
2. The mic picks up the click through the speaker (or wired headset mic picks up nothing — use the phone speaker + built-in mic; headset-mic routing is a separate test since directive §3 requires built-in mic preference).
3. The input stream records; the DSP detects the click's arrival (amplitude threshold + correlation).
4. Latency = (input frame position of click arrival) − (output frame position of click write) − (speaker + mic physical path, measured separately as reference).
5. Repeat N=100, report median/p95/max (means matter more than the single number).

## Acceptance thresholds (GPT-aligned, editable by owner)
- Round-trip latency (mic loopback): < 20 ms median on Samsung A-series (mid), < 12 ms on S-series (flagship) with Exclusive mode.
- xruns: 0 during a 10-minute sustained session at 48 kHz.
- Callback duration: max < 50% of the buffer period (no scheduler starvation).
- Thermal: no xrun inflation after 10 min (throttling detection).
- Algo latency (DC-002 counter): documented; the product latency budget = algo latency + device round-trip must stay under 40 ms for "hard tune" mode.

## Sequencing (after DC-002 lands)
1. DC-003 card: "loopback ping mode" — debug-only UI toggle (amount=0 + click generator + arrival detector). This is the G1 tool. It is a NEW feature (Devin, ~25 min).
2. Owner supplies devices → G1 test session (Hermes documents the results with the numbers).
3. G2 (DSP chain) proceeds in parallel with G1 — GPT round-1 verdict allows G2 work without waiting for G1.

## Known unknowns (recorded, not fabricated)
- Whether the mic path accepts the output's sample rate on real Samsung devices (fails closed today with a clear error).
- Actual xrun behavior, thermal, and latency on real devices.
- Whether Exclusive mode (MMMAP/AAudio) is available on the test devices (fallback ladder: Exclusive → LowLatency → Shared, per GPT round-1 verdict).

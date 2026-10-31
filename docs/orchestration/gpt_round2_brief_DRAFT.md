# GPT Round-2 Brief — D-Autotune (DRAFT — fill numbers when DC-002 lands)

## Status since round-1 (verdict: GO — G0 + CI parallel, Compose at G3, API 30/36 + 16 KB gate)
- **CI GREEN** (first full pass in project history): run 41178223381 @ 848693c — both jobs success, artifact D-Autotune-debug-APK 1.93 MB.
- 5-run fix cascade documented: license-prompt starvation (setup-android dropped, bounded yes|timeout) → wrapper-JAR 404 (services.gradle.org → raw.githubusercontent, checksum unchanged) → Prefab CXX1212 (ANDROID_STL=c++_shared) → Kotlin interop (isFillViewport property trap) → GREEN. Every fix verified by the NEXT run getting further — no fix claimed "fixed" without its follow-on run.
- **APK-level verification (hard evidence)**: 16 KB PT_LOAD alignment PASS (all 6 .so, both ABIs, incl. libc++_shared + liboboe); exactly 1 permission (RECORD_AUDIO) in binary AXML; minSdk=30/targetSdk=36 verified in structured AXML parse.
- **G0 audit complete**: 27-area evidence-backed baseline (`.orchestrator/G0_AUDIT.md`) — items #2/#4/#12/#22 upgraded to VERIFIED; critical-path gap list updated (item #1 resolved).
- **DC-002 (in flight)**: latency/xrun instrumentation — snapshot 8→12 [xruns, algoLatencyMs, cbMaxMs, cbAvgMs], callback remains allocation/lock/log-free. RESULT: <fill when landed>.
- **DC-003 (drafted)**: loopback ping mode — G1 measurement tool per g1_measurement_plan.md.
- **G1 measurement plan written** (docs/orchestration/g1_measurement_plan.md): loopback ping protocol, N=100 median/p95, thresholds (A-series <20 ms, S-series <20 ms, Exclusive <20 ms), N=100 median/p95... BLOCKED_HARDWARE (owner supplies Samsung devices).

## Questions for GPT (round-2)
1. Verify our fix cascade + APK verification logic (any gate we missed at APK level?).
2. Review DC-002's snapshot layout + algoLatency formula (once landed): is the measurement layer sufficient for G1?
3. DC-003 loopback ping design (draft attached): sound? pitfalls (threshold false-positives, speaker saturation)?
4. Priority check: after DC-002+DC-003, proceed to G2 DSP chain (adaptive YIN → tracking → TD-PSOLA) in parallel with waiting for hardware, per round-1 verdict?

## Constraints unchanged
- Internal name D-Autotune; release branding = owner decision (trademark evidence: Antares AUTOTUNE word mark serial 99040050 NOTICE OF ALLOWANCE 2026-05-12, deadline 2026-11-12, class 9 software — documented in docs/THIRD_PARTY.md).
- No Android device on host — hardware BLOCKED; owner must supply Samsung A-series + S-series + wired headset.

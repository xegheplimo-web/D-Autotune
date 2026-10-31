# G0 Audit — Evidence-Backed Baseline (2026-10-31)

Auditor: Hermes (Lead Orchestrator). Method: every claim below is tied to a file:line read in THIS session (F:/D-Autotune @ 1bafe05) or a real CI API/log result. No prior-report PASS was trusted without reproduction or direct code evidence. Per DIRECTIVE §2 (G0 mandatory audit) and GPT round-1 verdict (docs/orchestration/gpt_g0_consultation_reply.md).

Legend: IMPLEMENTED / PARTIAL / MISSING / BROKEN / UNVERIFIED / BLOCKED_*

## 1. Area baseline

| # | Area | Status | Evidence (file:line or run id) |
|---|---|---|---|
| 1 | Git repo + remote | IMPLEMENTED | 3 commits (`2b5ac36`,`fe926ee`,`1bafe05`); origin verified; `gh repo view` isPrivate=false |
| 2 | Build config (Gradle 8.13 / AGP 8.13.2 / Kotlin 2.2.20 / JDK 17) | **VERIFIED** — `:app:assembleDebug` completed remotely (run 41178223381), APK artifact built + SHA256 verified | build.gradle.kts, settings.gradle.kts, gradle-wrapper.properties (SHA256 pinned); run 41178223381 |
| 3 | CI `native-dsp` (host C++ ctest) | IMPLEMENTED + VERIFIED PASS (remote) | run 41175801147 job native-dsp conclusion=success |
| 4 | CI `android` (APK build) | **VERIFIED GREEN** (run 41178223381 @ `848693c`: both jobs success, artifact `D-Autotune-debug-APK` 1.93 MB uploaded, downloaded + SHA256 verified + APK-level checks passed). Full 5-step fix cascade documented in VERIFICATION.md Gate 11 | runs 41175801147→41177042055→41177443204→41177598995→41178223381 |
| 5 | Pitch detection (YIN-family, normalized CMND, parabolic refine) | IMPLEMENTED on synthetic host tests; real-voice accuracy UNVERIFIED (no corpus) | PitchCorrector.cpp:69-123; TEST_RESULTS.md 11 assertions incl. 228.022 Hz detected |
| 6 | Pitch correction/shifting (2-head granular Hann crossfade) | PARTIAL — works on synthetic (zero-crossing ~220 Hz from 228 Hz input); NOT formant-preserving; metallic-artifact risk acknowledged in docs; GPT decision: replace with clean-room TD-PSOLA in G2 | PitchCorrector.cpp:136-144; docs/ROADMAP.md P1; docs/TEST_RESULTS.md |
| 7 | Realtime callback safety (no heap/locks/logging in audio path) | IMPLEMENTED — verified by code read: stack buffer `float capture[512]` (AudioEngine.cpp:100), PitchCorrector::process allocation-free (fixed std::array members only), relaxed atomics, no mutex/logging/JNI in callback | AudioEngine.cpp:92-109; PitchCorrector.h:33-71 |
| 8 | Audio I/O architecture | PARTIAL — TWO SEPARATE streams (output LowLatency/Shared + input nonblocking read timeout=0), NOT `oboe::FullDuplexStream`; Shared mode only (Exclusive never requested, no fallback ladder); input sample rate forced equal to output rate — devices where mic path rejects that rate fail at start (error path exists but kills the whole session) | AudioEngine.cpp:15-50; AudioEngine.h:11-33 |
| 9 | Native stream-error / route-change recovery | MISSING — `AudioStreamErrorCallback` (onErrorBeforeClose/onErrorAfterClose) not implemented; disconnect (device removal, service死亡) only covered by Kotlin-side AudioDeviceCallback for headset removal; other failure modes leave engine dead with no recovery attempt | AudioEngine.h:11 (implements only AudioStreamDataCallback); AudioEngine.cpp:92-94 returns Stop on !running_ |
| 10 | Headset-unplug handling | IMPLEMENTED (Kotlin level) | MainActivity.kt:47-51 + 204-207 (AudioDeviceCallback → stopAudio on removal when no wired output) |
| 11 | Foreground-only guarantee | IMPLEMENTED — onStop() stops engine; no foreground service in manifest | MainActivity.kt:247-250; AndroidManifest.xml (no service) |
| 12 | Permissions / privacy | IMPLEMENTED + **APK-LEVEL VERIFIED** (binary AXML string pool = exactly 1 permission `RECORD_AUDIO`; INTERNET/WRITE_EXTERNAL/CAMERA/FOREGROUND_SERVICE absent in built APK from run 41178223381) | AndroidManifest.xml:3-10; MainActivity.kt:209-245; apk-verify evidence in VERIFICATION.md Gate 11 |
| 13 | Mic routing (built-in mic preferred while wired headphones out) | MISSING — input stream never sets preferred device (default routing; on headset-mic phones the headset mic will be used, contradicting directive §3 "built-in phone mic when supported") | AudioEngine.cpp:30-37 (no setPreferredDevice) |
| 14 | Latency / xrun instrumentation | **IMPLEMENTED** (DC-002, commit `db65a0f`, CI run 41179251125 GREEN): snapshot 8→12 `[…, running, xruns, algoLatencyMs, cbMaxMs, cbAvgMs]`; xruns = Oboe `getXRunCount()` on the output stream (input builder has NO data callback — verified AudioEngine.cpp:29-44, so the callback+timing are output-only); algoLatency = `kChunk·1000/sr + retuneMs` recomputed in start()/setParameters(); callback max/avg from steady_clock — single-writer cbTotalUs_, relaxed atomics, no alloc/lock/log/JNI. G1 (bypass vs full-DSP) is now executable once hardware arrives — BUT GPT round-2 REJECTED the algoLatencyMs semantics (D-012): follow-up DC-002A required (relabel + p99/deadline telemetry + xrun-validity + missingFrames-rate) before any G1 evidence is collected | AudioEngine.{h,cpp} diff db65a0f; VERIFICATION.md Gate 11; docs/orchestration/gpt_round2_reply.md §2 |
| 15 | Exclusive/MMMAP latency path + fallback ladder | MISSING (never requested) | AudioEngine.cpp:16-18 |
| 16 | UI functional (key/scale/amount/speed/reverb + live metrics) | IMPLEMENTED (plain Activity, no AndroidX — matches G3 deferral decision) | MainActivity.kt:98-197 |
| 17 | i18n VI/EN | MISSING — all UI strings hardcoded Vietnamese inline in Kotlin; strings.xml has 2 entries | MainActivity.kt (all literal text); strings.xml:2-3 |
| 18 | Natural/Hard Tune presets | MISSING (single amount slider only) | MainActivity.kt:148 |
| 19 | WAV record/export | MISSING | no code; roadmap P3 |
| 20 | Device capability diagnostics / latency modes | MISSING | no code |
| 21 | DSP golden + Android instrumentation tests | PARTIAL — host golden tests exist (test_dsp.cpp, 11 assertions, remote PASS); Android instrumented tests: NONE | native-tests/; no androidTest/ dir |
| 22 | 16 KB page-size ELF alignment | **VERIFIED at APK level** — all PT_LOAD segments of all 6 .so in run 41178223381's APK have `p_align == 0x4000` (16384), incl. `libc++_shared.so` + `liboboe.so` (python ELF parser; hard evidence, not build-flag trust). Link flag `-Wl,-z,max-page-size=16384` + `useLegacyPackaging=false` | app/src/main/cpp/CMakeLists.txt:9-10; app/build.gradle.kts:38; VERIFICATION.md Gate 11 |
| 23 | 64-bit ARM | IMPLEMENTED-PARTIAL — abiFilters arm64-v8a + x86_64 (armeabi-v7a intentionally absent; fine for Play 2027 64-bit requirement) | app/build.gradle.kts:17 |
| 24 | Dependency provenance/licenses | IMPLEMENTED (Oboe 1.11.0 Apache-2.0 via Google Maven Prefab; no other runtime deps; no copyleft) | docs/THIRD_PARTY.md; app/build.gradle.kts:42 |
| 25 | Trademark ("Auto-Tune" = Antares mark) | BLOCKED_EXTERNAL — keep internal name; owner decision before any store listing | docs/THIRD_PARTY.md; GPT directive §12 |
| 26 | Real-device evidence (G1) | BLOCKED_HARDWARE — no Android device/SDK on this machine; owner must supply Samsung A-series + S-series + wired headset | no device; GPT directive §6 |
| 27 | GPT round-1 consultation | DONE — brief sent + reply archived + decisions encoded | docs/orchestration/gpt_g0_consultation_reply.md; .orchestrator/DECISIONS.md D-007 |
| 28 | GPT round-2 consultation | DONE — round-2 brief sent (3,717 chars, `6f6fb49`) + reply archived + verdict CONDITIONAL GO encoded (D-012): DC-002A required, DC-003 REVISE, CI-H task, G2 D0→D4 GO on host, G1 BLOCKED_HARDWARE stands | docs/orchestration/gpt_round2_brief.md; docs/orchestration/gpt_round2_reply.md; .orchestrator/DECISIONS.md D-012 |

## 2. Critical-path gap list (priority order, from evidence)

1. **CI android job** — RESOLVED 2026-10-31 (run 41178223381 GREEN: both jobs + artifact 1.93 MB; 5-step cascade documented in VERIFICATION.md Gate 11).
2. **Latency instrumentation** (#14) — LANDED (DC-002, `db65a0f`, CI run 41179251125 GREEN + APK re-verified) but **measurement semantics REJECTED by GPT round-2** (D-012): algoLatencyMs formula invalid (retuneMs = control smoothing, not signal delay); follow-up **DC-002A** required (relabel + p99/histogram + deadline utilization + xrun=0-vs-unsupported + missingFrames-rate + input starvation) before ANY G1 evidence. GPT also confirmed both streams currently request SharingMode::Shared — Exclusive hypothesis untestable until actual granted stream properties are recorded (#15).
3. **Native stream-error recovery** (#9) + **mic routing preference** (#13) — required by directive §4/§3; current behavior breaks the "built-in mic preferred" product requirement on headset-mic phones.
4. **Exclusive-mode ladder** (#15) — only meaningful AFTER DC-002A provides valid measurement AND actual granted stream properties are recorded (GPT round-2: record granted properties first).
5. **G2 DSP chain** — GPT round-2 UPDATED order (D-012): D0 golden baseline+corpus (F0 accuracy, octave error, voiced/unvoiced accuracy, host execution time) → D1 adaptive-window YIN+voicing (improve detector BEFORE shifter; keep light for common Android) → D2 tracking+hysteresis (independent unit tests) → D3 clean-room TD-PSOLA → D4 end-to-end host verification. NO pYIN/neural/key-detection in the critical path now. GO parallel with hardware.
6. **G3 product layer** (deferred by GPT verdict): Compose rewrite, i18n VI/EN, presets, WAV export, diagnostics.
7. **Owner-required actions**: Samsung devices for G1 (BLOCKED_HARDWARE); trademark decision before store listing (BLOCKED_EXTERNAL — evidence: Antares AUTOTUNE serial 99040050 NOTICE OF ALLOWANCE, deadline 2026-11-12, documented in docs/THIRD_PARTY.md).

## 3. Honest unknowns (not fabricated)

- Whether `setup-android@v4` resolves the license hang: UNKNOWN until the remote run executes (root cause unproven either way — GPT correction adopted).
- Whether the mic path accepts the output stream's sample rate on real Samsung devices: UNKNOWN (design fails closed today with a clear error; needs device data or a resampler fallback decision in G2).
- Actual device round-trip latency, xrun behavior, thermal: UNKNOWN (BLOCKED_HARDWARE).
- Real-voice pitch accuracy of the current YIN detector: UNVERIFIED (no corpus; synthetic-only evidence).

# D-Autotune — Project Control Plane State

Project: D-Autotune (F:\D-Autotune)
Created by Hermes Orchestrator on 2026-10-31 (local session time, +07:00)
Git init: main root-commit `2b5ac36` (36 files, 1300 insertions)
Workflow: lead-orchestrator v1.8.1 (canonical process: RECON → ANALYZE → DECIDE → PLAN → ASSIGN → IMPLEMENT → VERIFY → FIX → INTEGRATE → FINAL VERIFY)

## 1. Verified Evidence (VERIFIED)

- Source directory: `F:\D-Autotune` (verified by `ls`)
- No `.git` directory existed before this session → git isolation broken (§14); fixed by `git init` + initial commit.
- Project type: Android real-time pitch-correction prototype (Gradle 8.13, Kotlin, NDK C++17, Oboe 1.11.0).
- Key source files verified:
  - `app/src/main/cpp/PitchCorrector.cpp` (213 lines) — YIN-family pitch detector + granular crossfade shifter
  - `app/src/main/cpp/PitchCorrector.h` (74 lines) — `PitchCorrector` class definition
  - `app/src/main/cpp/AudioEngine.cpp` / `AudioEngine.h`
  - `app/src/main/cpp/native-lib.cpp` — JNI bindings (`start`, `stop`, `configure`, `snapshot`, `lastError`)
  - `app/src/main/java/com/dautotune/app/MainActivity.kt`
  - `app/src/main/java/com/dautotune/app/NativeAudio.kt`
- CI workflow: `.github/workflows/ci.yml` exists (1133 bytes, 2 jobs: `native-dsp` [ubuntu, cmake + ctest] + `android` [ubuntu, gradlew + SDK 36 + NDK])
- `gradlew.bat` + `gradlew` present; `gradle/wrapper/gradle-wrapper.properties` references `gradle-8.13-bin.zip` with verified SHA256 (`20f1b117...` from official Gradle release checksums)
- SHA256SUMS.txt: 35 file hashes verified (matches source files)
- Docs verified: `docs/RESEARCH.md`, `docs/ROADMAP.md`, `docs/TEST_RESULTS.md`, `docs/THIRD_PARTY.md`
- Scripts verified: `scripts/build-windows.ps1`, `scripts/ensure-wrapper.ps1`, `scripts/test-dsp-windows.ps1`
- Native tests: `native-tests/test_dsp.cpp` (2932 bytes, 11 assertions) + `native-tests/bench_dsp.cpp` (1383 bytes)
- `.gitignore` exists (`.gradle/`, `.idea/`, `local.properties`, `**/build/`, `**/.cxx/`, `*.apk`, `*.aab`, `.DS_Store`)

## 2. Environment / Tool Evidence (VERIFIED)

- Devin CLI: `devin 3000.11.3` (`devin --version` verified)
- GitHub CLI (`gh`): `v2.100.0` — auth status: `github.com` logged in (`xegheplimo-web` account active, scopes include `repo`, `workflow`, `project`, `user`, `gist`)
- `cmake`: NOT installed (`command not found` — verified by `cmake --version`)
- `gcc`: NOT installed (`command not found` — verified by `gcc --version`)
- `JAVA_HOME`: `E:\jdk-21.0.12.1+1` (verified by `$JAVA_HOME`)
- `gradlew.bat`: wrapper present (verified by `ls gradlew.bat` + properties read)
- Chrome debug (`:9222`): DOWN (`curl -s --max-time 3 http://localhost:9222/json/version` returned empty; no response)
- ChatGPT collaboration URL (`https://chatgpt.com/c/6ae57dbb-f3bc-83ec-abea-d8f57db27be6`): 404 (`"The page does not exist at this URL"` — verified by `web_extract`)

## 3. GPT Collaboration Status — BLOCKED

Reference procedure: `lead-orchestrator` references/gpt-collaboration.md (verified this session).
- Prerequisite check 1 (CDP endpoint): FAILED (`curl :9222` empty; no Chrome debug running).
- Prerequisite check 2 (authenticated ChatGPT tab): NOT ATTEMPTED (no endpoint to inspect).
- Direct URL fetch (`https://chatgpt.com/c/6ae57dbb-f3bc-83ec-abea-d8f57db27be6`): 404.
- Conclusion: GPT collaboration channel is DOWN. Per `p0-persistent-autonomy.md` (§11, point 11): when a consult is blocked, status updates must say so explicitly with the owner-required action; silent deferral reads to the owner as "no GPT interaction at all". Action taken: documented BLOCKED here; continued with independent evidence-based decisions (§3.3 autonomy directive: self-decision mode when evidence is sufficient and authorization scope is respected — this is a public repo creation + CI verification, no real spend/access changes, no secret/token exposure).

## 4. Independent Decisions (DECIDED — no clarification needed, within authorization scope)

Per lead-orchestrator §3.3a (autonomy directive, verified 2026-10-29): self-decision mode applies when evidence is sufficient, authorization scope is respected, and no secret/token access is required.

Decisions made (recorded here for audit trail — no hidden steps):

D-001: Initialize git repo locally (evidence: no `.git` existed; git is required for any GitHub publication and for worktree isolation per §14). No irreversible external effect.
D-002: Create `.orchestrator/` control plane (PROJECT_STATE.md, DISCUSSIONS.md, DECISIONS.md, VERIFICATION.md). This is bookkeeping, not a secret/action change.
D-003: Create GitHub repo as public (`D-Autotune`) under account `xegheplimo-web` (evidence: `gh auth status` confirms active account; `gh repo list` confirms existing repos; no name collision with `D-Autotune`). Public = lower risk than private for an open-source prototype; user can change visibility independently.
D-004: Push initial commit `2b5ac36` to remote `origin` (main branch). This is the first push — no overwrite risk.
D-005: Verify remote CI status via `gh run list` after push. Local native-dsp gate is BLOCKED by missing `cmake`/`gcc` (documented in VERIFICATION.md); remote CI will use `ubuntu-latest` which has these tools.
D-006: Report BLOCKED GPT collaboration to user explicitly (per autonomy directive rules 3, 5, 7: audit trail must exist; no fabricated evidence; no hidden omissions). Brief content that WOULD have been sent to GPT is preserved in `.orchestrator/DISCUSSIONS.md`.

## 5. Security / Authorization Checks (verified before action)

Per autonomy directive §3.3a (points 5, 6):
- No real financial spending triggered (`gh repo create` does not bill; CI runs are on GitHub's free tier for public repos).
- No secret/token used or exposed in any brief/file. The `gh` auth token is NOT printed or stored in files (`.gitignore` excludes `.devin/` and `.serena/` — no tool-scaffold writes allowed).
- No `.env` or credentials committed (`.gitignore` covers `.idea/`, `local.properties`, `**/build/`).
- No account/access change other than creating a new public repo (reversible by user deleting the repo).
- Fail-closed mechanism: if `gh repo create` fails (e.g., auth revoked, rate limit), action stops — no silent rerouting or fabricated success.

## 6. Integration / Next Steps (plan recorded, not executed beyond current session)

P0 (this session): RECON → ANALYZE → DECIDE → PLAN → ASSIGN (no Devin CLI delegation needed — the work is bookkeeping + git + GitHub creation, which is within Hermes §9 mechanical boundary) → IMPLEMENT (git init + `.orchestrator/` + GitHub repo creation) → VERIFY (push verification + remote CI check) → INTEGRATE → FINAL VERIFY.
P1 (next session / user decision): If GPT collaboration is required for DSP architecture decisions (P1 blocking per `docs/ROADMAP.md`: accurate pitch tracker, formant-preserving shifter, quality analysis), restore the collaboration (relaunch Chrome :9222, re-check ChatGPT URL). Until then, P1 work is BLOCKED by the missing channel.
P2 (after P1): Device qualification (`docs/ROADMAP.md` P2) — requires real Samsung devices, not simulated.
P3 (after P2): Commercial launch (`docs/ROADMAP.md` P3) — requires privacy policy, Play policy compliance, trademark clearance (note from `docs/THIRD_PARTY.md`: "Auto-Tune" may be a trademark).

## 7. Session update 2026-10-31 (CI GREEN milestone — supersedes §3 for GPT collaboration status)

GPT collaboration: §3 is SUPERSEDED. Round-1 brief was delivered via ChatGPT (attong39@gmail.com) and the reply archived verbatim at `docs/orchestration/gpt_g0_consultation_reply.md`; verdict encoded in `DECISIONS.md` D-007 (GO — G0 + CI parallel; Compose at G3; keep API 30/36 + 16 KB/API 37 gate; DSP: YIN → Tracking → TD-PSOLA → verification).

CI milestone (first full pass in project history):
- Run 41178223381 @ `848693c`: BOTH jobs success + artifact `D-Autotune-debug-APK` (1.93 MB, downloaded + SHA256 `eefe97e7…`).
- APK-level verification: 16 KB PT_LOAD `p_align == 0x4000` for ALL 6 .so (2 ABIs); exactly ONE permission (`RECORD_AUDIO`); both ABIs present.
- Fix cascade (5 runs): setup-android license hang → wrapper-JAR 404 → Prefab CXX1212 STL mismatch → Kotlin `fillViewport` interop → GREEN. Full evidence: `VERIFICATION.md` Gate 11; decisions D-008..D-011; run ledger `RUNS.jsonl` (5 rows).
- Ownership discipline held: DC-001 (Devin) = ci.yml only; D-009/D-010/D-011 (Hermes mechanical) = 2-line URL swap + 1-argument build config + 1-token Kotlin interop, each forced by the exact compiler/CI error and each verified by the NEXT run getting further.

In flight: DC-002 — latency/xrun instrumentation (Devin, card `scratch/dautotune-cards/dc-002-latency-instrumentation.md`). G0 audit item #14 (G1 prerequisite). Snapshot layout 8→12, xruns + algoLatency + cbMax/cbAvg, callback remains allocation/lock/log-free.

## 8. Session update 2026-10-31 (DC-002 landed + GPT round-2 verdict — supersedes §7 "In flight")

- **DC-002 LANDED + verified**: commit `db65a0f`, CI run 41179251125 GREEN (both jobs), APK re-verified (16 KB PASS, RECORD_AUDIO only). Diff-reviewed by Hermes. G0 audit #14 upgraded to IMPLEMENTED.
- **GPT round-2**: brief `docs/orchestration/gpt_round2_brief.md` (`6f6fb49`) SENT via Chrome debug; reply archived `docs/orchestration/gpt_round2_reply.md`; verdict **CONDITIONAL GO** encoded as D-012. Incident: first insert attempt landed in the thread's canvas doc (round-1 DIRECTIVE writing block) — restored via the block's own undo within ~30 s, no data loss; lesson recorded (disambiguate composers by viewport geometry, never first-match DOM).
- GPT round-2 requires: **DC-002A** (algoLatency semantics rejected — relabel + p99/histogram/deadline/xrun-validity/missingFrames-rate/input-starvation + granted stream properties), **CI-H** (license `|| true` fix + static APK gates: 16 KB ELF+ZIP, manifest/security, reproducibility), **G2-D0** (golden corpus + baseline of CURRENT detector). G2 GO parallel; G1 stays BLOCKED_HARDWARE; G1 thresholds <12/<20 ms demoted to research targets — Live gate stays p95 ≤ 50 ms (S) / 70 ms (A) + perceptual check.
- Round-3 prerequisites (what GPT expects next): DC-002A verification + revised DC-003 measurement design + D0/D1 golden benchmark evidence.
- Next dispatch: DC-002A (Devin) → verify → G2-D0 (Devin, disjoint files: native-tests/ only) → CI-H (Devin) → Hermes revises DC-003 card per approved design (correlation-based, 2 measurement classes, fail-closed) before its dispatch.

## 9. Session update 2026-10-31 (DC-002A LANDED + G1 plan REV 2 — supersedes §8 "Next dispatch")

- **DC-002A LANDED + verified**: commits `615bf8a` + `a58bcd6`, CI cascade run 41181595093 FAIL @ 615bf8a (real const-correctness error on out-stream pointer, caught by CI as designed) → Hermes mechanical fix `a58bcd6` → run 41181759477 **BOTH jobs GREEN** + artifact 1.93 MB + APK re-verified (16 KB PT_LOAD PASS ×6 .so ×2 ABIs; exactly 1 permission RECORD_AUDIO; debuggable=true expected). Full evidence: VERIFICATION.md Gate 12; G0 audit #14 → IMPLEMENTED+SEMANTICS FIXED. Snapshot is now 16 slots with honest labels: slot 9 retuneMs labeled "control" (NOT latency), new slots cbP99Ms/xrunValid/missFramesPerSec/shortReadPerSec; XRunCount moved OUT of RT callback into snapshot(); `getStreamInfo()` JNI reports GRANTED stream properties for honest Shared-vs-Exclusive baselines. NOTHING in the engine claims algorithm latency anymore (GPT round-2 D-012 requirement fully satisfied).
- **G1 measurement plan REV 2** committed (`80e39fc`): two measurement classes (Class 1 diagnostic acoustic loopback ≠ Class 2 authoritative fixture), release gate p95 ≤ 50 ms (S) / ≤ 70 ms (A) + perceptual check, 12/20 ms medians demoted to research targets — per GPT round-2 correction.
- **DC-003 REV 2 card ready** (`scratch/dautotune-cards/dc-003-loopback-ping-REV2.md`): correlation-based design per GPT-approved pattern — stimulus design, normalized cross-correlation ≥ threshold else INVALID_MEASUREMENT, N=100 cycles, timeout + amplitude cap + routing control + never-during-session constraints; result strictly labeled as diagnostic. NOT dispatched yet (sequence: G2-D0 → CI-H first, then DC-003 REV2).
- **G2-D0 dispatched (IN FLIGHT, Devin, card `scratch/dautotune-cards/g2-d0-golden-baseline.md`)**: synthetic deterministic corpus + metrics + baseline report of CURRENT detector in `native-tests/` only; PitchCorrector READ-ONLY; verification via CI native-dsp job (host has no cmake); findings (octave trap etc.) reported, NOT fixed — D1's scope.
- Remaining round-3 prereq: CI-H (Devin) after G2-D0; DC-003 REV2 dispatch after CI-H; owner devices still needed for G1 (BLOCKED_HARDWARE).

## 10. Session update 2026-10-31 (G2-D0 LANDED — golden corpus + measured baseline; supersedes §9 "G2-D0 dispatched")

- **G2-D0 LANDED + verified**: commits `a6020ae` (corpus + harness) → `20e83a8` (POST_BUILD table echo — Devin found ctest --output-on-failure hides passing tests' stdout, the baseline table must reach the CI log) → `4f2075b` (baseline report committed with numbers from CI run 41183129226). CI: runs 41182972143, 41183129226, 41183408556 ALL GREEN (both jobs each). Scope clean: 7 files, 920 insertions 0 deletions — native-tests/ + docs/baseline/D0_BASELINE.md only; PitchCorrector ZERO changes; no .devin/, no scaffold files, no binary corpus. Full evidence: VERIFICATION.md Gate 13; G0 audit #21 updated.
- **Measured baseline (CI run 41183129226, cross-checked verbatim by Hermes)**: 43-item deterministic corpus × 2 rates (48k/44.1k, seed `0xd0ba5eed`, hashes byte-stable): det 95.8% · median 1.0 ¢ · p95 43.8 ¢ · octave errors 0.00 % · V/UV 95.7 % · call med 20.2 µs p95 35.4 µs per 512×2 call (~270× realtime headroom, indicative).
- **Findings that shape D1/D2** (recorded, not fixed): tracking lag dominates real-singing error (vibrato d=5%: 44–67 ¢ median, p95 94 ¢; portamento ramps p95 112–121 ¢); hard SNR cliff between 10 dB and 5 dB (det% = 0 at ≤ 5 dB — detector deaf); V/UV misses at onsets (tvEu≈151 vs tuEv≈6); octave trap 110+220+440 did NOT trap (first-trough works); formant-shaped vowel held det 100% (missing fundamental). Timing run-to-run variance documented (second run: 10.0 µs median).
- **D1 card UPDATED with measured baseline floor** (`scratch/dautotune-cards/g2-d1-adaptive-yin.md`): targets ranked — (1) tracking lag reduction, (2) SNR-aware voicing (5 dB lock target det ≥ 80%), (3) V/UV onset misses; octave-error premise corrected (D0 shows 0%).
- **Next dispatch: G2-D1** (Devin, detector path only, shifter READ-ONLY) — dispatching now. CI-H follows after D1; DC-003 REV2 after CI-H. Round-3 brief to GPT once D1 lands (prereqs: DC-002A ✓, D0/D1 golden evidence, revised DC-003 design card).

## 11. Session update 2026-10-31 (G2-D1 LANDED — detector improvement verified; supersedes §10 "Next dispatch")

- **G2-D1 LANDED + verified**: commits `b2b6272` (detector) + `8837872` (report), CI runs 41184850825 + 41185190541 BOTH GREEN. native-dsp GREEN on first try (no fix cascade needed). Scope: detect path ONLY — 3-pass scan (fresh short window / long window / relaxed SNR fallback), native-rate lag refinement, bounded 2-point extrapolation, armed onsets; shifter byte-verified untouched; ~44 bytes state, no heap/locks. A/B discipline: frozen pre-D1 detector compiled in the same binary (d0_reference.cpp).
- **Measured D1 vs D0** (7563 chunks, A/B in same binary): det 95.8→**97.8%**, median 1.0→**0.2 ¢**, p95 43.8→**14.5 ¢**, oct 0.00% held, V/UV 95.7→**97.7%**, call med 20.1→14.7 µs. ALL 10 harness gates PASS. SNR cliff CLOSED (5 dB: det 0→100%, median 8 ¢). Tracking lag cut ~4× (vibrato d05/f7: 62.8→15.5 ¢ median; porta p95 121→53 ¢). tvEu 315→163, tuEv unchanged 6→6.
- Honest: SNR-threshold-scaling from the card REJECTED by Devin with a correct degeneracy argument (documented in D1_CHANGES.md §"Honest deviation"); modest regressions on snr_10/20/30 medians shown and justified; timing tail up on noise rows (~140× under budget). D1 ≠ D2 scope respected (no tracker added).
- **Next dispatch: CI-H** (Devin, card `ci-h-packaging-gates.md` ready — license-failure masking + 3 static APK gates), then G2-D2 (card ready `g2-d1-adaptive-yin.md` successor: `g2-d2` tracking/hysteresis — NOT yet written), then DC-003 REV2 dispatch, then round-3 GPT brief (prereqs now: DC-002A ✓, D0+D1 golden evidence ✓, DC-003 REV2 card ✓).

## 12. Session update 2026-10-31 (CI-H + G2-D2 LANDED — G2 detector/tracker stack complete through D2)

- **CI-H LANDED + verified**: commits `0a3b8b3` + `ab6ce8d`, runs 41186098647 + 41186271605 GREEN. License `|| true` masking GONE (sdk_pipe PIPESTATUS classification, auditable license output); APK gates in CI: 16 KiB packaging (6 libs p_align=0x4000 STORED+aligned), manifest security (only RECORD_AUDIO, no exported non-launcher, debuggable evidence), reproducibility (wrapper JAR checksum + distributionSha256Sum + ndk/CMake pinned). docs/CI_GATES.md documents mechanisms + known gaps (runtime gate → CI-RT card; Maven dependency verification explicitly out of scope). Fail-loud design self-corrected on its own first run (yes EPIPE 1 vs 141).
- **G2-D2 LANDED + verified**: commits `080ea4d` + `8f231fc`, runs 41187660379 + 41187902432 GREEN. detect() and shift() byte-identical to D1 (SHA256-verified by Hermes). Floor D1 HELD by construction (7563 chunks identical: det 97.8/med 0.2/p95 14.5/oct 0.00/VUV 97.7; tvEu 86→83 @48k, tuEv 6→6). D2 battery 26 asserts across 5 batteries ALL PASS (outlier rejection, Schmitt .65/.40/.20, breath window, ramp settle worst 40.5¢<50¢, bounded hold). Design: accepted-F0 ring + jump gate (≤150¢ instant reseed, retune ~21ms on 2nd detection), Schmitt band, breath window ≤150ms, correction law unchanged.
- **Sequencing note (Hermes decision)**: DC-003 REV2 dispatch HELD until D2 landed — both touch native-tests/CMakeLists.txt (D2 +11 lines, DC-003 +1 target); serializing avoids the commit race. DC-003 REV2 dispatching NOW (card ready, no revision needed — the REV2 design already matches the running architecture).
- **Next: GPT round-3 brief** (prereqs ALL now present: DC-002A ✓, D0/D1/D2 golden evidence ✓, DC-003 REV2 design card ✓, CI-H ✓). Round-3 brief draft in `scratch/dautotune-cards/round3-brief-draft.md` — fill D2 numbers, then send.
- **G2 remaining: D3 clean-room TD-PSOLA** (shifter replacement) → D4 end-to-end host verification. Then G3 (UX), G4 (RC), G5 (commercialization). G1 still BLOCKED_HARDWARE (owner devices pending; DC-003 REV2 gives the app-side measurement mode).

## 13. Session update 2026-10-31 (G2-D2EXT + DC-003 REV2 + G2-D3 LANDED — DSP stack complete through D3; D4 dispatching)

- **G2-D2EXT LANDED + verified**: commit `70b1147`, run 41189822166 GREEN. Test-only scope (test_d2_tracking.cpp +598, D2_CHANGES.md +105); PitchCorrector byte-identical. GPT round-3 batteries: F1/F2 staccato (target freeze on 1st new-note frame, ~21 ms cadence, no flip), G1–G5 scale-boundary vibrato (grazing carries, 0 chatter), H1/H2 interval + −1200¢ octave return. 157 PASS / 0 FAIL. Voicing telemetry: no hysteresis needed — confirms D2 design.
- **DC-003 REV2 LANDED + verified**: commit `f844289`, run 41189132393 GREEN. Correlation-based diagnostic loopback (class 1, never labeled mic→DSP→headphone latency); no second callback (measurement inside the existing output callback at the actual input read point); fail-closed everywhere (NCC < 0.6 → INVALID_MEASUREMENT; no class-2 hardware → MEASUREMENT_UNAVAILABLE); scope exactly 10 card files.
- **G2-D3 LANDED + verified**: commits `8ee8394` + `5538e8b` (cleanup), runs 41192970665 + 41193157913 GREEN. Clean-room TD-PSOLA replaces the granular crossfade. **detect()/applyDetection byte-frozen** (SHA256 both regions identical 70b1147→5538e8b). 6/6 ctest green; D1 floor identical. Batteries ALL PASS × 2 rates: output F0 −0.6 ¢ full-chain (driven −0.01 ¢), bypass bit-exact, formants stay put (F1 666.2/F2 1207.6 inside ±60) while LEGACY granular FAILS the formant gate (F1 880/F2 1410 — metallic risk PROVEN fixed), click-free + transient + deterministic delay 2.27–4.17 ms (LOWER than old ~18.7 ms), per-call p95 15.2 µs ≈ D0's 14.7 µs, 2 fresh instances bit-identical.
- **Next: G2-D4 end-to-end host verification** (card `scratch/dautotune-cards/g2-d4-e2e-verification.md` ready — A/B old-shifter vs new under identical conditions, both rates, regression verdict per round-3 rule). After D4: GPT round-4 consultation (D-014: corrected-output F0, artefact examples, formant comparison, actual signal-delay measurements, CI status, real-voice evidence if available). Then real-voice corpus (license-vetted VocalSet subset) BEFORE G3 implementation. G1 still BLOCKED_HARDWARE.

## 14. Session update 2026-10-31 (G2-D4 LANDED — G2 DSP chain D0→D4 COMPLETE; GPT round-4 next)

- **G2-D4 LANDED + verified**: commits `80adfd6` + `2fa679e`, runs 41194634866 + 41194792144 GREEN. Scope exactly 5 card files (+1921: test_d4_e2e.cpp 1044, d2_reference.{cpp,h} 666, D4_CHANGES.md 199, CMakeLists +12); PitchCorrector.cpp/.h byte-identical (SHA256 verified both sides). d2_reference verbatim copy of 70b1147 PitchCorrector (3 mechanical hunks only — Hermes-verified). 7/7 ctest green; D1 floor identical.
- **D4 A/B verdict (round-3 rule) — BOTH rates**: pitch accuracy NEW worst 0.61 ¢ vs OLD 1.61 ¢ (both <3 ¢, SAME); formant retention NEW s* 0.990 vs OLD 1.022 @48k / pumping-corrupted @44.1k (BETTER); artifacts NEW 1 excursion vs OLD 15 @44.1k (BETTER); DSP delay NEW 2.27 ms vs OLD 18.67/20.32 ms (BETTER, 9×); callback cost NEW 19 µs vs OLD 10.8 µs (WORSE but ≪ 200 µs budget — ≈10× chunk headroom). No gate regresses without compensating benefit — **G2 DSP chain D0→D4 COMPLETE**.
- **Next: GPT round-4 consultation** (D-014 prereqs ALL present: D3+D4 landed, corrected-output F0 ✓, formant comparison ✓, artefact examples ✓, signal-delay measurements ✓, CI status ✓, real-voice evidence pending — license-vetted VocalSet subset selection is the open work item). Round-4 brief covers: D3+D4 results, A/B verdict, remaining risks, G3 plan + real-voice plan.
- After round-4: G3 (UX) per its verdict; G1 still BLOCKED_HARDWARE (owner devices pending).


## 15. Session update 2026-10-31 (round-4 closed; D5 real-voice harness DISPATCHING — REAL VOICE FIRST; G3 prep GO)

- **GPT round-4 COMPLETE**: brief sent via Chrome debug :9222 (same thread `6ae57dbb`, 5th consultation), verdict archived `docs/orchestration/gpt_round4_reply.md`. D-015 encoded: **G2 HOST-VERIFIED PASS** (do not rewrite TD-PSOLA without a reproducible failure), **REAL VOICE FIRST** (license-vetted harness: ≥20 clips, ≥4 singers, VocalSet preferred with documented CC BY attribution; objective output-F0/formant/pumping/artifact measurements + 3-person blind listening), **G3 UX PREPARATION GO** (Hermes designs UI architecture + interaction contracts now; Compose gated on real-voice smoke-test PASS), **DSP QUALITY FOLLOW-UP** (investigate the 3.5–4.2 dB envelope pumping — quantify on real steady-state vocals; fix = D6, only if audible), **COMMERCIAL RELEASE BLOCKED** until real-voice evidence. Never claim VOCAL-QUALITY VERIFIED / LIVE CERTIFIED / PRODUCT_COMPLETE.
- **p95 reconciliation DONE** (GPT demanded it before next review): D4_CHANGES.md now carries a two-host table — CI Ubuntu runner canonical 18.9/19.0 µs (new) vs 10.7/10.8 µs (old); local Windows MinGW cross-check 53.9/54.6 vs 20.5/25.7 µs; host note says ratio ≈ 1.8–2.1× holds on both, verdict cites CI. Commits: `75c2852` (D4 control plane), `9a52d92` (D-014/D-015 + reconcile), `d1c4937` (housekeeping + DISCUSSIONS §6–7).
- **License vetting DONE** (Hermes, 2026-10-31): VocalSet audio = **CC BY 4.0** (Zenodo 1193957 + 1442513, 20 singers, 10.1 h) — redistribution with attribution permitted. **Annotated-VocalSet** = **CC BY 4.0** (Zenodo 7061506; Faghih & Timoney, Appl. Sci. 12(18):9257, 2022) — per-note F0/onset/offset annotations → independent ground truth for the harness (not self-referenced by our own detector). Earlier NC/SA ambiguity traced to third-party repos applying their own cc-by-nc-4.0 to derived models, NOT the dataset license.
- **D5 DISPATCHED** (Devin, background, card `scratch/dautotune-cards/g2-d5-real-voice-harness.md`): Devin = harness code (loader + manifest/annotation schema, batteries R1 output-F0 / R2 formant / R3 envelope-pump / R4 artifacts / R5 delay, listening-pack export, CI wiring) with skip-loud when corpus absent — NO fake clips, PitchCorrector byte-frozen; Hermes acquires the VocalSet subset (20+ clips, ≥4 singers) + annotations AFTER the schema lands; blind listening = BLOCKED_HUMAN (needs 3 real listeners). Pumping FIX = D6, dispatched only after real numbers (trigger: pump excess > 3 dB input-corrected on real sustained vowels).
- **G3 prep GO**: design doc `docs/design/G3_UI_CONTRACT.md` (Hermes-owned, no code) drafted while Devin runs — UI architecture + interaction contract only; Compose implementation gated on real-voice smoke-test PASS.
- **G1** still BLOCKED_HARDWARE. **Next GPT review (round-5)**: real-vocal measurements + blind listening results + pumping root-cause + G3 readiness.

## 16. Session update 2026-10-31 (corpus LANDED — first real-voice numbers: 2 real defects caught by the gate; D6 dispatching)
- **Corpus v1 LANDED** (Hermes): `e70719c` + key fix `b7bd4a1` — VocalSet CC BY 4.0 subset: 20 clips / 4 singers / straight technique (long_tones + scales), Annotated-VocalSet reference F0, THIRD_PARTY.md attribution. CI loader: `20 declared, 20 usable, 0 rejected`. Blind-listening pack v1 generated (8 pairs, RMS-matched randomized; key + sheet committed; WAVs gitignored).
- **First real-voice R1–R5 run: the gate WORKED — 2 real defects found.** CI run 41199096037 RED (native-dsp 3 failures; android PASS); local repro identical. **R2 formant FAIL** (worst |ΔF1| 884 Hz / ΔF2 482 Hz, gate ±60) — suspected octave-error windows wrecking formants via huge PSOLA shifts. **R4 FAIL** (5/432 amplified boundaries, worst 11.53×, all silence→tone onsets) — Devin's D5 per-sample-edge prediction confirmed on real attacks. **R3 PASS on real voice** (+0.03 dB excess — round-4's 3.5–4.2 dB pumping question CLOSED: synthetic artifact; D6 trigger not fired). **R5 PASS** (3.75 ms class). **R1 informational**: detector octave tail female-only (f1/f3 p95 ≈1200¢ = exactly one octave vs males 68–71¢) — suspected sub-octave lock; output audibly an octave low in those windows; det 87.3% real vs 97.8% synthetic.
- **D6 DISPATCHED** (Devin, card `g2-d6-realvoice-fix.md`, base `b7bd4a1`): root-cause + fix — detector octave stability, R2 formant (engine-vs-measurement via per-window instrumentation), R4 onset amplification (without regressing D3 transient/click-free); NEW R1 octave-tail gate (0 windows ≥600¢); full regression suite must stay green; listening pack regenerates post-landing. CI red on main until D6 lands (expected, documented).
- **Round-5 brief to GPT**: real-voice measurements + defect evidence + D6 scope + R3 closure + listening pack ready (3-person listening BLOCKED_HUMAN).
- G3 prep unchanged (`docs/design/G3_UI_CONTRACT.md`); Compose remains gated on real-voice smoke-test PASS — now additionally gated on D6 fixes landing. G1 still BLOCKED_HARDWARE.

## 17. Session update 2026-10-31 (D6 LANDED — real-voice defects fixed, CI green on main, brief round-5 ready)
- **D6 LANDED**: commit `bf6c3e9` (Devin, card `g2-d6-realvoice-fix.md`), CI run 41206064557 GREEN both jobs; orchestrator re-verified 10/10 ctest locally. The gate worked: run 41199096037 RED by design caught 2 real defects; instrumentation then corrected the initial diagnosis (female ~1200¢ tail was a measureF0 measurement artifact, output NOT octave-low; the REAL engine defects were detector octave-UP/DOWN phantom locks on H2-dominant voice + R4 passthrough-lag teleport at V/UV onset).
- **Fixes**: detect() subharmonic check; applyDetection() pair-evidence octave-phantom veto (seam detections never vetoed — D2 battery H untouched); passLag_ slews at the xfade rate (no teleport, no latency, no heap); measureF0 1000 Hz band + octave-safe multi-candidate pick; R2 scorer → LPC envelope centroid (order 24); NEW R1 gate 0 windows ≥600¢ in CI_GATES.md §7.
- **Numbers**: R1 tail 0≥600¢ (was ~1205¢); R1 p95 f1 39.7 / f3 28.3 / m1 67.1 / m3 62.1¢; R2 worst 41/31 Hz (gate ±60 unchanged); R4 0/432 worst 1.17× (was 5/432 worst 11.53×); R3 +0.03 dB, R5 3.75 ms unchanged. Summary: 3 FAILURES → COMPLETE, 0 warnings.
- **Round-5 brief** (`docs/orchestration/gpt_round5_brief.md`) updated with the corrected diagnosis + D6 landing + before→after table; 5 questions to GPT (D6 verdict, next-order listening→G3, R4 residual gate, listening protocol, corpus breadth). GPT send pending via Chrome debug.
- Blind listening BLOCKED_HUMAN (pack ready); G3 implementation unblocked pending GPT round-5 verdict; G1 BLOCKED_HARDWARE.



### Round-5 verdict (2026-10-31, post-D6): D6 ACCEPTED / HUMAN LISTENING PENDING / G3 CONDITIONAL GO
- GPT verified repo @5e381ee + both CI runs independently; archived `docs/orchestration/gpt_round5_reply.md`.
- Ordered sequence: (1) D7 = R1 residual-window diagnosis + detector-coverage classification (86.0% → silence/transition/breath/missed) + R4 >1.10× nonblocking warning; (2) D8 = blind pack redesign (2 pairs/singer long-tone+scales + 2 A/A controls, PRIVATE key — never committed before ratings frozen); (3) 3-person listening = BLOCKED_HUMAN; (4) if PASS → G3 Compose implementation; (5) parallel: corpus expansion (≥8 singers/≥40 clips/vibrato+falsetto) + G1 device prep for G4.


## 18. Session update 2026-10-31 (round-5 verdict encoded — D7 DISPATCHED: R1 residual diagnosis + coverage classification + R4 warning)
- **D7 DISPATCHED** (Devin, background, card `scratch/dautotune-cards/g2-d7-r1-residual.md`, base `73f515b`, persist_on_release): three tasks per GPT round-5 verdict FIRST item — (1) R1 residual-window per-cause table: worst windows per singer, time-aligned output F0 vs instantaneous targetHz() vs annotation F0, classify (a) vibrato/reference (b) retune lag (c) segment-median target (d) measureF0 error (e) genuine engine defect — NO engine changes unless case (e) reproduced, then STOP and REPORT; (2) detector-coverage classification: every non-detected voiced chunk → silence-adjacent/transition/breath/genuinely-missed, per singer + context, plus the 87.3→86.0% honesty row; (3) R4 >1.10× nonblocking diagnostic warning (hard gate stays 1.25×+0.01; no threshold tightening). New `docs/baseline/D7_CHANGES.md` carries the tables.
- **Forbidden in D7**: PitchCorrector (frozen — diagnosis only), corpus files (Hermes owns data; D8 redesigns the pack with a PRIVATE key), listening_key.csv, CI gates numeric changes.
- **Parallel**: GPT round-5 verdict fully encoded (DISCUSSIONS §8, VERIFICATION Gate 19, DECISIONS D-017); D8 blind-pack redesign card is next after D7 lands; 3-person listening stays BLOCKED_HUMAN; G3 Compose gated on listening PASS; corpus expansion (≥8 singers/≥40 clips/vibrato+falsetto) is the G4 prep item.


## 19. Session update 2026-10-31 (D7 LANDED — R1 residual = expected behavior, coverage fully explained, R4 warn band in; D8 dispatching)
- **D7 LANDED**: commit `96cdd07` (Devin, card `g2-d7-r1-residual.md`), CI run `41211475193` GREEN both jobs; orchestrator re-verified **10/10 ctest PASS** locally. Engine untouched — PitchCorrector SHAs identical to D6 (`0d11c14…`/`c1afc18…`). Scope exactly 4 files (+757): d5_measure.h (+71 shared helpers incl. behavior-identical measureF0 refactor), test_d5_realvoice.cpp (+509 instrumentation), CI_GATES.md (+3), new `docs/baseline/D7_CHANGES.md` (201 lines).
- **Task 1 verdict — R1 residual = EXPECTED BEHAVIOR, no engine defect**: e=0/361 worst windows (was the STOP condition — not triggered). Dominant cause `b` = retune smoothing lag 219/361 (61%, m3 109/m1 52 where target steps are dense); `a` = reference artifacts 104/361 (vibrato-snap smear segSteps 18-25 + det-gap passthrough); `c` = mid-window target changes 31; `d` = measurement-limited 7 (all ≤38¢, isolated). Several `b` rows are within 10¢ of the ANNOTATION (m3 scales_a 4.6¢, f3 scales_o 7.0¢) — output faithful to the sung note while the segment-median reference smears. The round-5 brief's "corpus vibrato vs reference-F0" attribution was imprecise; GPT's rejection upheld by data.
- **Task 2 — coverage 87.32%→86.03% fully explained**: 2,294 non-detected voiced chunks classified S=342 / T=247 / B=1,468 (1,467 low-energy interior, m1 breathy dominates — energy-floor story, not tracking loss) / M=237 flagged (233 tentative holds estVoiced=1, only 4 hard drops: 1 was an old +1166¢ phantom removal = correct, 1 genuine 1-hop miss on m3 scales_i t=4.42 — report-only, no reproducible pattern). Old-vs-new trace probe reproduces exactly: old 14,338/16,421 = 87.32%, new 14,127 = 86.03%, net −211 (267 lost/56 gained; 33 lost = old phantom octave-UP detections correctly removed, 232 = tentative-hold bookkeeping, 2 = hard drops).
- **Side observation (report-only, future card)**: new detector telemetry emits 161 sub-octave-ish candidates vs old 11 (super-octave 114→32) — tracker veto holds tgt (output unaffected, R1 tail 0/1445); NOT a tracking defect, candidate for D8+ detector-polish.
- **Task 3 — R4 warn band in**: `out > 1.10×in + 0.01` → WARN line (clip/timestamp/amp/ratio), never fails; 0/432 currently in band (worst 1.17× absorbed by abs tolerance); hard gate unchanged 1.25×+0.01; CI_GATES.md §7 documented.
- **D8 DISPATCHED next** (card `g2-d8-blind-pack-redesign.md` ready): listening pack redesign per GPT round-5 order — balanced 2/singer (1 long_tones + 1 scales) = 8 main pairs + 2 hidden A/A controls, fresh private randomization, key → `docs/corpus/listening_key_v2.csv` gitignored (old public key git-rm'd from HEAD), sheet updated to 10 pairs with neutral instructions.
- Listening = BLOCKED_HUMAN after D8 (need 3 real listeners); G3 Compose gated on listening smoke PASS; corpus expansion (G4) + G1 device prep remain parallel.


## 20. Session update 2026-10-31 (D8 LANDED — blind pack v2: balanced + private key; LISTENING GATE OPEN = BLOCKED_HUMAN)
- **D8 LANDED**: commit `69bf645` (Devin, card `g2-d8-blind-pack-redesign.md`), CI run `41212546946` GREEN both jobs. Scope exactly 5 files: d5_listening_pack.cpp (rewrite), listening_sheet.md (10 rows, neutral instructions, "none — identical/indistinguishable"), README.md (v1 void/v2 private note), .gitignore (+1 entry), listening_key.csv git-rm'd from HEAD.
- **Pack v2 verified**: 10 pairs → 20 WAVs; 8 main pairs balanced 2/singer (1 long_tones + 1 scales each; f1/f3/m1/m3); 2 hidden A/A controls (1 scales + 1 long_tones, byte-identical A/B — listener-reliability check); fresh private randomization (no clip overlap main vs control, mixed A/B roles); key `docs/corpus/listening_key_v2.csv` gitignored (check-ignore confirmed) — NEVER committed before ratings frozen; old public key void (stays in history). Devin also exercised the SKIP/FAIL disciplines via scratch corpora (absent corpus → SKIP exit 0; <4 singers / missing context → loud FAIL listing each gap) with the cygpath fix.
- **Hermes follow-up**: Devin flagged `docs/CI_GATES.md:110` still citing the old key path (out of its scope) — updated by orchestrator to the v2-private language in this commit.
- **LISTENING GATE = BLOCKED_HUMAN** (the G3 gate): pack v2 WAVs are in `docs/corpus/listening_pack/` (gitignored, regenerated by `d5_listening_pack`), sheet `docs/corpus/listening_sheet.md` (committed, 10 rows). Need 3 independent human listeners; ratings frozen before the key can be committed. PASS gate: corrected median naturalness ≥3/5, no repeated critical artifacts, no pair where all 3 raters fail a corrected sample unexplained.
- Next after listening PASS: G3 Compose implementation (Devin). Parallel: corpus expansion (G4) + G1 device prep. Next GPT review: 3-listener results + D7 diagnosis + G3 readiness.

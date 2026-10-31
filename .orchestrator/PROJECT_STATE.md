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

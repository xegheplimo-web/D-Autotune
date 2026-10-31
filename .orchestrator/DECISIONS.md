# D-Autotune — Decisions (Audit Trail)

Reference: `lead-orchestrator` references/autonomy-directive.md (verified 2026-10-29) + `references/p0-persistent-autonomy.md`.
Every decision must have a verifiable artifact file. No hidden steps.

---

D-001 — Git init (2026-10-31)
Evidence: `F:/D-Autotune` had no `.git` directory (verified by `ls -la` + `git rev-parse` returning `fatal: not a git repository`).
Action: `git init` + `git config user.email/name` + initial commit `2b5ac36` (36 files, 1300 insertions).
Why independent: No secret/token used; no external effect beyond local filesystem; reversible (can delete `.git` if needed, though that would lose history).
Verification: `git log --oneline` shows `2b5ac36`; `git status --short` is clean.

D-002 — `.orchestrator/` control plane creation (2026-10-31)
Evidence: No `.orchestrator/` directory existed before this session (`ls -la` verified empty).
Action: Created `.orchestrator/PROJECT_STATE.md`, `.orchestrator/DISCUSSIONS.md`, `.orchestrator/DECISIONS.md`, `.orchestrator/VERIFICATION.md`.
Why independent: Bookkeeping; no code change; preserves audit trail for future sessions; aligns with lead-orchestrator §19 (control plane updates in the same session as related integration).
Verification: All 4 files exist (`ls .orchestrator/`); content verified by `read_file`.

D-003 — GitHub repo creation (public, `D-Autotune`, `xegheplimo-web`) — TO BE EXECUTED IN NEXT STEP
Evidence: `gh auth status` confirms active account (`xegheplimo-web`); `gh repo list` shows existing repos (`VietScope`, `hermes-orchestrator`, `VietGeo_Nova_2026`, etc.); no `D-Autotune` repo exists (verified by `gh repo list --limit 10` — no `D-Autotune` entry).
Why independent: No secret/token exposed in repo creation (repo name is public); no code change to project; user can delete repo independently.
Planned verification: `gh repo view xegheplimo-web/D-Autotune --json url,name` after push; `git remote -v` shows `origin https://github.com/xegheplimo-web/D-Autotune`.

D-004 — Push to remote (`main` branch) — TO BE EXECUTED IN NEXT STEP
Evidence: Commit `2b5ac36` exists locally; `.github/workflows/ci.yml` is included (verified by `git ls-files` after commit).
Why independent: Push is a standard git operation; no file content is modified (only transmitted); the repo is public, so no sensitive data exposure.
Planned verification: `git log --oneline main..origin/main` is empty (same commits); `gh run list --repo xegheplimo-web/D-Autotune` shows CI runs.

D-005 — Verify remote CI (BLOCKED locally by missing `cmake`/`gcc`, available remotely) — TO BE EXECUTED
Evidence: `cmake --version` returns `command not found`; `gcc --version` returns `command not found`; `.github/workflows/ci.yml` specifies `ubuntu-latest` (which has `cmake` and `gcc` by default) and `tests/site/` (native-dsp: `cmake -S native-tests -B /tmp/dautotune-tests -DCMAKE_BUILD_TYPE=Release` + `ctest`).
Why independent: This is verification of an existing mechanism, not a new design. No action changes the project.
Planned verification: `gh run list --repo xegheplimo-web/D-Autotune --limit 5`; check `conclusion` field (`success` / `failure` / `skipped`). For `native-dsp`, verify that the job completes the 3 steps (checkout, host C++ tests, artifact upload). For `android`, verify the 4 steps (checkout, setup-java, setup-android, install SDK components, compile debug APK, upload artifact).

D-006 — GPT collaboration BLOCKED (2026-10-31)
Evidence (verified):
- Chrome `:9222`: `curl -s --max-time 3 http://localhost:9222/json/version` → empty (no response).
- ChatGPT URL: `https://chatgpt.com/c/6ae57dbb-f3bc-83ec-abea-d8f57db27be6` → `404 Not found` (verified by `web_extract` — server response, not a proxy error).
- Brief content preserved in `.orchestrator/DISCUSSIONS.md` (never sent — DATA preserved, not executed).
Why independent: The block is an external infrastructure issue (`:9222` not running + conversation URL missing), not a design/implementation decision. Per autonomy directive §3.3a (point 2): self-decision applies when evidence is sufficient; the user explicitly asked to "test the full CI process" (a verification task, not a design task requiring external architect input); the next design direction (`docs/ROADMAP.md` P1: accurate pitch tracker, formant-preserving shifter, hysteresis, quality analysis) requires either the restored GPT channel or an explicit user decision, so P1 remains BLOCKED.
Verification: `.orchestrator/DISCUSSIONS.md` exists with full brief + verification log; no fabricated reply; no hidden omission; `PROJECT_STATE.md` references the BLOCKED status.
STATUS UPDATE (2026-10-31, later same session): RESOLVED — Chrome debug relaunched via PowerShell Start-Process (canonical form; `cmd //c` launcher failed silently, known pitfall); ChatGPT tab logged in (attong39@gmail.com); conversation found in account listing (the URL the user gave IS the D-Autotune thread; earlier "404" was the keyless fetcher, not the server). Brief SENT via composer (aria-label 'Hỏi ChatGPT', Input.insertText, 2659 chars, readback verified) and reply RECEIVED + archived at `docs/orchestration/gpt_g0_consultation_reply.md`.

D-008 — DC-001 integration (android CI fix) + scope-leak incident (2026-10-31)
Devin run: `devin --respect-workspace-trust=false --permission-mode dangerous --model swe-2-max -p --prompt-file <card>` from repo root; card `scratch/dautotune-cards/dc-001-fix-android-ci.md`; log `dc-001-run1.log` (9,133 bytes); process PID 40244 exited normally; telemetry row in `.orchestrator/RUNS.jsonl` (dc001-20261031-r1).
Accepted deliverable: `.github/workflows/ci.yml` — drop `android-actions/setup-android` entirely; drive the runner's preinstalled SDK with `yes | timeout 300 sdkmanager --licenses` + `yes | timeout 600` component install (fail-loud, no `|| true` on install); `timeout-minutes: 15` backstop; `./gradlew` → `sh gradlew` (gradlew committed 100644, no exec bit — verified `git ls-files -s`).
Root-cause evidence (Devin-fetched, Hermes-accepted): setup-android v3 AND v4 feed a FIXED 10-line 'y' buffer into `sdkmanager --licenses` via `@actions/exec input`; the observed prompt sequence (8+ prompts) outlives it → stdin starvation → infinite hang. v4's mechanism is byte-identical → upgrade ≠ fix. GPT's original hypothesis ("v4 supports license acceptance") was true but insufficient — corrected by source-level evidence. This is the directive's own §1.6 discipline (claims tied to reproducible evidence) applied to the architect's own suggestion.
Integration: commit `d7becf1` (ci.yml only, 20+/4-), pushed to origin/main; remote run 41177042055 triggered.
Hermes independent verification: yq YAML parse OK; diff scope = ci.yml only (native-dsp byte-identical); claims re-checked (gradlew mode 100644 ✓, POSIX shebang ✓); Devin report contains no fabricated CI result (explicit "No CI result is claimed").
SCOPE-LEAK INCIDENT (honest record): during the Devin session, `gradle/wrapper/gradle-wrapper.properties` was modified at 13:28:53 by the toolchain-scaffolding layer (NOT Devin's hand-edits — the `#Sat Oct 31…` regeneration signature): distributionUrl 8.13→9.7.1 while KEEPING the 8.13 checksum (`20f1b117…`) — an internally inconsistent file that would deterministically fail wrapper checksum verification after the license fix (9.7.1 official bin checksum is `acd53f1e…`, verified against gradle.org/release-checksums). The original 8.13 pairing was independently verified correct (`20f1b117…` sits under the 8.13 heading on the official page). Reverted by Hermes via in-place write (`git show HEAD:` redirect — plain `git restore` failed with unlink error because Devin's redhat.java language server held the file open; per §13 kill-safety the lane was NOT killed). Lesson recorded: a card whitelist that names files does not stop the agent's OWN toolchain hook from scaffolding outside it — post-run scope check must diff EVERYTHING, not just expected paths.
Verification of the real gate: remote CI run 41177042055 (head d7becf1) — verdict recorded in VERIFICATION.md Gate 11 update when concluded. PASS = android job completes assembleDebug + uploads `D-Autotune-debug-APK`. A new failure at a later step = a NEW bug (GPT condition, D-007), never "CI fixed".

D-007 — GPT consultation round 1 verdict encoded (2026-10-31)
Evidence: full reply verbatim at `docs/orchestration/gpt_g0_consultation_reply.md` (extracted from DOM tail after completion markers: "Phản hồi mới nhất" + no streaming button; total main.innerText 38,968 chars; reply starts after "ChatGPT đã nói:").
Decisions encoded (GPT verdict, Hermes-verified against repo before adoption):
- ORDER: (b) G0 audit PARALLEL with CI fix. Devin CLI fixes Android CI as P0 task #1 (small independent PR); Hermes does G0 audit without waiting for CI.
- ROOT-CAUSE DISCIPLINE (GPT correction accepted): "Chưa đủ bằng chứng kết luận setup-android@v3 chính là nguyên nhân gốc" — the hang at "Review licenses..." proves the license step blocked the process, does NOT prove the action is broken. setup-android@v4 supports license acceptance too. CI fix PASS = assembleDebug completes + valid APK artifact uploaded; a NEW failure later = a new bug, never "CI fully fixed".
- UI: Jetpack Compose rewrite at G3 ONLY. MainActivity.kt (plain Activity) kept through G0–G2; only direct blockers (mic/permission/routing) fixed earlier. No UI rewrite in G0/G1/G2.
- SDK: keep minSdk 30 / targetSdk 36 for G0–G2. Add: 16 KB memory-page-size compatibility check in CI as soon as APK builds (NDK r28+, ELF alignment, APK/AAB packaging — gate for Feb 2027); targetSdk 37 (Android 17) prep at the pre-release compatibility gate (Aug 2027 roadmap); RECORD_AUDIO permission lifecycle, audio routing, foreground-service restrictions, 64-bit ARM checks.
- DSP G2 order: adaptive-window YIN + voicing (confidence, silence/consonant, octave rejection) → pitch tracking + hysteresis → TD-PSOLA + pitch-mark validation → end-to-end verification. MANDATORY precondition before TD-PSOLA: tracker must output F0 + confidence + voiced/unvoiced + temporal continuity (not just one sine frequency). No pYIN/HMM initially — only if YIN benchmark fails. No hard latency gate from assumptions — measure algorithmic latency (lookahead, window, overlap-add) separately.
- G1 = BLOCKED_HARDWARE permanently until owner provides Samsung device(s) — never self-promoted to PASS.
- Next GPT round: after Android CI produces a valid APK — send commit, log, and G0 audit findings.
Verification: decisions cross-checked against repo state (manifest minSdk 30/targetSdk 36 confirmed in AndroidManifest read; CI log evidence in VERIFICATION.md Gate 11; DSP chain compatible with PitchCorrector.cpp current structure).

D-009 — Hermes mechanical fix: wrapper-JAR downloader 404 (gradlew + ensure-wrapper.ps1) (2026-10-31)
Failure evidence (remote, run 41177042055): android job PASSED the former blocker (license step — `Install Android components` completed; the D-007/D-008 fix worked) then FAILED at `Compile debug APK`: `curl: (22) The requested URL returned error: 404` from the gradlew bootstrap. GPT condition honored: classified as a NEW bug, not "license fix failed".
Root cause (Hermes-verified): both `gradlew:8` and `scripts/ensure-wrapper.ps1:6` download `https://services.gradle.org/distributions/gradle-8.13-wrapper.jar`, which 301s and 404s (services.gradle.org serves distribution ZIPs, not standalone wrapper JARs). Correct source: `https://raw.githubusercontent.com/gradle/gradle/v8.13.0/gradle/wrapper/gradle-wrapper.jar` — downloaded 43,705 bytes, SHA256 `81a82aaea5abcc8ff68b3dfcb58b3c3c429378efd98e7433460610fecd7ae45f` = EXACTLY the EXPECTED checksum already hardcoded in both scripts (checksum validation logic untouched).
Why Hermes, not Devin (§9 mechanical boundary): 2-line URL substitution, zero logic change, independently verified locally BEFORE push — `scripts/ensure-wrapper.ps1` run end-to-end on this host: download + checksum verify + move all PASSED ("Gradle wrapper verified."). A Devin dispatch for a verified 2-line change with the evidence already in hand is disproportionate overhead.
Also: `.gitignore` += `gradle/wrapper/gradle-wrapper.jar` (design is a bootstrap downloader; the jar must never be committed — local test had produced an untracked jar artifact).
Verification gates: local ensure-wrapper.ps1 run PASSED (real download+checksum); remote gate = CI run on the fix commit (verdict recorded in VERIFICATION.md Gate 11).

D-010 — Hermes mechanical fix: Prefab CXX1212 STL mismatch (app/build.gradle.kts) (2026-10-31)
Failure evidence (remote, run 41177443204): wrapper bootstrap PASSED (curl 404 gone; build reached CMake configure 58s in), then `Task :app:configureCMakeDebug[arm64-v8a] FAILED` — `com.google.prefab.api.NoMatchingLibraryException: No compatible library found for //oboe/oboe … [CXX1212] User is using a static STL but library requires a shared STL`. native-dsp still success.
Root cause (Hermes-verified): `app/build.gradle.kts` sets NO `-DANDROID_STL` → NDK default `c++_static`; the Google-Maven Oboe AAR (Prefab) ships compiled against the shared STL → Prefab hard-rejects the combination. Canonical fix: `arguments += listOf("-DANDROID_STL=c++_shared")` (comment in file). Consequence documented: `libc++_shared.so` is bundled automatically by AGP for both ABIs (arm64-v8a, x86_64); Oboe 1.11.0 and NDK r27+ libc++_shared support 16 KB page alignment (gate #22 still pending APK-level verification).
Why Hermes, not Devin (§9 mechanical boundary): 1-argument build-config addition forced unambiguously by the CXX1212 error; DSP/business logic untouched; local Android build impossible on this host (no SDK), so the only verification available is the remote CI — same discipline as D-009.
Verification gates: kts syntax trivial (Kotlin DSL `arguments += listOf(...)`); remote gate = CI run on the fix commit (verdict recorded in VERIFICATION.md Gate 11). Cascade so far: license hang → wrapper 404 → STL mismatch → (next unknown). Each fix is verified by the NEXT run getting further; the PASS gate remains assembleDebug + APK artifact.

D-011 — Hermes mechanical fix: Kotlin interop — `fillViewport` synthetic property does not exist (MainActivity.kt:99) (2026-10-31)
Failure evidence (remote, run 41177598995): CXX1212 GONE — `:app:configureCMakeDebug` and `:app:externalNativeBuildDebug` PASSED (D-010 verified by this run getting further); native-dsp success. New failure: `compileDebugKotlin` — `e: MainActivity.kt:99:47 Unresolved reference 'fillViewport'`. Line 99: `ScrollView(this).apply { fillViewport = true; clipToPadding = false; fitsSystemWindows = true }`.
Root cause (Hermes-verified): Kotlin interop rule — a getter `isX()` maps to Kotlin property `isX`, NOT `x` (e.g. `Thread.isDaemon()`, `View.isSelected()` → `isX`/`isX`). `ScrollView.setFillViewport(boolean)` pairs with `isFillViewport()` → property `isFillViewport`; `fillViewport` is therefore unresolved. `clipToPadding`/`fitsSystemWindows` are fine (they have true `getX()` getters).
Fix: `setFillViewport(true)` — explicit setter (API 1), bulletproof regardless of interop subtleties; matches the file's imperative style. `isFillViewport = true` would also compile; chose the explicit setter.
Repo-wide scan for the same landmine BEFORE push: `fillViewport` = 1 occurrence only (fixed); audited every boolean-assignment in the file (`includeFontPadding`, `isAllCaps`, `clipToPadding`, `fitsSystemWindows`, `textSize`, `setTextColor`) — all valid (`setTextColor` correctly already explicit: TextView has no `getTextColor()` getter, only `getCurrentTextColor()`). No other occurrences of this pattern.
Why Hermes, not Devin (§9 mechanical boundary): single-token interop fix forced by the exact compiler error; no design decision involved (same discipline as D-009/D-010). NOTE for handoff: this is Devin's code — 2 interop-class issues in Devin-authored Kotlin now observed; the next Devin card must include a self-review pass over Kotlin interop properties (or CI gains ktlint/detekt in a later gate).
Verification gates: remote gate = CI run on the fix commit (verdict recorded in VERIFICATION.md Gate 11). Cascade so far: license hang → wrapper 404 → STL mismatch → Kotlin interop → (next unknown).

D-012 — GPT consultation round 2 verdict encoded (2026-10-31)
Evidence: full reply verbatim at `docs/orchestration/gpt_round2_reply.md` (extracted via CDP DOM innerText after completion marker "Đã hoàn tất phản hồi"; body 41,691→51,776 chars; brief was `docs/orchestration/gpt_round2_brief.md` @ `6f6fb49`, 3,717 chars, sent via composer bottom-viewport ProseMirror + nút "Gửi").
GPT independently verified on GitHub: runs 41178223381 + 41179251125 GREEN; artifact exists @ db65a0f (~1.93 MB). Did NOT re-extract the APK — APK-level evidence rests on Hermes's own checks (VERIFICATION.md).
Decisions encoded (Hermes-verified against repo before adoption):
- VERDICT: CONDITIONAL GO. Continue bounded implementation/review loop; escalate only blockers/authorization/major-architecture/milestones.
- CI-H (bounded task, does NOT block G2): replace `yes | timeout 300 ... || true` in ci.yml with explicit exit-code handling (SIGPIPE of yes ≠ sdkmanager failure); add 4 gates — APK/AAB packaging (16 KB in ELF PT_LOAD AND ZIP entry + 16 KB page-size runtime), manifest/security (exported components, debuggable, signing, merged manifest), build reproducibility (pinned toolchain, wrapper verification, dependency checksums), runtime (installable, JNI loadable, stream open/stop no crash).
- DC-002A (created): ACCEPT snapshot structure 8→12; REJECT algoLatencyMs semantics — retuneMs is control smoothing, NOT signal delay; kChunk=512 proves no 512-frame latency; kBaseDelay/kShiftWindow make real latency read-head-position dependent; TD-PSOLA will change it. Required: remove/relabel the misleading metric; real DSP latency comes from reference-signal measurement; add callback p99 (or histogram), deadline utilization, xrun=0 vs unsupported-xrun distinction, missingFrames as rate not cumulative, input starvation separated from output xrun.
- DC-003 REVISE (card rewritten): Flaw A — engine has NO input callback (input_->read() inside output callback; verified AudioEngine.cpp:29-44 — matches GPT's finding); Flaw B — two independent frame counters share no timing origin (Δt=(F_in−F_out)/f_s only valid after mapping to one time base); Flaw C — full-scale click + fixed 0.5 threshold unsafe/unstable. Approved design: low-amplitude identifiable signal + normalized cross-correlation + shared time base; TWO measurement classes (diagnostic acoustic loopback speaker→mic ≠ latency gate; authoritative end-to-end with common reference clock / calibrated loopback = Live gate evidence); INVALID_MEASUREMENT fail-closed; timeout + amplitude limit + routing control; measurement mode never during a singing session.
- G1 thresholds demoted: <12 ms (S) / <20 ms (A) medians = research targets for suitable paths, NOT default release gates. Live DSP gate stays: p95 ≤ 50 ms (S-series) / ≤ 70 ms (A-series) + perceptual check, approved only after real-hardware measurement. `g1_measurement_plan.md` must be updated accordingly.
- G2: GO on host in parallel (D0 golden baseline+corpus → D1 adaptive YIN+voicing → D2 tracking+hysteresis → D3 clean-room TD-PSOLA → D4 end-to-end verification). No pYIN/neural/key-detection in the critical path now. D0/D1 evidence = round-3 prerequisite.
- Stream modes: record ACTUAL granted stream properties (Shared mode is what the code requests today — GPT confirmed from source) before any Exclusive comparison.
- Autonomy: continue without routine owner prompts. Round 3 = DC-002A verification + revised DC-003 design + D0/D1 benchmark evidence (commits, reproducible results, failures, risks).
Verification: GPT's Flaw A re-checked against repo (AudioEngine.cpp — input builder has no data callback, input read inside output callback: TRUE); algoLatency formula present at db65a0f AudioEngine.cpp (TRUE — semantics rejected is a fair reading: retuneMs is a smoothing parameter); ci.yml `|| true` line confirmed present.
Hermes corrections to the reply record: none material. GPT's "did not re-extract APK" honesty preserved (APK gates remain Hermes-verified evidence until CI-H adds automated packaging gates).

---

Audit Trail Integrity Check (per `references/autonomy-directive.md` — verified 2026-10-29)
- No `.env` or secret committed (`.gitignore` covers `.idea/`, `local.properties`, `**/build/`).
- No `.devin/` files created (skill `devin-cli` pitfall: writing to `.devin/` triggers ForceAsk and kills session; prevented by `.gitignore` + no agent writes to `.devin/`).
- No fabricated CI results (local `cmake`/`gcc` unavailable → BLOCKED honestly documented; remote CI not yet triggered → no claim made).
- Every decision (D-001 to D-006) has a corresponding artifact file or planned verification step.
- The control plane (`.orchestrator/`) is NOT inside `.gitignore` (it is tracked) — this ensures audit artifacts survive git operations and are visible to future sessions.

D-013 — DC-002A landed + G1 plan REV 2 + G2 pipeline advance (2026-10-31)
Evidence: commits 615bf8a + a58bcd6 (Devin DC-002A + Hermes mechanical const-fix), CI cascade 41181595093 FAIL @ 615bf8a (const-correctness on out-stream pointer — real error, caught by CI as designed) → 41181759477 GREEN (both jobs + artifact 1.93 MB). APK re-verified: 16 KB PT_LOAD PASS ×6 .so ×2 ABIs; exactly 1 permission RECORD_AUDIO; debuggable=true (debug variant). VERIFICATION.md Gate 12; G0 audit #14 → IMPLEMENTED + SEMANTICS FIXED.
- DC-002A satisfies GPT round-2 D-012 fully: snapshot 16 slots — retuneMs honestly labeled "control" (NOT latency); NEW cbP99Ms (16-bucket histogram 2 ms buckets, single-writer relaxed), xrunValid (1=counted 0=unsupported → UI "xruns unsupported"), missFramesPerSec + shortReadPerSec (delta-based in snapshot(), no callback changes beyond removal of XRunCount); getXRunCount() moved OUT of the RT callback into snapshot() under controlMutex_ (UI thread); getStreamInfo() JNI returns GRANTED (not requested) stream properties for honest Shared-vs-Exclusive baselines. NOTHING in the engine claims algorithm latency anymore.
- G1 measurement plan REV 2 committed (80e39fc): two measurement classes (Class 1 diagnostic acoustic loopback ≠ Class 2 authoritative fixture), release gate p95 ≤ 50 ms (S) / ≤ 70 ms (A) + perceptual check, 12/20 ms medians demoted to research targets.
- DC-003 REV 2 card written per GPT-approved design (correlation-based, 2 classes, fail-closed): scratch/dautotune-cards/dc-003-loopback-ping-REV2.md. NOT dispatched (sequence: G2-D0 → CI-H → DC-003 REV2).
- G2-D0 DISPATCHED (Devin, background): card scratch/dautotune-cards/g2-d0-golden-baseline.md — deterministic synthetic corpus + metrics + D0_BASELINE.md in native-tests/ only; PitchCorrector READ-ONLY; CI-native-dsp verification; findings reported NOT fixed (D1 scope).
- G2-D1 card PREPARED (scratch/dautotune-cards/g2-d1-adaptive-yin.md): adaptive-window YIN + V/UV decision; dispatches after D0 lands. Design informed by code read: kDetectWindow=512 @ kDownsample=4 → effective 128-sample window ≈ 1 period at 110 Hz (under-resolved at low F0); kCompare=256; first-trough selection with 0.16/0.27 thresholds.
Rationale: GPT round-2 sequencing (D0 → D1 → ...), all cards respect scope isolation and the .devin/ no-write rule (DC-002 attempt 1 lesson).

D-014 — GPT round-3 verdict: GO D3 conditional; D2 test extension; real voice before G3 (2026-10-31)
Evidence: brief sent via Chrome debug :9222 thread 6ae57dbb (same thread as round-1/2; canvas writing-block insertion — composer confirmed by position/parent-chain BEFORE send, no canvas incident). Reply archived docs/orchestration/gpt_round3_reply.md (2177 chars, canvas doc "GPT ROUND-3 VERDICT").
- D1 ACCEPTED (adaptive YIN 3-pass + bounded prediction + strict temporal confirmation; NO pYIN/neural without comparative evidence).
- D2 ACCEPTED WITH TEST EXTENSION: keep the tracking implementation; ADD tests for staccato notes, scale-boundary vibrato, 300–1200-cent intervals; measure applied F0, target-note transitions, correction ratio (not only reported detectedHz); add target-note hysteresis ONLY IF tests expose target chatter.
- D3 GO: clean-room TD-PSOLA, preserve the existing audio engine interface. Mandatory gates: corrected-output F0, formant retention, discontinuities, transient handling, actual DSP signal delay, realtime safety.
- D4 AFTER D3: full-chain host verification with reproducible golden signals @ 44.1k + 48k, baseline vs new shifter under identical conditions; reject regressions that improve pitch accuracy at the expense of unacceptable timbre/artefacts/latency.
- REAL VOICE BEFORE G3 IMPLEMENTATION: small license-vetted VocalSet subset + consented recordings; objective + blind-listening evidence; do not mistake synthetic-corpus success for commercial vocal quality.
- G1 REMAINS BLOCKED_HARDWARE (device latency/routing/16KB runtime/thermal need real Android testing; diagnostic acoustic loopback is NOT authoritative mic-to-headphone latency evidence).
- HERMES CONTINUOUS LOOP reaffirmed: Devin implements each bounded card; Hermes independently reviews diff/tests/artifacts/CI; rejected work returns to Devin; ask GPT at architectural/quality milestones only.
- Next GPT consultation: after D3 AND D4 (corrected-output F0, artefact examples, formant comparison, actual signal-delay measurements, CI status, real-voice evidence if available).
- DO NOT rewrite Android UI before DSP quality results; do NOT claim Live certification or PRODUCT_COMPLETE.
Rationale: round-3 sent exactly per the round-2 requested evidence (DC-002A verification + D0/D1 golden evidence + DC-003 REV2 design); verdict encodes the next 4 gates and the evidence-priority order.

D-015 — GPT round-4 verdict: G2 HOST-VERIFIED PASS / G3 PREPARATION GO / COMMERCIAL RELEASE BLOCKED (2026-10-31, docs/orchestration/gpt_round4_reply.md):
- G2 ACCEPTED: D0-D4 host-verified DSP evidence. Do NOT rewrite TD-PSOLA without a reproducible failure. RECONCILE the D4 p95 process-time discrepancy (GPT saw ~54.5-54.9 us vs the report's 19 us) before the next review. The 2.27 ms onset delay is NOT a total Live latency measurement (DSP-only; total requires G1 device data).
- REAL VOICE FIRST (next execution order): Devin implements a license-vetted vocal validation harness: >= 20 real singing clips, >= 4 singers, prefer VocalSet with documented CC BY attribution + owner-consented recordings when available. Objective output-F0, formant, pumping, artifact measurements + minimum 3-person blind listening smoke test.
- G3 UX PREPARATION GO: Hermes may design UI architecture + interaction contracts NOW; full Compose implementation gated on real-voice smoke-test PASS; do NOT wait for Samsung hardware to begin UI implementation after this gate.
- DSP QUALITY FOLLOW-UP: investigate the 3.5-4.2 dB envelope pumping (steady-state vocal segments; quantify modulation; correct audible defects without regressing F0/formants/transients/latency). Do not optimize CPU on host timing alone.
- G1 REMAINS BLOCKED_HARDWARE (Samsung A+S series, wired/USB audio, independent latency verification, thermal, route-change, 16 KB native runtime validation).
- HERMES AUTONOMY: Devin sole coding agent; Hermes independently verifies each bounded task; owner approval required for commercial/legal/spending/production actions.
- Next execution order: real-vocal harness -> vocal-quality + pumping verification -> G3 Compose after PASS -> G1 device certification when hardware arrives -> G4 release candidate.
- Next GPT review: real-vocal measurements + blind listening results + pumping root-cause evidence + G3 readiness decision. Never claim VOCAL-QUALITY VERIFIED / LIVE CERTIFIED / PRODUCT_COMPLETE prematurely.

D-016 — D6 scope: fix the 2 real-voice defects + octave tail; pumping fix NOT dispatched (2026-10-31, card `g2-d6-realvoice-fix.md`, base `b7bd4a1`):
- Evidence: CI 41199096037 RED @ e70719c — 3 failures (R2 F1 worst 884 Hz, R2 F2 482 Hz on 217 sustained windows; R4 5/432 amplified boundaries, worst 11.53×); local Windows repro identical (EXIT=1). R3 pump excess +0.03 dB on real voice → the D-015 trigger (>3 dB input-corrected) NOT fired → no engine pumping fix; round-4's 3.5–4.2 dB pumping question closed as a synthetic-signal artifact.
- Decision: D6 = (1) detector sub-octave lock — female-only ~1200¢ tail (f1/f3 p95) with output audibly an octave low in those windows; (2) R2 formant — per-window diagnosis first (engine damage vs LPC measurement noise on high-F0 female frames), fix the right side, never loosen the gate silently; (3) R4 onset amplification — root-cause the soft-onset steps amplified up to 11.53× and fix without regressing the D3 transient-preservation + click-free batteries. NEW R1 gate: 0 windows ≥600¢ (the octave signature) across the whole corpus.
- Rationale: fix by evidence, not by gate adjustment; full regression suite (D0–D5) must stay green; listening pack v1 kept for the owner's listening of the current state, regenerated after D6.

D-017 — D6 landing accepted; next-order = blind listening → G3; R4 residual documented (2026-10-31, commit `bf6c3e9`):
- Evidence: CI 41206064557 GREEN both jobs on `bf6c3e9`; orchestrator local re-run 10/10 ctest PASS; before→after on the real corpus: R1 tail 0≥600¢, R2 worst 41/31 Hz (gate unchanged ±60), R4 0/432 worst 1.17×, R3 +0.03 dB, R5 3.75 ms.
- Decision: accept the D6 landing as the real-voice defect closure; the instrumented diagnosis (measureF0 cap artifact vs real detector octave-UP/DOWN locks; R4 teleport) is authoritative over the initial hypotheses. R4 residual worst 1.17× documented; a tighter CI bound (≤1.1×) goes to GPT round-5 as a question rather than asserted unilaterally. Next-order sent to GPT: blind listening (3 listeners) → G3 Compose implementation → G1 device smoke.
- Rationale: gates were never loosened (R2 ±60 Hz unchanged; new R1 gate ADDED); every fix is evidence-driven; questions to GPT keep the review protocol honest.



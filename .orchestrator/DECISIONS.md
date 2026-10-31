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

---

Audit Trail Integrity Check (per `references/autonomy-directive.md` — verified 2026-10-29)
- No `.env` or secret committed (`.gitignore` covers `.idea/`, `local.properties`, `**/build/`).
- No `.devin/` files created (skill `devin-cli` pitfall: writing to `.devin/` triggers ForceAsk and kills session; prevented by `.gitignore` + no agent writes to `.devin/`).
- No fabricated CI results (local `cmake`/`gcc` unavailable → BLOCKED honestly documented; remote CI not yet triggered → no claim made).
- Every decision (D-001 to D-006) has a corresponding artifact file or planned verification step.
- The control plane (`.orchestrator/`) is NOT inside `.gitignore` (it is tracked) — this ensures audit artifacts survive git operations and are visible to future sessions.

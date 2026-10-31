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

---

Audit Trail Integrity Check (per `references/autonomy-directive.md` — verified 2026-10-29)
- No `.env` or secret committed (`.gitignore` covers `.idea/`, `local.properties`, `**/build/`).
- No `.devin/` files created (skill `devin-cli` pitfall: writing to `.devin/` triggers ForceAsk and kills session; prevented by `.gitignore` + no agent writes to `.devin/`).
- No fabricated CI results (local `cmake`/`gcc` unavailable → BLOCKED honestly documented; remote CI not yet triggered → no claim made).
- Every decision (D-001 to D-006) has a corresponding artifact file or planned verification step.
- The control plane (`.orchestrator/`) is NOT inside `.gitignore` (it is tracked) — this ensures audit artifacts survive git operations and are visible to future sessions.

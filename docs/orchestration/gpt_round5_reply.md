## GPT Round-5 Reply — D-Autotune (received 2026-10-31 via ChatGPT thread `6ae57dbb`)

**Overall: D6 ACCEPTED / OBJECTIVE VOCAL GATES PASS / HUMAN LISTENING PENDING / G3 CONDITIONAL GO**

GPT verified the repository directly at `5e381ee` (D6 code, R1–R5 report, listening pack generator) and both CI workflows `41206064557` + `41207486610` SUCCESS. It independently caught the listening-key exposure and the pair-distribution skew before we flagged them.

### 1. D6 — ACCEPT, with one correction
- Accept `bf6c3e9` and its verified CI as closure of the reproduced octave-lock, formant-measurement and onset-discontinuity defects; preserve ALL existing regression gates.
- **Correction:** do NOT attribute the remaining 28–67¢ per-singer R1 p95 to "reference-F0 disagreement" without evidence — R1 out-vs-snap-target compares output F0 against the **segment-median INTERNAL target**, not the annotation. Order: inspect the worst windows using time-aligned output F0 + instantaneous target F0 + external annotations. No DSP rewrite if results prove expected behavior.
- Record the detector coverage change **87.3% → 86.0%** (−1.3pp) even though the catastrophic octave errors are gone.
- R3 closure holds only for the 24 straight long-tone segments tested — not a global no-pumping claim.

### 2. Order — blind listening BEFORE the Compose rewrite (round-4 order kept)
Numbers being good does not replace perception. Parallel work allowed: G3 architecture prep, UI test plans, Samsung G1 plan, corpus expansion. No full MainActivity→Compose rewrite yet.

### 3. R4 gate — keep 1.25× hard, no tightening
The code's rule is ratio AND absolute tolerance (1.25×input + 0.01) — important near silence. Add a **WARNING above 1.10×** (nonblocking) with amplitude, clip, timestamp. Tighten only with real-voice + listening evidence.

### 4. Blind listening — 8 pairs OK, but fix sampling + key secrecy (GPT-caught defects)
- `docs/corpus/listening_key.csv` is committed **publicly**; pair distribution skewed: f1 5, f3 1, m1 1, m3 1 of 8.
- Requirements: **2 pairs per singer (1 long-tone + 1 scales) = 8 main pairs + 2 hidden A/A controls**; fresh private randomization; listeners receive encoded WAVs only; the NEW key must NOT be committed before ratings are frozen; freeze all 3 listeners' ratings before opening the key; the old key is in git history so a NEW pack with a different order is required.
- PASS gate: corrected median naturalness ≥3/5; no repeated critical artifacts; no pair where all 3 raters fail a corrected sample unexplained. 3×8 = perceptual smoke test only — not a statistical superiority claim.

### 5. Corpus — v1 sufficient for the G3 gate, NOT for G4
Before G4: ≥8 independent singers, ≥40 clips, straight + vibrato + falsetto/breathy; vocal-register coverage (male low/high, female mid/high); modes Natural Tune + Hard Tune; metrics F0, V/UV, formant, artifact, pumping, subjective. The 86.0% real-voice detector coverage must be **classified** (silence / transition / breath / genuinely missed) — never substitute the 97.8% synthetic figure. If VocalSet lacks samples in a technique, only consented or clearly-licensed recordings; no synthetic data counted as new singers.

### Immediate sequence (GPT-ordered)
1. R1 residual-window diagnosis.
2. Corrected blind-listening pack + 3-person human evaluation.
3. If PASS → G3 Compose implementation.
4. In parallel: corpus expansion + G1 device-test prep.
5. Before G4: device certification, expanded vocal evidence, commercial/legal release gates.

**Next GPT review**: actual 3-listener results + R1 residual diagnosis + G3 readiness evidence. Do not claim human-listening PASS, live certification or commercial readiness before verification.

**Conclusion (GPT)**: milestone from synthetic-only to real-voice discovery + fix; do not touch the engine again without a reproduced bug; finish the blind listening, then Devin implements Compose while preparing Samsung device tests — the shortest path to a sellable product without trading gate rigor.

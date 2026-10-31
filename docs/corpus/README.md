# Real-voice validation corpus — schema (card G2-D5)

License-vetted singing clips used by the `d5_realvoice` ctest target
(batteries R1–R5) and the `d5_listening_pack` blind-test pack generator.
**Only this schema document and the committed templates are landed by the
D5 task.** The audio (`clips/`), annotations and `manifest.jsonl` are
committed afterwards by the orchestrator; until then every real-voice
battery reports `SKIP — corpus absent (0 clips)` and the CI gate stays
green.

## Directory layout

```
docs/corpus/
  README.md              this file (schema, licenses, selection rules)
  manifest.jsonl         one flat JSON object per clip (see below)
  clips/<clip_id>.wav    16-bit PCM mono, 44100 or 48000 Hz, <= 15.0 s
  annotations/<clip_id>.csv   per-note reference F0 (see below)
  listening_key_v2.csv   PRIVATE A/B role key (written by the v2 pack
                         generator on each run; .gitignore'd — committed
                         only AFTER the raters' sheets are frozen)
  listening_sheet.md     3-person blind rating sheet template
  listening_pack/        GENERATED randomized A/B WAVs (gitignored,
                         regenerated — never committed)
```

## `manifest.jsonl`

One JSON object per line, **flat** (no nesting), `#` lines and blank lines
ignored. Required fields:

| field | type | rule |
|---|---|---|
| `clip_id` | string | unique slug, e.g. `"vocalset_f1_straight_long_a3"` |
| `file` | string | WAV path relative to `docs/corpus/` (must start `clips/`; `..` and absolute paths rejected) |
| `sha256` | string | 64-char lowercase hex of the WAV bytes — the loader refuses the clip on mismatch |
| `fs` | int | `44100` or `48000`; must equal the WAV header rate |
| `singer` | string | anonymized singer id (`"f1".."f10"`, `"m1".."m10"` for VocalSet) |
| `context` | string | `long_tones` \| `scales` \| `arpeggios` \| `songs` \| `exercises` — R3 only scores `long_tones` segments |
| `technique` | string | free label: `straight` \| `vibrato` \| `belt` \| `lip_trill` \| `trillo` \| `inhaled` \| `spoken` \| `messa_di_voce` \| … |
| `vowel` | string | `a` \| `e` \| `i` \| `o` \| `u` \| `na` (songs) \| `none` |
| `source` | string | `vocalset` \| `owner` \| `other:<desc>` — drives attribution checks |
| `annotation` | string \| `null` | `annotations/<clip_id>.csv` relative to `docs/corpus/`, or `null` (clip excluded from R1–R4) |

Example:

```json
{"clip_id":"vocalset_f1_straight_long_a","file":"clips/vocalset_f1_straight_long_a.wav","sha256":"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad","fs":44100,"singer":"f1","context":"long_tones","technique":"straight","vowel":"a","source":"vocalset","annotation":"annotations/vocalset_f1_straight_long_a.csv"}
{"clip_id":"owner_atton_song_1","file":"clips/owner_atton_song_1.wav","sha256":"…","fs":48000,"singer":"owner1","context":"songs","technique":"vibrato","vowel":"na","source":"owner","annotation":null}
```

## `annotations/<clip_id>.csv`

Per-note reference-F0 segments (Annotated-VocalSet format adopted):

```csv
t_start_s,t_end_s,f0_hz,midi
0.512,1.489,220.00,57
1.704,2.631,246.94,59
```

Rules enforced by the loader: rows strictly time-ordered and
non-overlapping; `t_end > t_start`; `f0_hz > 0` (unvoiced regions are
simply absent, not `f0_hz = 0`); `midi` is the perceived note label used
as the correction target in R1. The header line is tolerated.

## WAV rules (enforced by `d5corpus::readWav16`)

- RIFF/WAVE, PCM format 1, **mono**, **16-bit**, 44100 or 48000 Hz.
- Duration **≤ 15.0 s** (the gate rejects longer files — trim before
  committing).
- Peak level: keep ≤ −1 dBFS so the output limiter never dominates the
  measurement.

## Corpus composition mandate (GPT round-4, D-015)

- **≥ 20 clips, ≥ 4 distinct singers.**
- Include `long_tones` clips (≥ 5, ideally covering both rate classes and
  several vowels) — R3 pumping is only scored on `long_tones` segments
  ≥ 0.5 s.
- Prefer **VocalSet** (see licenses) + owner-consented recordings.
- Every clip's SHA-256 goes in the manifest — the loader hard-fails on
  mismatch so real evidence cannot silently degrade.

## Licenses (verified against Zenodo/NIAID by Hermes — do not re-verify)

- **VocalSet (audio)** — Zenodo `1193957` (+`1442513`), 20 professional
  singers, 10.1 h, **CC BY 4.0**. Redistribution with attribution is
  permitted. Cite: J. Wilkins, P. Seetharaman, A. Wahl, B. Pardo,
  "VocalSet: A Singing Voice Dataset," ISMIR 2018.
- **Annotated-VocalSet** (per-note F0/onset/offset) — Zenodo `7061506`,
  Faghih & Timoney, *Appl. Sci.* 12(18):9257 (2022), **CC BY 4.0**.
- Owner-consented recordings: `source:"owner"`, consent recorded by the
  orchestrator outside this repo.

Attribution bookkeeping for vendored audio lives in `THIRD_PARTY.md`
(owned by Hermes — D5 does not touch it). Every committed clip must have
its attribution row there before the corpus commit lands.

## Validation

The batteries validate the corpus every run — malformed manifest lines,
SHA-256 mismatches, non-schema WAVs and bad annotation rows each print a
loud error and fail `d5_realvoice`. Run locally with:

```bash
cmake --build .local-test-build && ctest --test-dir .local-test-build -R d5_
```

## Blind-listening procedure (BLOCKED_HUMAN — the 3-person smoke test)

1. `ctest --test-dir <build> -R d5_listening_pack` (or run the binary)
   renders `listening_pack/pair_NN_{A,B}.wav` — 10 randomized, RMS-matched
   A/B pairs (v2, G2-D8): 8 main pairs balanced 2 per singer (1
   `long_tones` + 1 `scales` each) plus 2 hidden A/A control pairs
   (byte-identical audio in both slots — rater-reliability checks; raters
   are not told which). The PRIVATE role key is rewritten at
   `listening_key_v2.csv` (.gitignore'd — auditors only, and only after
   the raters' sheets are frozen). The v1 `listening_key.csv` was
   committed publicly and is VOID: the v2 ordering is re-randomized under
   a fresh seed, so the old key predicts nothing.
2. Each of ≥3 raters scores every pair on `listening_sheet.md`
   (naturalness 1–5, audible artifacts y/n, A/B preference).
3. Smoke-test PASS criterion (round-4 bar): no rater reports systematic
   artifacts on corrected outputs; median naturalness of `output` ≥ 3;
   no unanimous "corrected sounds wrong" verdict on any pair.


---

## Orchestrator prep notes — corpus v1 (committed by Hermes after the D5 schema landed)

Data: VocalSet v1.1 (Zenodo 1193957, CC BY 4.0) subset + Annotated-VocalSet
(Zenodo 7061507, CC BY 4.0) per-note F0 annotations. 20 clips, 4 singers
(f1, f3, m1, m3), techniques `straight` only, contexts `long_tones` (3 clips per
singer: 2 long_tones vowels + 3 scales vowels — 5 per singer, 20 total).

Processing applied before commit (documented honestly):
- 6 male `scales` clips exceeded the 15 s schema bound (15.69–19.53 s) — trimmed
  to exactly 15.0 s from t=0 (no time shift; annotation rows beyond 15 s dropped
  or clipped to t_end=15.0 — counts per file in the source JSON, 6 clips).
- Peak check: all 20 clips ≤ −1 dBFS natively (max −2.1 dBFS) — no scaling applied.
- WAV format verified: RIFF/WAVE PCM format 1, mono, 16-bit, 44100 Hz, all 20.
- Annotations converted from the Annotated-VocalSet 17-column format to the
  4-column schema: `t_start_s,t_end_s` verbatim; `f0_hz` = the segment MEDIAN F0
  (column 7 of the source); `midi` = the source MIDI note number (column 15).
  Only `Sound` segments retained (Rest/Transition rows dropped by rule).
- SHA-256 in the manifest is of the FINAL committed bytes (post-trim).

Selection rationale: `straight` technique isolates pitch correction without
ornament (vibrato/belt/breathy stress-test the detector, not the corrector —
they are a future expansion); `long_tones` give ≥2 sustained segments per singer
for the R3 envelope-pump battery; `scales` give 14–17 annotated note boundaries
per clip for R1/R2. Male scales clipped at 15 s still contain ≥14 of 17 notes.

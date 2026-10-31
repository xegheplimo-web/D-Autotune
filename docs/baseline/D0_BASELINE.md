# D0 — DSP baseline + golden corpus

Card G2-D0 (GPT round-2 D-012 sequence step 1). This is the **measurement
floor**: the CURRENT `PitchCorrector` detector, unchanged, on a synthetic
golden corpus. **D1 must beat these numbers on the same harness** — same
corpus, same scoring, same CI job.

Source of numbers: CI run `41183129226`, `native-dsp` job (ubuntu-latest,
GCC `-O2`), commit `20e83a8`. Printed verbatim by the `d0_baseline` ctest
(also re-run as a POST_BUILD step so the table lands in the build log —
ctest only shows output on failure). Do not paste numbers from anywhere
else; regenerate via CI.

## Methodology

- **Corpus**: generated at test time by `native-tests/corpus_gen.cpp`; no
  binary audio committed. Every item derives from seed `0xd0ba5eed` through
  an xorshift64* PRNG, salted per (seed, sample rate, item name) so items
  are order-independent. FNV-1a hash of every item is asserted identical
  across two builds in the same run (byte-identical corpus). Hash equality
  is guaranteed within one toolchain; a different libm could shift bits —
  that does not affect the metric values at float precision.
- **Inventory** (per sample rate; both 48000 and 44100): 6 pure sines
  (110/220/329.63/440/523.25/784 Hz), 24 harmonic stacks (k ∈ {1,2,5,8}
  harmonics, 1/j rolloff, random fixed phases), 4 vibrato contours
  (f0=220, depth {2%,5%} × rate {5,7} Hz, 6 harmonics), 1 portamento
  (linear ramps 220→330→440), 1 voiced/unvoiced mixture (vowel stack vs
  white noise and 5 kHz band-limited hiss, 60 ms raised-cosine
  transitions, boundary at crossfade midpoint), 5 SNR items (f0=220,
  white noise at 30/20/10/5/0 dB), 1 equal-amplitude 110+220+440 octave
  trap (ground truth 110 Hz), 1 formant-shaped vibrato vowel (/a/ F1–F3
  envelope, fundamental attenuated — a missing-fundamental case).
  43 items per rate.
- **Ground truth**: per evaluation chunk, F0 at the chunk's center time
  (0 = unvoiced). For vibrato/portamento items this is the instantaneous
  F0, so tracking lag shows up as cents error — intentionally.
- **Evaluation**: one `PitchCorrector` per item, `setParameters(0, 0, 0.9,
  35, 0.1)` (C major, bench/app-typical), input fed in `kChunk = 512`
  sample calls, interleaved stereo output (2 ch), exactly like
  `AudioEngine::onAudioReady`. `detectedHz()`/`voiced()` are read after
  every call — the detector itself only updates every 512 samples after a
  2048-sample window fill, so the first 5 chunks (2560 samples) are
  excluded from scoring.
- **Metric definitions** (`native-tests/metrics.cpp`):
  - `det%` = voiced-ground-truth chunks where `detectedHz() > 0`.
  - `med/p95 cents` = |1200·log2(est/gt)| over voiced-GT chunks with a
    valid estimate (chunks with no estimate count in `det%`, not here).
  - `oct%` = fraction of cents-scored chunks with |err| within ±150 cents
    of 1200 or 600 (octave / half-octave class errors).
  - `vuv%` = (tvEv + tuEu) / scored, using the `voiced()` flag (which is
    gated by confidence ≥ 0.73 and two consecutive agreeing estimates).
  - `us/call` = `steady_clock` around each `process()` call, scored
    chunks only. Timings on a shared CI runner are indicative, not a gate.

## Results — CI run 41183129226

Corpus hashes: `0x464218dd93e6ed1f` @ 48000, `0x69b5e58531723182` @ 44100
(43 items each).

### sr = 48000

| item                     |    sr | scored | det% | med cents | p95 cents | oct% | vuv% | med us/call | p95 us/call |
|--------------------------|------:|-------:|-----:|--------:|--------:|-----:|-----:|------------:|------------:|
| sine_A2_110              | 48000 |   88 | 100.0 |     0.1 |     0.1 |  0.00 | 100.0 |     30.3 |     31.0 |
| sine_A3_220              | 48000 |   88 | 100.0 |     0.1 |     0.1 |  0.00 | 100.0 |     20.8 |     20.9 |
| sine_E4_329              | 48000 |   88 | 100.0 |     0.4 |     0.4 |  0.00 | 100.0 |     17.5 |     17.6 |
| sine_A4_440              | 48000 |   88 | 100.0 |     1.0 |     1.0 |  0.00 | 100.0 |     15.7 |     15.9 |
| sine_C5_523              | 48000 |   88 | 100.0 |     1.5 |     1.7 |  0.00 | 100.0 |     15.0 |     15.1 |
| sine_G5_784              | 48000 |   88 | 100.0 |     3.1 |     3.3 |  0.00 | 100.0 |     13.4 |     13.5 |
| harm1_A2_110             | 48000 |   88 | 100.0 |     0.1 |     0.1 |  0.00 | 100.0 |     30.4 |     30.8 |
| harm1_A3_220             | 48000 |   88 | 100.0 |     0.1 |     0.1 |  0.00 | 100.0 |     20.9 |     20.9 |
| harm1_E4_329             | 48000 |   88 | 100.0 |     0.4 |     0.4 |  0.00 | 100.0 |     17.5 |     17.7 |
| harm1_A4_440             | 48000 |   88 | 100.0 |     1.0 |     1.0 |  0.00 | 100.0 |     15.7 |     15.9 |
| harm1_C5_523             | 48000 |   88 | 100.0 |     1.5 |     1.7 |  0.00 | 100.0 |     15.0 |     18.8 |
| harm1_G5_784             | 48000 |   88 | 100.0 |     3.1 |     3.3 |  0.00 | 100.0 |     13.4 |     13.5 |
| harm2_A2_110             | 48000 |   88 | 100.0 |     0.1 |     0.1 |  0.00 | 100.0 |     31.3 |     31.7 |
| harm2_A3_220             | 48000 |   88 | 100.0 |     0.1 |     0.1 |  0.00 | 100.0 |     20.8 |     21.0 |
| harm2_E4_329             | 48000 |   88 | 100.0 |     0.4 |     0.4 |  0.00 | 100.0 |     17.5 |     17.6 |
| harm2_A4_440             | 48000 |   88 | 100.0 |     1.1 |     1.2 |  0.00 | 100.0 |     15.7 |     15.8 |
| harm2_C5_523             | 48000 |   88 | 100.0 |     1.5 |     1.7 |  0.00 | 100.0 |     15.0 |     15.1 |
| harm2_G5_784             | 48000 |   88 | 100.0 |     3.6 |     3.8 |  0.00 | 100.0 |     13.4 |     13.6 |
| harm5_A2_110             | 48000 |   88 | 100.0 |     0.1 |     0.1 |  0.00 | 100.0 |     31.2 |     31.4 |
| harm5_A3_220             | 48000 |   88 | 100.0 |     0.1 |     0.1 |  0.00 | 100.0 |     21.2 |     21.3 |
| harm5_E4_329             | 48000 |   88 | 100.0 |     0.5 |     0.6 |  0.00 | 100.0 |     17.5 |     17.6 |
| harm5_A4_440             | 48000 |   88 | 100.0 |     1.5 |     1.7 |  0.00 | 100.0 |     15.7 |     15.9 |
| harm5_C5_523             | 48000 |   88 | 100.0 |     1.2 |     1.4 |  0.00 | 100.0 |     15.0 |     15.1 |
| harm5_G5_784             | 48000 |   88 | 100.0 |     6.3 |     6.5 |  0.00 | 100.0 |     13.4 |     13.5 |
| harm8_A2_110             | 48000 |   88 | 100.0 |     0.1 |     0.1 |  0.00 | 100.0 |     31.3 |     31.8 |
| harm8_A3_220             | 48000 |   88 | 100.0 |     0.0 |     0.1 |  0.00 | 100.0 |     20.8 |     20.9 |
| harm8_E4_329             | 48000 |   88 | 100.0 |     0.8 |     0.9 |  0.00 | 100.0 |     17.5 |     17.6 |
| harm8_A4_440             | 48000 |   88 | 100.0 |     2.3 |     2.5 |  0.00 | 100.0 |     15.8 |     15.8 |
| harm8_C5_523             | 48000 |   88 | 100.0 |     0.7 |     0.9 |  0.00 | 100.0 |     15.0 |     15.6 |
| harm8_G5_784             | 48000 |   88 | 100.0 |    10.5 |    10.9 |  0.00 | 100.0 |     13.4 |     13.5 |
| vib_220_d02_f5           | 48000 |  107 | 100.0 |    17.8 |    25.7 |  0.00 | 100.0 |     21.1 |     21.5 |
| vib_220_d02_f7           | 48000 |  107 | 100.0 |    24.6 |    34.7 |  0.00 | 100.0 |     21.1 |     21.5 |
| vib_220_d05_f5           | 48000 |  107 | 100.0 |    44.2 |    64.0 |  0.00 | 100.0 |     21.5 |     29.3 |
| vib_220_d05_f7           | 48000 |  107 | 100.0 |    62.8 |    86.9 |  0.00 | 100.0 |     26.4 |     29.6 |
| porta_220_330_440        | 48000 |  130 | 100.0 |     1.6 |   112.3 |  0.00 | 100.0 |     16.8 |     27.6 |
| vuv_mix                  | 48000 |  154 |  96.0 |     0.1 |     0.1 |  0.00 |  92.2 |     21.3 |     39.2 |
| snr_30db                 | 48000 |   79 | 100.0 |     0.1 |     0.4 |  0.00 | 100.0 |     21.2 |     21.4 |
| snr_20db                 | 48000 |   79 | 100.0 |     0.4 |     1.4 |  0.00 | 100.0 |     21.2 |     21.3 |
| snr_10db                 | 48000 |   79 | 100.0 |     3.3 |     7.8 |  0.00 | 100.0 |     21.1 |     21.4 |
| snr_5db                  | 48000 |   79 |   0.0 |     0.0 |     0.0 |  0.00 |   0.0 |     37.9 |     38.2 |
| snr_0db                  | 48000 |   79 |   0.0 |     0.0 |     0.0 |  0.00 |   0.0 |     37.9 |     38.4 |
| octave_trap_110_220_440  | 48000 |   88 | 100.0 |     0.1 |     0.1 |  0.00 | 100.0 |     31.3 |     34.4 |
| formant_vib_a            | 48000 |  107 | 100.0 |    33.2 |    45.7 |  0.00 | 100.0 |     21.1 |     21.4 |
| **ALL @ 48000**          | 48000 | 3942 |  95.8 |     0.9 |    42.2 |  0.00 |  95.7 |     20.5 |     37.9 |

Confusion @48000: tvEv=3725 tvEu(FVo)=164 tuEv(FUV)=6 tuEu=47.
Timing @48000: median 20.5 µs, p95 37.9 µs per 512×2 call (~40.1 ns/sample).

### sr = 44100

| item                     |    sr | scored | det% | med cents | p95 cents | oct% | vuv% | med us/call | p95 us/call |
|--------------------------|------:|-------:|-----:|--------:|--------:|-----:|-----:|------------:|------------:|
| sine_A2_110              | 44100 |   81 | 100.0 |     0.1 |     0.1 |  0.00 | 100.0 |     28.6 |     28.9 |
| sine_A3_220              | 44100 |   81 | 100.0 |     0.3 |     0.4 |  0.00 | 100.0 |     20.2 |     20.3 |
| sine_E4_329              | 44100 |   81 | 100.0 |     0.3 |     0.4 |  0.00 | 100.0 |     16.9 |     17.0 |
| sine_A4_440              | 44100 |   81 | 100.0 |     1.4 |     1.5 |  0.00 | 100.0 |     15.4 |     15.5 |
| sine_C5_523              | 44100 |   81 | 100.0 |     1.9 |     2.1 |  0.00 | 100.0 |     14.6 |     14.7 |
| sine_G5_784              | 44100 |   81 | 100.0 |     4.3 |     4.6 |  0.00 | 100.0 |     13.2 |     13.3 |
| harm1_A2_110             | 44100 |   81 | 100.0 |     0.1 |     0.1 |  0.00 | 100.0 |     28.6 |     29.2 |
| harm1_A3_220             | 44100 |   81 | 100.0 |     0.3 |     0.4 |  0.00 | 100.0 |     20.2 |     20.3 |
| harm1_E4_329             | 44100 |   81 | 100.0 |     0.3 |     0.4 |  0.00 | 100.0 |     16.9 |     17.0 |
| harm1_A4_440             | 44100 |   81 | 100.0 |     1.4 |     1.5 |  0.00 | 100.0 |     15.4 |     15.5 |
| harm1_C5_523             | 44100 |   81 | 100.0 |     1.9 |     2.1 |  0.00 | 100.0 |     14.6 |     14.7 |
| harm1_G5_784             | 44100 |   81 | 100.0 |     4.3 |     4.6 |  0.00 | 100.0 |     13.2 |     13.3 |
| harm2_A2_110             | 44100 |   81 | 100.0 |     0.1 |     0.1 |  0.00 | 100.0 |     29.5 |     29.7 |
| harm2_A3_220             | 44100 |   81 | 100.0 |     0.3 |     0.4 |  0.00 | 100.0 |     20.2 |     20.3 |
| harm2_E4_329             | 44100 |   81 | 100.0 |     0.4 |     0.4 |  0.00 | 100.0 |     16.9 |     17.0 |
| harm2_A4_440             | 44100 |   81 | 100.0 |     1.4 |     1.6 |  0.00 | 100.0 |     15.4 |     15.4 |
| harm2_C5_523             | 44100 |   81 | 100.0 |     2.0 |     2.2 |  0.00 | 100.0 |     14.6 |     14.7 |
| harm2_G5_784             | 44100 |   81 | 100.0 |     4.4 |     4.8 |  0.00 | 100.0 |     13.2 |     13.4 |
| harm5_A2_110             | 44100 |   81 | 100.0 |     0.1 |     0.1 |  0.00 | 100.0 |     29.5 |     30.3 |
| harm5_A3_220             | 44100 |   81 | 100.0 |     0.4 |     0.5 |  0.00 | 100.0 |     27.9 |     28.0 |
| harm5_E4_329             | 44100 |   81 | 100.0 |     0.5 |     0.6 |  0.00 | 100.0 |     24.0 |     24.3 |
| harm5_A4_440             | 44100 |   81 | 100.0 |     1.5 |     1.7 |  0.00 | 100.0 |     15.4 |     15.5 |
| harm5_C5_523             | 44100 |   81 | 100.0 |     2.3 |     2.5 |  0.00 | 100.0 |     14.6 |     14.8 |
| harm5_G5_784             | 44100 |   81 | 100.0 |     5.2 |     5.6 |  0.00 | 100.0 |     13.2 |     13.4 |
| harm8_A2_110             | 44100 |   81 | 100.0 |     0.1 |     0.1 |  0.00 | 100.0 |     29.5 |     29.7 |
| harm8_A3_220             | 44100 |   81 | 100.0 |     0.5 |     0.5 |  0.00 | 100.0 |     20.2 |     20.3 |
| harm8_E4_329             | 44100 |   81 | 100.0 |     0.7 |     0.8 |  0.00 | 100.0 |     16.9 |     17.0 |
| harm8_A4_440             | 44100 |   81 | 100.0 |     1.8 |     2.0 |  0.00 | 100.0 |     15.4 |     15.4 |
| harm8_C5_523             | 44100 |   81 | 100.0 |     2.8 |     3.0 |  0.00 | 100.0 |     14.6 |     14.8 |
| harm8_G5_784             | 44100 |   81 | 100.0 |     6.4 |     6.9 |  0.00 | 100.0 |     13.2 |     13.3 |
| vib_220_d02_f5           | 44100 |   98 | 100.0 |    19.6 |    28.1 |  0.00 | 100.0 |     20.2 |     20.5 |
| vib_220_d02_f7           | 44100 |   98 | 100.0 |    26.6 |    37.5 |  0.00 | 100.0 |     20.2 |     20.5 |
| vib_220_d05_f5           | 44100 |   98 | 100.0 |    48.7 |    69.7 |  0.00 | 100.0 |     20.2 |     20.8 |
| vib_220_d05_f7           | 44100 |   98 | 100.0 |    67.4 |    94.4 |  0.00 | 100.0 |     19.7 |     20.9 |
| porta_220_330_440        | 44100 |  119 | 100.0 |     1.2 |   121.4 |  0.00 | 100.0 |     16.9 |     20.3 |
| vuv_mix                  | 44100 |  141 |  94.6 |     0.4 |     0.5 |  0.00 |  90.8 |     20.3 |     36.7 |
| snr_30db                 | 44100 |   72 | 100.0 |     0.4 |     0.8 |  0.00 | 100.0 |     20.2 |     20.4 |
| snr_20db                 | 44100 |   72 | 100.0 |     0.7 |     1.7 |  0.00 | 100.0 |     20.2 |     20.3 |
| snr_10db                 | 44100 |   72 | 100.0 |     2.1 |     6.9 |  0.00 | 100.0 |     20.2 |     20.3 |
| snr_5db                  | 44100 |   72 |   0.0 |     0.0 |     0.0 |  0.00 |   0.0 |     35.4 |     37.0 |
| snr_0db                  | 44100 |   72 |   0.0 |     0.0 |     0.0 |  0.00 |   0.0 |     35.4 |     35.5 |
| octave_trap_110_220_440  | 44100 |   81 | 100.0 |     0.1 |     0.1 |  0.00 | 100.0 |     29.6 |     30.0 |
| formant_vib_a            | 44100 |   98 | 100.0 |    35.2 |    49.4 |  0.00 | 100.0 |     20.2 |     20.6 |
| **ALL @ 44100**          | 44100 | 3621 |  95.8 |     1.3 |    45.6 |  0.00 |  95.7 |     19.8 |     35.4 |

Confusion @44100: tvEv=3422 tvEu(FVo)=151 tuEv(FUV)=6 tuEu=42.
Timing @44100: median 19.8 µs, p95 35.4 µs per 512×2 call (~38.6 ns/sample).

**GRAND TOTAL** (7563 scored chunks, both rates): det 95.8 %, median 1.0 ¢,
p95 43.8 ¢, octave errors 0.00 %, V/UV 95.7 %, call median 20.2 µs,
p95 35.4 µs.

## Findings (measured, not fixed — D1/D2 scope)

1. **Clean periodic input is essentially solved.** Sines and harmonic
   stacks: median ≤ ~3 ¢, worst case 10.5 ¢ at harm8_G5_784 (fixed-lag
   quantization at high F0). No octave errors anywhere — `oct% = 0.00`
   on every item including the equal-amplitude 110+220+440 trap; the
   first-trough selection handles it correctly on this corpus.
2. **Tracking lag dominates real-singing error.** Vibrato at d=2 % costs
   18–27 ¢ median; at d=5 %, 44–67 ¢ median (p95 up to 94 ¢). Portamento
   ramps: median 1.2–1.6 ¢ on the holds, p95 ~112–121 ¢ during the two
   150 ms ramps. The 2048-sample window + hop cadence lags instantaneous
   F0 by roughly half a window — this, not accuracy on steady tones, is
   the gap D1/D2 must close.
3. **Hard SNR cliff between 10 dB and 5 dB.** 10 dB still locks (2–3 ¢);
   at 5 dB and 0 dB detection rate is exactly 0 — the CMND trough never
   reaches the 0.16/0.27 thresholds, so every voiced frame is reported
   unvoiced (vuv% = 0 on those rows). The detector is deaf below ~10 dB
   SNR, not merely inaccurate.
4. **V/UV boundaries cost ~1–2 chunks each.** vuv_mix: det% 94.6–96.0,
   vuv% 90.8–92.2; aggregate confusion is tvEu=164/151 vs only
   tuEv=6/6 — misses at voiced onsets/transitions (stableCount gate +
   window straddling), essentially no false voicing on noise.
5. **Formant-shaped vowel** (fundamental attenuated, /a/ spectrum):
   33–35 ¢ median — the error is tracking lag on the 3 % vibrato, not
   octave confusion; det% stayed 100.
6. **Timing headroom is large.** Worst median ~31 µs per 512×2 call
   (low-F0 items, deeper lag scan), p95 ≤ ~39 µs vs a ~10.7 ms chunk
   deadline at 48 kHz — ~270× realtime headroom on the CI host. Detection
   runs once per 512-sample call post-warmup, so med/p95 already include
   the detector. µs/call scales inversely with F0 (fewer inner-loop lags
   to the first trough at high F0).

## Known limitations

- **No real vocal recordings.** Licensed sung-voice samples
  ("mẫu giọng hát được phép sử dụng") are DEFERRED: a verified-license
  source (e.g. a Mozilla Common Voice CC0 subset or equivalent) needs
  vetting before commit. The corpus is 100 % synthetic; the
  formant/vibrato items approximate singing but do not cover real glottal
  source, breath noise, or microphone channels.
- Ground truth on vibrato/portamento is instantaneous F0 while the
  detector reports a windowed estimate — part of the measured "error" is
  unavoidable lag under any windowed method; D1 should reduce it, D2 owns
  tracking/hysteresis behavior on transitions.
- Timing is from a shared CI runner — use it as an order-of-magnitude
  check, not a performance gate; device numbers come from G1 hardware.
- Evaluation cadence = 512 samples (the detector's own update rate); the
  V/UV confusion matrix inherits that granularity on segment boundaries.

## Reproduce

```bash
cmake -S native-tests -B .local-test-build -DCMAKE_BUILD_TYPE=Release
cmake --build .local-test-build   # table prints in build output (POST_BUILD)
ctest --test-dir .local-test-build --output-on-failure   # or: test d0_baseline
```

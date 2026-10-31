# Technical research (31 October 2026)

Evidence-based architecture selection:

1. Google Oboe supports high-performance Android audio using a unified C++ API, and its official documentation recommends callbacks for lowest-latency output. Input can be read non-blockingly from the output callback. https://github.com/google/oboe and https://android.googlesource.com/platform/external/oboe/+/HEAD/docs/FullGuide.md
2. Google Maven offers Oboe 1.11.0. Prefab connects an Android Studio CMake project with `oboe::oboe`. https://github.com/google/oboe/blob/main/docs/GettingStarted.md and https://github.com/google/oboe/releases
3. Requested low-latency streams do NOT imply guaranteed low latency on each phone. Sample-rate mismatches, accessories and device-specific behavior can add delay. https://android.git.googlesource.com/platform/external/oboe/+/HEAD/docs/FAQ.md
4. Android Gradle Plugin 8.13.2 supports compile SDK 36 and Gradle 8.13, JDK 17. https://developer.android.com/build/releases/agp-8-13-0-release-notes
5. New apps submitted to Google Play after 31 August 2026 generally require `targetSdk >= 36`. https://support.google.com/googleplay/android-developer/answer/11926878
6. Gradle wrapper checksums are published officially: https://gradle.org/release-checksums/ . The project ships an SHA256-verified first-use wrapper bootstrap instead of embedding downloaded binaries.

## Why not integrate MicDroid, MicUp, SnapTune or Windows DSP code?

- Android app + DSP are newly implemented. No migrated DSP code, no GPL/AGPL copyleft dependency included.
- Proven underlying platform dependencies (Oboe) are allowed, rather than rebuilding an Android audio driver.
- Prototype tuner: a coarse-to-fine difference-function pitch detector with normalized cumulative difference (YIN-family) plus two-head, crossfaded, variable-rate delay pitch shifter. This shifter is intentionally basic and MAY produce phasing, metallic sound, or octave errors on real singing. Acoustic production quality is unverified.

## Performance targets, NOT measured outcomes

- Foreground live-monitor operation, wired analog/USB headset only.
- Prefer 48 kHz native device rate, output stream callback; measure 44.1/48 kHz and other actual rates.
- Candidate acceptance criteria: end-to-end round-trip <= 40 ms on selected test devices (stretch, not a guarantee); >= 30 minutes without underrun/dropout; pitch accuracy (voiced, clean input) <= 20 cents median; no catastrophic octave errors in a labeled vocal corpus.
- Off-the-shelf Bluetooth typically adds unacceptable delay for live monitoring; not supported by the alpha UI.

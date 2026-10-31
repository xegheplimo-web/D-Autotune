# Release criteria / plan to commercial maturity

## P0: Functional prototype (this archive)
- [x] Native mic-to-headphones pipeline (source implemented; device not validated)
- [x] UI Key / Scale / Correction / Speed / Reverb
- [x] YIN-family pitch detector and basic granular crossfade pitch shifter
- [x] Host C++ deterministic DSP tests
- [x] Permission / foreground-only recording / wired-or-USB output gate
- [ ] Android SDK compile verification
- [ ] 3 distinct Samsung devices: wired headset routing, mic permissions, unplug recovery

## P1: Production-quality DSP (BLOCKING)
- [ ] Accurate pitch tracker on real voices (male, female, breathy, vibrato, noisy rooms)
- [ ] Formant-preserving, transient-aware pitch correction (current grain shifter may sound metallic)
- [ ] Robust note transition/hysteresis + scale/key presets
- [ ] Quality analysis: pitch error, vowel/formant drift, octave mistakes, artifacts
- [ ] Audio callback worst-case CPU and 16 KB page size compatibility on Android 15+

## P2: Device qualification (BLOCKING)
- [ ] Measure round-trip device latency with loopback/OboeTester (mic->DSP->headphones), not estimated from buffer size alone
- [ ] Measure A23 6GB, A24 6GB, A54 or actual owned Samsung models
- [ ] Verify headset unplug / interruptions / calls / screen lock / permission revocation
- [ ] Maintain explicit allowlist of tested devices; route others to future offline Studio mode

## P3: Commercial launch
- [ ] Record/export with user-controlled local files, robust privacy design
- [ ] Data safety/privacy policy, local audio handling disclosure
- [ ] Presets, onboarding, accessibility, multi-language
- [ ] Billing entitlement and fraud controls (if monetized); store signing/CI releases
- [ ] Trademark review of "Auto-Tune" in store-facing brand
- [ ] Beta + stability telemetry with opt-in and no raw voice uploads

**GO / NO-GO:** This is NOT ready for Play Store commercial release until P1 and P2 gates pass.

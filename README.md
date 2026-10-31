# D-Autotune — Native Android real-time pitch-correction prototype

**Status: ALPHA / source prototype — NOT commercial-ready or verified on a real Android phone.**

Created independently for a from-scratch Android Auto-Tune concept. The project includes a real native audio pipeline and working pitch detection/correction DSP, not just screens or a placeholder project.

## Included

- Android app (min Android 11 / API 30, target Android 16 / API 36), Kotlin UI, Android NDK C++17.
- Oboe 1.11.0 audio input/output: low-latency output callback, nonblocking microphone reads.
- Audio DSP: pitch detection (YIN-style normalized difference), scale quantization, time-domain pitch shifter, simple reverb and safety limiter.
- Select musical key (12 roots), Major/Minor/Chromatic, correction strength, retune speed and reverb.
- Foreground-only microphone permission; automatically stops when app is hidden or wired headset is disconnected; no network permission.
- Native C++ tests + GitHub CI template + Windows build scripts + docs.

**Important caveats:** The phase/grain pitch shifter is a first-pass algorithm that may sound artificial and is not formant-preserving. It currently requires a 48-ish kHz capable output/input float stream and wired/USB audio routing; older devices can fail to open. This archive has NOT been compiled against Android SDK or trialed on a Samsung device in the authoring environment.

## On Windows 11 — build in Android Studio

1. Extract the archive into a short local path such as `F:\D-Autotune`. Open the root folder with an up-to-date Android Studio using **JDK 17**.
2. Install **Android SDK Platform 36**, Android SDK build tools 36, **NDK** and **CMake** via SDK Manager. Gradle build refers to a CMake SDK version available through `externalNativeBuild`.
3. Sync Gradle. **The archive omits binary `gradle-wrapper.jar` intentionally**, but includes `gradlew.bat` and an HTTPS SHA256-verified downloader. To build from terminal:
   ```powershell
   cd F:\D-Autotune
   .\gradlew.bat :app:assembleDebug
   ```
   First build requires internet to fetch the pinned wrapper/Gradle/Android dependencies. If Android Studio Sync requires the wrapper before it can import the project, run the command once from PowerShell first.
4. Output (after a successful build): `app\build\outputs\apk\debug\app-debug.apk`.
5. Install on a Samsung Android 11+ phone. Connect **wired or USB headphones**, reduce volume, allow mic permission, select musical key and press **BẬT LIVE AUTOTUNE**.

**Never monitor through loudspeakers** (feedback can cause very loud audio). Prefer a headset with a microphone and direct audio output. The app auto-stops when moved to background.

## Run DSP tests without Android

Requires CMake 3.20+ and a C++17 compiler on Windows/Linux:

```powershell
.\scripts\test-dsp-windows.ps1
```

Or with portable CMake commands:

```bash
cmake -S native-tests -B .local-test-build -DCMAKE_BUILD_TYPE=Release
cmake --build .local-test-build
ctest --test-dir .local-test-build --output-on-failure
```

## Current file structure

```
D-Autotune/
  app/
    build.gradle.kts
    src/main/
      AndroidManifest.xml
      cpp/{PitchCorrector,AudioEngine,native-lib,CMakeLists.txt}
      java/com/dautotune/app/{MainActivity.kt,NativeAudio.kt}
      res/{values,drawable}
  native-tests/{CMakeLists.txt,test_dsp.cpp}
  gradle/wrapper/gradle-wrapper.properties
  gradlew, gradlew.bat (verified wrapper bootstrap)
  scripts/{ensure-wrapper.ps1,build-windows.ps1,test-dsp-windows.ps1}
  docs/{RESEARCH,THIRD_PARTY,ROADMAP,TEST_RESULTS}.md
  .github/workflows/ci.yml
  LICENSE, README.md
```

## Test evidence and limits

`docs/TEST_RESULTS.md` records actual host-side checks performed here. **No Android APK, phone latency or real vocal sound-quality tests were claimed or fabricated.** An Android Studio build on the user's own machine is the next hard gate before advancing DSP quality.

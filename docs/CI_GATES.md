# CI gates — D-Autotune

What each gate in `.github/workflows/ci.yml` asserts, what evidence it
prints, and what is deliberately NOT covered yet (GPT round-2, D-012).

## 1. `Install Android components` — fail-loud sdkmanager

`yes | timeout N sdkmanager <args> | tee <log>` under a `sdk_pipe()` bash
helper. Exit codes are classified via `PIPESTATUS` (GitHub runs `bash -e`
*without* `pipefail`, so a bare pipeline status alone cannot separate the
two sides):

| Signal | Meaning | Verdict |
|---|---|---|
| `PIPESTATUS[1]` = 0 | sdkmanager exited cleanly | OK |
| `PIPESTATUS[1]` = 124/137/143 | `timeout` fired or process killed | FAIL step |
| `PIPESTATUS[1]` = other non-zero | real sdkmanager error | FAIL step |
| `PIPESTATUS[0]` = 141 | `yes` SIGPIPE when sdkmanager closed stdin | expected, warning only if ≠ 0/141 |
| `PIPESTATUS[2]` ≠ 0 | `tee`/log capture failed | FAIL step |

Evidence printed: the per-stage `yes/sdkmanager/tee` exit codes plus the
tail of each sdkmanager log. The `--licenses` invocation additionally
requires the word "license" in its output, otherwise license acceptance is
not auditable and the step fails.

## 2. `Print resolved toolchain`

Prints the pins (`ndkVersion`, `cmake` `version`) from
`app/build.gradle.kts` and the `Pkg.Revision`/`Pkg.Desc` lines of the
installed `$ANDROID_HOME/ndk/27.0.12077973` and `cmake/3.22.1`
`source.properties`. Fails if either component is missing — a drift
between the Gradle pin and the installed component is caught here, not at
CMake configure time.

## 3. `Compile debug APK` — unchanged

`sh gradlew --no-daemon :app:assembleDebug` (gradlew is committed without
the exec bit).

## 4. `Verify Gradle wrapper JAR checksum` — build reproducibility

`gradle-wrapper.jar` is **not committed**. Bootstrap mechanism: `gradlew`
downloads it once from `gradle/gradle` git tag `v8.13.0`
(`raw.githubusercontent.com/gradle/gradle/v8.13.0/gradle/wrapper/gradle-wrapper.jar`)
and verifies it against the SHA-256 pinned in the `EXPECTED=` variable
inside `gradlew` itself (`81a82aae…7ae45f`). `gradlew.bat` does the same
via `scripts/ensure-wrapper.ps1`.

The gate re-verifies after the build: extracts `EXPECTED` from `gradlew`
at runtime (single source of truth, no duplicated constant), hashes the
jar the build actually used, and fails on mismatch. It also asserts
`distributionSha256Sum` remains pinned in `gradle-wrapper.properties`
(the wrapper validates the Gradle 8.13 dist zip against it).

## 5. `APK packaging gate (16 KiB)` — `scripts/apk-gates.py 16k`

Static Android 15+ 16 KiB page-size compatibility check on the built APK.
For every `lib/<abi>/*.so` entry:

- **ELF**: parses the ELF header + program headers; every `PT_LOAD` must
  have `p_align == 0x4000`. This is produced by
  `-Wl,-z,max-page-size=16384` in `app/src/main/cpp/CMakeLists.txt` for
  `libdautotune.so`; Oboe/NDK prebuilts must already comply.
- **ZIP**: the entry must be `STORED` (uncompressed — required by
  `useLegacyPackaging=false` / `extractNativeLibs=false`) and its local
  file-header data offset must be 16384-aligned. A compressed or
  misaligned entry fails even if the ELF itself is fine.

Evidence: per-file table (entry, size, stored flag, data offset + aligned
flag, ELF class, machine, all PT_LOAD aligns, verdict) then `RESULT:
PASS/FAIL`. Exit 1 on FAIL.

## 6. `Manifest security gate` — `scripts/apk-gates.py manifest`

Parses the **binary** `AndroidManifest.xml` (AXML string pool + resource
map + START_ELEMENT attribute records; merged manifest as actually
shipped, not the source file). Asserts:

- `uses-permission` set is exactly `{android.permission.RECORD_AUDIO}` —
  INTERNET, QUERY_ALL_PACKAGES, etc. must be absent.
- No `service`, `receiver`, or `provider` is effectively `exported` —
  explicit `android:exported="true"` or an intent-filter with no explicit
  value. `activity`/`activity-alias` may be exported **only** if it is the
  launcher (MAIN action + LAUNCHER category in a direct intent-filter).
- `android:debuggable` is present and `true` — expected on this debug
  build; printed as evidence, asserted true.

Evidence: package/version, min/target SDK, permission + feature lists,
per-component exported/launcher/effective flags, application flag dump
(`allowBackup`, `usesCleartextTraffic`, `extractNativeLibs`,
`supportsRtl`), then `RESULT: PASS/FAIL`.

## Known gaps (out of scope for CI-H)

- **Runtime gate does not exist yet.** Installing the APK on a 16 KiB
  page-size emulator, loading the JNI lib, and opening/stopping an Oboe
  stream is a separate card (CI-RT). Gates 5–6 are static checks only;
  they cannot prove the stream starts on-device.
- **Full Gradle dependency verification** (`gradle/verification-metadata.xml`
  covering all Maven artifacts) is not implemented — bounded card; the
  wrapper JAR + Gradle distribution + toolchain are pinned, Maven dep
  checksums are a follow-up if the owner approves.

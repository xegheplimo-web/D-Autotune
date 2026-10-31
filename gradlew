#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
JAR="$ROOT/gradle/wrapper/gradle-wrapper.jar"
EXPECTED="81a82aaea5abcc8ff68b3dfcb58b3c3c429378efd98e7433460610fecd7ae45f"
if [ ! -f "$JAR" ]; then
  command -v curl >/dev/null 2>&1 || { echo 'curl required to bootstrap Gradle' >&2; exit 1; }
  curl -fL --retry 2 --output "$JAR.part" https://raw.githubusercontent.com/gradle/gradle/v8.13.0/gradle/wrapper/gradle-wrapper.jar
  echo "$EXPECTED  $JAR.part" | sha256sum --check --status || { rm -f "$JAR.part"; echo 'Wrapper checksum mismatch' >&2; exit 1; }
  mv "$JAR.part" "$JAR"
fi
echo "$EXPECTED  $JAR" | sha256sum --check --status || { echo 'Existing wrapper JAR checksum mismatch' >&2; exit 1; }
exec "${JAVA_HOME:+$JAVA_HOME/bin/}java" -Dorg.gradle.appname=gradlew -cp "$JAR" org.gradle.wrapper.GradleWrapperMain "$@"

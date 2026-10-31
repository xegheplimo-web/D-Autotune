$ErrorActionPreference = 'Stop'
Set-Location (Resolve-Path (Join-Path $PSScriptRoot '..'))
Write-Host 'Building D-Autotune Android APK (debug)...'
& .\gradlew.bat --no-daemon :app:assembleDebug
if ($LASTEXITCODE -ne 0) { throw "Gradle build failed: $LASTEXITCODE" }
Write-Host "APK: $PWD\app\build\outputs\apk\debug\app-debug.apk"

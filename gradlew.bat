@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\ensure-wrapper.ps1"
if errorlevel 1 exit /b 1
if defined JAVA_HOME (
  set "JAVA_CMD=%JAVA_HOME%\bin\java.exe"
) else (
  set "JAVA_CMD=java"
)
"%JAVA_CMD%" -Dorg.gradle.appname=gradlew -classpath "%~dp0gradle\wrapper\gradle-wrapper.jar" org.gradle.wrapper.GradleWrapperMain %*
exit /b %errorlevel%

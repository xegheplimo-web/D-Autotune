# Download only the official Gradle wrapper JAR and verify the upstream SHA-256.
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$jar = Join-Path $project 'gradle\wrapper\gradle-wrapper.jar'
$expected = '81a82aaea5abcc8ff68b3dfcb58b3c3c429378efd98e7433460610fecd7ae45f'
$url = 'https://services.gradle.org/distributions/gradle-8.13-wrapper.jar'
function Test-Checksum($path) {
    if (!(Test-Path $path)) { return $false }
    return ((Get-FileHash $path -Algorithm SHA256).Hash.ToLowerInvariant() -eq $expected)
}
if (!(Test-Checksum $jar)) {
    if (Test-Path $jar) { throw 'Existing Gradle wrapper JAR checksum mismatch; delete the file manually after investigation.' }
    $tmp = "$jar.part"
    try {
        Write-Host 'Downloading official Gradle 8.13 wrapper JAR...'
        Invoke-WebRequest -Uri $url -OutFile $tmp -UseBasicParsing
        if (!(Test-Checksum $tmp)) { throw 'Gradle wrapper SHA-256 verification FAILED.' }
        Move-Item -Path $tmp -Destination $jar -Force
    } finally {
        if (Test-Path $tmp) { Remove-Item $tmp -Force }
    }
}
Write-Host 'Gradle wrapper verified.'

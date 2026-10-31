$ErrorActionPreference = 'Stop'
Set-Location (Resolve-Path (Join-Path $PSScriptRoot '..'))
cmake -S native-tests -B .local-test-build -DCMAKE_BUILD_TYPE=Release
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed' }
cmake --build .local-test-build --config Release
if ($LASTEXITCODE -ne 0) { throw 'CMake build failed' }
ctest --test-dir .local-test-build --output-on-failure -C Release
if ($LASTEXITCODE -ne 0) { throw 'DSP test failed' }

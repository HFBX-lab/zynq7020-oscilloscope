param(
    [string]$BuildRoot = 'D:\FPGA_Project\SP2_Trigger_Build\host',
    [string]$Gcc = 'D:\msys64\ucrt64\bin\gcc.exe',
    [string]$Label = 'trigger',
    [switch]$ExpectTestFailure
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
if (!(Test-Path -LiteralPath $Gcc)) { throw "GCC not found: $Gcc" }
$run = Join-Path $BuildRoot ((Get-Date -Format 'yyyyMMdd-HHmmss-ffff') + '-' + ($Label -replace '[^a-zA-Z0-9_-]', '_'))
$source = Join-Path $run 'source'
New-Item -ItemType Directory -Path $source -Force | Out-Null
$files = @('src/scope_trigger.h', 'src/scope_trigger.c', 'tests/scope_trigger_test.c', 'scripts/test_scope_trigger.ps1')
$hashes = @()
foreach ($file in $files) {
    $path = Join-Path $source $file
    New-Item -ItemType Directory -Path (Split-Path -Parent $path) -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $repo $file) -Destination $path
    $hashes += [ordered]@{ file = $file; sha256 = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash }
}
$status = [ordered]@{
    scope = 'offline-only'; hardware = 'not-run'; state = 'failed'; run = $run
    compiler = $Gcc; flags = @('-std=c11', '-Wall', '-Wextra', '-Werror', '-lm')
    expected_failure = [bool]$ExpectTestFailure; sources = $hashes
}
$oldPath = $env:PATH
$env:PATH = (Split-Path -Parent $Gcc) + ';' + $env:PATH
try {
    $include = Join-Path $source 'src'
    $implementation = Join-Path $source 'src/scope_trigger.c'
    $test = Join-Path $source 'tests/scope_trigger_test.c'
    $object = Join-Path $run 'scope_trigger.o'
    $exe = Join-Path $run 'scope_trigger_test.exe'
    $ErrorActionPreference = 'Continue'
    & $Gcc -std=c11 -Wall -Wextra -Werror -I $include -c $implementation -o $object *> (Join-Path $run 'compile-object.log')
    $objectExit = $LASTEXITCODE
    & $Gcc -std=c11 -Wall -Wextra -Werror -I $include $test $object -lm -o $exe *> (Join-Path $run 'compile-test.log')
    $compileExit = $LASTEXITCODE
    $ErrorActionPreference = 'Stop'
    if ($objectExit -ne 0 -or $compileExit -ne 0) { throw 'Compilation failed; inspect compile logs.' }
    $nm = Join-Path (Split-Path -Parent $Gcc) 'nm.exe'
    $symbols = & $nm -u $object
    $symbols | Set-Content -LiteralPath (Join-Path $run 'undefined-symbols.log')
    if ($LASTEXITCODE -ne 0) { throw 'Object symbol inspection failed.' }
    if (($symbols -join "`n") -match '\b_?(malloc|calloc|realloc|free|alloca)\b') { throw 'Trigger finder imports allocation.' }
    $status.no_allocation_imports = 'passed'
    $ErrorActionPreference = 'Continue'
    & $exe *> (Join-Path $run 'test.log')
    $testExit = $LASTEXITCODE
    $ErrorActionPreference = 'Stop'
    $status.test_exit = $testExit
    $log = Get-Content -LiteralPath (Join-Path $run 'test.log') -Raw
    if ($ExpectTestFailure) {
        if ($testExit -ne 1 -or $log -notmatch 'SCOPE_TRIGGER_TEST FAIL checks=\d+ failures=[1-9]\d*') {
            throw 'Expected a semantic test failure, not a crash or passing tests.'
        }
        $status.state = 'red-expected'
    } else {
        if ($testExit -ne 0 -or $log -notmatch 'SCOPE_TRIGGER_TEST PASS checks=\d+ failures=0') { throw 'Trigger tests failed.' }
        $status.state = 'passed'
    }
    $status.executable_sha256 = (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash
    Write-Output (($log -split "`r?`n" | Where-Object { $_ -match '^SCOPE_TRIGGER_TEST ' }) -join "`n")
    Write-Output "Evidence: $run"
} finally {
    $env:PATH = $oldPath
    $ErrorActionPreference = 'Stop'
    $status | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $run 'status.json') -Encoding UTF8
}

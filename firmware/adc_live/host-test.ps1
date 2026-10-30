param([switch]$ExpectFailure)
$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$buildRoot = 'D:/FPGA_Project/SP2_ADC_Live_Build/host'
$runPath = Join-Path $buildRoot ((Get-Date -Format 'yyyyMMdd-HHmmss-fff') + '-' + $PID)
$sourcePath = Join-Path $runPath 'source'
$tempPath = Join-Path $runPath 'tmp'
New-Item -ItemType Directory -Path $sourcePath, $tempPath -Force | Out-Null
$gcc = 'D:/msys64/ucrt64/bin/gcc.exe'
$python = 'D:/Python/python.exe'
$files = @('firmware/adc_live/record_stats.h', 'firmware/adc_live/record_stats.c',
           'tests/adc_live_stats_test.c', 'firmware/adc_live/host-test.ps1')
$hashes = @{}
foreach ($relative in $files) {
    $original = Join-Path $projectRoot $relative
    Copy-Item -LiteralPath $original -Destination (Join-Path $sourcePath (Split-Path $relative -Leaf))
    $hashes[$relative] = (Get-FileHash -LiteralPath $original -Algorithm SHA256).Hash
}
$status = [ordered]@{ state = 'not-run'; scope = 'offline-only'; run = $runPath;
    source_sha256 = $hashes; compiler = $gcc; python = $python;
    expect_failure = [bool]$ExpectFailure }
$oldTemp = $env:TEMP
$oldTmp = $env:TMP
try {
    $env:TEMP = $tempPath
    $env:TMP = $tempPath
    $fixtureGenerator = @'
import pathlib, struct, sys, zlib
p=pathlib.Path(sys.argv[1])
data=b''.join(struct.pack('<I', ((i*7+13)%1024 | (1024 if i%11==0 else 0)) | (((i*19+23)%1024 | (1024 if i%17==0 else 0))<<16)) for i in range(4096))
crc=zlib.crc32(data)
(p/'fixture_expected.h').write_text('#define FIXTURE_CRC32 0x%08Xu\n'%crc, encoding='ascii')
(p/'fixture-reference.bin').write_bytes(data)
print('PYTHON_FIXTURE_CRC=%08X'%crc)
'@
    & $python -c $fixtureGenerator $runPath | Set-Content -LiteralPath (Join-Path $runPath 'fixture.log')
    if ($LASTEXITCODE -ne 0) { throw 'Python CRC fixture generation failed' }
    $exe = Join-Path $runPath 'adc_live_stats_test.exe'
    $arguments = @('-std=c99', '-Wall', '-Wextra', '-Werror', '-pedantic', '-O2',
        '-I', $sourcePath, '-I', $runPath, (Join-Path $sourcePath 'record_stats.c'),
        (Join-Path $sourcePath 'adc_live_stats_test.c'), '-lm', '-o', $exe)
    $status.compile_arguments = $arguments
    $compileOutput = @(& $gcc @arguments 2>&1)
    $status.compile_exit = $LASTEXITCODE
    [System.IO.File]::WriteAllLines((Join-Path $runPath 'compile.log'), [string[]]$compileOutput)
    if ($LASTEXITCODE -ne 0) { throw 'C compilation failed' }
    $outputPath = Join-Path $runPath 'fixture-actual.bin'
    # Native stderr is captured separately so Windows PowerShell does not turn
    # intentional RED assertions into terminating NativeCommandError records.
    $process = Start-Process -FilePath $exe -ArgumentList ('"' + $outputPath + '"') -Wait -PassThru -WindowStyle Hidden -RedirectStandardOutput (Join-Path $runPath 'test.stdout.log') -RedirectStandardError (Join-Path $runPath 'test.stderr.log')
    $status.test_exit = $process.ExitCode
    if ($ExpectFailure) {
        if ($process.ExitCode -ne 1 -or -not (Select-String -LiteralPath (Join-Path $runPath 'test.stderr.log') -Pattern 'FAIL line' -Quiet)) { throw 'Expected semantic RED assertions did not occur' }
        $status.state = 'expected-red'
    } else {
        if ($process.ExitCode -ne 0) { throw 'C statistics tests failed' }
        $crcCheck = @'
import pathlib, sys, zlib
p=pathlib.Path(sys.argv[1]); actual=(p/'fixture-actual.bin').read_bytes(); expected=(p/'fixture-reference.bin').read_bytes()
assert len(actual)==16384 and actual==expected
line='CRC_FIXTURE=%08X'%zlib.crc32(actual)
assert line in (p/'test.stdout.log').read_text()
print('PASS independent Python zlib CRC and little-endian bytes: '+line)
'@
        & $python -c $crcCheck $runPath | Set-Content -LiteralPath (Join-Path $runPath 'crc-crosscheck.log')
        if ($LASTEXITCODE -ne 0) { throw 'Independent CRC crosscheck failed' }
        $status.state = 'passed'
    }
} catch {
    $status.state = 'failed'
    $status.error = $_.Exception.Message
    throw
} finally {
    $env:TEMP = $oldTemp
    $env:TMP = $oldTmp
    $status | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $runPath 'status.json') -Encoding UTF8
    Write-Output ('ADC_LIVE_STATS_RUN=' + $runPath)
}
Get-Content -LiteralPath (Join-Path $runPath 'test.stdout.log')
Write-Output ('STATE=' + $status.state)

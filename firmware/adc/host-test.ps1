param([string]$BuildRoot='D:\FPGA_Project\SP2_ADC_Firmware\host')
$ErrorActionPreference='Stop'
$root=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
if(-not ([IO.Path]::GetFullPath($BuildRoot).StartsWith('D:\',[StringComparison]::OrdinalIgnoreCase))){throw 'Host build must remain on D drive'}
$run=Join-Path $BuildRoot (Get-Date -Format 'yyyyMMdd-HHmmss-ffff')
$source=Join-Path $run 'source'
New-Item -ItemType Directory -Path $source -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'adc_format.c'),(Join-Path $PSScriptRoot 'adc_format.h'),(Join-Path $root 'tests\adc_format_test.c') -Destination $source
$exe=Join-Path $run 'adc_format_test.exe'
$state=@{state='building';scope='offline-only host C protocol tests';run=$run;
  source_hashes=@(Get-ChildItem -LiteralPath $source -File | Get-FileHash | Select-Object Path,Hash)}
$tempDir=Join-Path $run 'tmp'
New-Item -ItemType Directory -Path $tempDir -Force | Out-Null
$saved=@{TEMP=$env:TEMP;TMP=$env:TMP;TMPDIR=$env:TMPDIR}
$env:TEMP=$tempDir;$env:TMP=$tempDir;$env:TMPDIR=$tempDir
try {
  & D:\msys64\ucrt64\bin\gcc.exe -std=c11 -Wall -Wextra -Werror "-I$source" (Join-Path $source 'adc_format_test.c') (Join-Path $source 'adc_format.c') -o $exe *> (Join-Path $run 'compile.log')
  if($LASTEXITCODE){throw 'ADC host compilation failed'}
  & $exe | Tee-Object -FilePath (Join-Path $run 'test.log')
  if($LASTEXITCODE){throw 'ADC host tests failed'}
  $state.state='succeeded';$state.exe_sha256=(Get-FileHash -LiteralPath $exe).Hash
} catch {$state.state='failed';$state.error=$_.Exception.Message;throw}
finally {
  foreach($kv in $saved.GetEnumerator()){[Environment]::SetEnvironmentVariable($kv.Key,$kv.Value,'Process')}
  $state | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $BuildRoot 'build-status.json') -Encoding utf8
}

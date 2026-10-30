param([string]$BuildRoot='D:\FPGA_Project\SP2_Touch_Build\host')
$ErrorActionPreference='Stop'
$root=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$run=Join-Path $BuildRoot (Get-Date -Format 'yyyyMMdd-HHmmss-ffff')
$source=Join-Path $run source
New-Item -ItemType Directory $source -Force | Out-Null
Get-ChildItem $PSScriptRoot -File | Where-Object Extension -in '.c','.h' | Copy-Item -Destination $source
Copy-Item "$root/tests/touch_test.c","$root/tests/touch_i2c_test.c" -Destination $source
$previousTemp=$env:TEMP;$previousTmp=$env:TMP
New-Item -ItemType Directory "$run/tmp" -Force | Out-Null
$env:TEMP="$run/tmp";$env:TMP="$run/tmp"
$state=@{state='building';scope='offline-only';run=$run;source_hashes=@(Get-ChildItem $source -File | Get-FileHash | Select-Object Path,Hash)}
try {
 foreach($pair in @(@('touch_test','touch'),@('touch_i2c_test','i2c_bitbang'))){
  & D:/msys64/ucrt64/bin/gcc.exe -std=c11 -Wall -Wextra -Werror -Wno-misleading-indentation "-I$source" "$source/$($pair[0]).c" "$source/$($pair[1]).c" -o "$run/$($pair[0]).exe" *> "$run/$($pair[0])-compile.log"
  if($LASTEXITCODE){throw "Host compile failed: $pair"}
  & "$run/$($pair[0]).exe" | Tee-Object "$run/$($pair[0]).log"
  if($LASTEXITCODE){throw "Host tests failed: $pair"}
 }
 $state.state='succeeded'
}catch{$state.state='failed';$state.error=$_.Exception.Message;throw}
finally{
 $env:TEMP=$previousTemp;$env:TMP=$previousTmp
 $state | ConvertTo-Json -Depth 6 | Set-Content "$BuildRoot/build-status.json" -Encoding utf8
}

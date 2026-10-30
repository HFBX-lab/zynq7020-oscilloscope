param(
  [Parameter(Mandatory=$true)][string]$Xsa,
  [string]$BuildRoot='D:\FPGA_Project\SP2_ADC_Firmware'
)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
if(-not ([IO.Path]::GetFullPath($BuildRoot).StartsWith('D:\',[StringComparison]::OrdinalIgnoreCase))){throw 'ADC build/cache must remain on D drive'}
$Xsa=(Resolve-Path -LiteralPath $Xsa).Path
$hardwareDir=Split-Path $Xsa -Parent
$hardwareStatus=Join-Path $hardwareDir 'build-status.json'
$hardwareState=Get-Content -LiteralPath $hardwareStatus -Raw | ConvertFrom-Json
if($hardwareState.state -ne 'succeeded'){throw 'ADC hardware platform has no successful build state'}
$xsaName=Split-Path $Xsa -Leaf
$expected=@($hardwareState.artifacts | Where-Object file -eq $xsaName)
if($expected.Count -ne 1 -or (Get-FileHash -LiteralPath $Xsa).Hash -ne $expected[0].sha256){throw 'ADC XSA hash mismatch'}
if($xsaName -ne 'sp2_adc_internal.xsa'){throw 'Only the new internal digital ADC XSA is accepted'}
$bit=Join-Path $hardwareDir 'sp2_adc_internal.bit'
$bitExpected=@($hardwareState.artifacts | Where-Object file -eq 'sp2_adc_internal.bit')
if($bitExpected.Count -ne 1 -or (Get-FileHash -LiteralPath $bit).Hash -ne $bitExpected[0].sha256){throw 'ADC bitstream hash mismatch'}
$id=Get-Date -Format 'yyyyMMdd-HHmmss-ffff'
$run=Join-Path $BuildRoot "runs\$id"
$stage=Join-Path $run 'source'
$workspace=Join-Path $run 'workspace'
$evidence=Join-Path $projectRoot 'build\adc-firmware'
New-Item -ItemType Directory -Path $stage,$evidence -Force | Out-Null
# Retain earlier ADC firmware candidates; LCD/LVGL/FreeRTOS outputs are untouched.
$oldFiles=@(Get-ChildItem -LiteralPath $evidence -File)
if($oldFiles.Count){
  $history=Join-Path $evidence "history\$id"
  New-Item -ItemType Directory -Path $history -Force | Out-Null
  foreach($f in $oldFiles){Move-Item -LiteralPath $f.FullName -Destination $history}
}
Get-ChildItem -LiteralPath $PSScriptRoot -File | Copy-Item -Destination $stage
foreach($name in @('board.c','board.h','portable.c','portable.h')){
  Copy-Item -LiteralPath (Join-Path $projectRoot "firmware\lvgl\$name") -Destination $stage
}
foreach($name in @('frame_source.c','frame_source.h','memory_probe.c','memory_probe.h')){
  Copy-Item -LiteralPath (Join-Path $projectRoot "firmware\standalone\$name") -Destination $stage
}
Copy-Item -LiteralPath (Join-Path $projectRoot 'firmware\Xilinx.spec') -Destination $stage
Copy-Item -LiteralPath $Xsa -Destination (Join-Path $run 'hardware.xsa')
Copy-Item -LiteralPath $bit -Destination (Join-Path $run 'hardware.bit')
Copy-Item -LiteralPath $hardwareStatus -Destination (Join-Path $run 'hardware-build-status.json')
$linker=Get-Content -LiteralPath (Join-Path $projectRoot 'firmware\linker_template.ld') -Raw
if($linker -notmatch 'LENGTH\s*=\s*0x1FF00000'){throw 'Unexpected linker template memory size'}
$linker=$linker -replace 'LENGTH\s*=\s*0x1FF00000','LENGTH = 0x00F00000'
if($linker -notmatch '_STACK_SIZE = DEFINED\(_STACK_SIZE\) \? _STACK_SIZE : 0x2000;'){throw 'Unexpected stack template'}
$linker=$linker -replace '_STACK_SIZE = DEFINED\(_STACK_SIZE\) \? _STACK_SIZE : 0x2000;','_STACK_SIZE = 0x20000;'
Set-Content -LiteralPath (Join-Path $stage 'lscript.ld') -Value $linker -Encoding ascii
$state=@{state='building';scope='offline-only; internal DIGITAL source; no JTAG';run=$run;workspace=$workspace;
  hardware_status=$hardwareStatus;xsa_sha256=$expected[0].sha256;bit_sha256=$bitExpected[0].sha256;artifacts=@();
  source_hashes=@(Get-ChildItem -LiteralPath $stage -File | Get-FileHash | Select-Object Path,Hash)}
$statusPath=Join-Path $evidence 'build-status.json'
$state | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $statusPath -Encoding utf8
$tempDir=Join-Path $run 'tmp'
New-Item -ItemType Directory -Path $tempDir -Force | Out-Null
$saved=@{TEMP=$env:TEMP;TMP=$env:TMP;TMPDIR=$env:TMPDIR}
$env:TEMP=$tempDir;$env:TMP=$tempDir;$env:TMPDIR=$tempDir
$armBin='D:/Xilinx_2020.2/Vitis/2020.2/gnu/aarch32/nt/gcc-arm-none-eabi/bin'
try {
  if((Get-FileHash -LiteralPath (Join-Path $run 'hardware.xsa')).Hash -ne $state.xsa_sha256 -or
     (Get-FileHash -LiteralPath (Join-Path $run 'hardware.bit')).Hash -ne $state.bit_sha256){throw 'Hardware changed during snapshot'}
  & D:\Xilinx_2020.2\Vitis\2020.2\bin\xsct.bat (Join-Path $stage 'bsp.tcl') (Join-Path $run 'hardware.xsa') $workspace *> (Join-Path $run 'bsp.log')
  if($LASTEXITCODE -or -not (Select-String -LiteralPath (Join-Path $run 'bsp.log') -Pattern '^ADC_BSP_PASS:' -Quiet)){throw 'ADC standalone BSP generation failed'}
  # Vitis exports library/include trees separately (bsplib vs bspinclude).
  # Use this run's fresh source BSP, whose lib/include directories are siblings.
  $bspRoot=Join-Path $workspace 'adc_platform\ps7_cortexa9_0\standalone_domain\bsp\ps7_cortexa9_0'
  $lib=@(Get-Item -LiteralPath (Join-Path $bspRoot 'lib\libxil.a'))
  if($lib.Count -ne 1 -or -not (Test-Path -LiteralPath (Join-Path $bspRoot 'include\xparameters.h'))){throw 'Fresh standalone BSP library/include tree missing'}
  $libdir=$lib[0].DirectoryName;$inc=Join-Path $lib[0].Directory.Parent.FullName 'include'
  $state.bsp_include=$inc;$state.bsp_lib=$libdir
  # Verify this BSP actually maps the new slave before compiling an executable.
  if(-not (Select-String -LiteralPath (Join-Path $inc 'xparameters.h') -Pattern '0x43C10000' -Quiet)){throw 'Fresh BSP lacks ADC candidate base address'}
  $build=Join-Path $run 'cmake'
  & D:\msys64\ucrt64\bin\cmake.exe -S $stage -B $build -G Ninja '-DCMAKE_SYSTEM_NAME=Generic' '-DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY' "-DCMAKE_C_COMPILER=$armBin/arm-none-eabi-gcc.exe" '-DCMAKE_MAKE_PROGRAM=D:/Python/Scripts/ninja.exe' "-DBSP_INCLUDE=$inc" "-DBSP_LIB=$libdir" *> (Join-Path $run 'configure.log')
  if($LASTEXITCODE){throw 'ADC ARM CMake configuration failed'}
  & D:\msys64\ucrt64\bin\cmake.exe --build $build --parallel 4 *> (Join-Path $run 'compile.log')
  if($LASTEXITCODE){throw 'ADC ARM compilation failed'}
  $elf=Join-Path $build 'adc_internal_selftest.elf'
  $headers=& (Join-Path $armBin 'arm-none-eabi-readelf.exe') -lW $elf
  if($LASTEXITCODE){throw 'ELF segment inspection failed'}
  $headers | Set-Content -LiteralPath (Join-Path $run 'elf-segments.log')
  $loadCount=0
  foreach($line in $headers){
    if($line -match '^\s*LOAD\s+0x[0-9a-f]+\s+(0x[0-9a-f]+)\s+(0x[0-9a-f]+)\s+0x[0-9a-f]+\s+(0x[0-9a-f]+)'){
      $va=[Convert]::ToUInt64($Matches[1].Substring(2),16);$pa=[Convert]::ToUInt64($Matches[2].Substring(2),16);$bytes=[Convert]::ToUInt64($Matches[3].Substring(2),16)
      if($va -lt 0x100000 -or $pa -lt 0x100000 -or $va+$bytes -gt 0x1000000 -or $pa+$bytes -gt 0x1000000){throw 'ELF LOAD exceeds reserved low 16MiB'}
      $loadCount++
    }
  }
  if($loadCount -eq 0){throw 'No ELF LOAD segments verified'}
  $symbols=& (Join-Path $armBin 'arm-none-eabi-nm.exe') -n $elf
  if($LASTEXITCODE){throw 'ELF symbol inspection failed'}
  $symbols | Set-Content -LiteralPath (Join-Path $run 'elf-symbols.log')
  $symbolValues=@{}
  foreach($line in $symbols){if($line -match '^([0-9a-f]+)\s+\S\s+(\S+)$'){$symbolValues[$Matches[2]]=[Convert]::ToUInt64($Matches[1],16)}}
  foreach($name in @('_heap_start','_heap_end','_stack_end','_stack','__undef_stack')){
    if(-not $symbolValues.ContainsKey($name) -or $symbolValues[$name] -lt 0x100000 -or $symbolValues[$name] -ge 0x1000000){throw "Invalid ELF memory symbol $name"}
  }
  if($symbolValues['_stack']-$symbolValues['_stack_end'] -ne 0x20000){throw 'Main stack is not 128KiB'}
  $state.memory_verified=@{load_segments=$loadCount;low_ddr_limit='0x01000000';main_stack_bytes=131072}
  $state.bsp_hashes=@(Get-FileHash -LiteralPath $lib[0].FullName,(Join-Path $inc 'xparameters.h') | Select-Object Path,Hash)
  # Publish only after compiler, segment, stack, XSA, and bitstream checks pass.
  foreach($name in @('adc_internal_selftest.elf','adc_internal_selftest.map')){
    $f=Join-Path $build $name;Copy-Item -LiteralPath $f -Destination $evidence
    $state.artifacts+=@{file=$name;sha256=(Get-FileHash -LiteralPath $f).Hash}
  }
  Copy-Item -LiteralPath (Join-Path $stage 'lscript.ld'),(Join-Path $run 'hardware-build-status.json') -Destination $evidence
  $state.state='succeeded'
} catch {
  $state.state='failed';$state.error=$_.Exception.Message
  # Any partly published files are quarantined so a failed build is never current.
  foreach($name in @('adc_internal_selftest.elf','adc_internal_selftest.map')){
    $partial=Join-Path $evidence $name
    if(Test-Path -LiteralPath $partial){Move-Item -LiteralPath $partial -Destination $run -Force}
  }
  throw
} finally {
  foreach($kv in $saved.GetEnumerator()){[Environment]::SetEnvironmentVariable($kv.Key,$kv.Value,'Process')}
  Get-ChildItem -LiteralPath $run -File -Filter '*.log' | Copy-Item -Destination $evidence -Force
  $state | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $statusPath -Encoding utf8
}

param(
  [ValidateSet('standalone','freertos')][string]$OS='standalone',
  [string]$BuildRoot='D:\FPGA_Project\SP2_LVGL_Build',
  [string]$BspWorkspace=''
)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$xsa=Join-Path $projectRoot 'build\ps-screen\sp2_ps_screen.xsa'
$hardwareState=Get-Content (Join-Path $projectRoot 'build\ps-screen\build-status.json') -Raw | ConvertFrom-Json
if($hardwareState.state -ne 'succeeded'){throw 'Hardware platform has no successful build state'}
$expected=($hardwareState.artifacts | Where-Object file -eq 'sp2_ps_screen.xsa').sha256
if((Get-FileHash -LiteralPath $xsa).Hash -ne $expected){throw 'XSA hash mismatch'}
$id=Get-Date -Format 'yyyyMMdd-HHmmss-ffff'
$run=Join-Path $BuildRoot "runs\$id-$OS"
$stage=Join-Path $run 'board'
$ui=Join-Path $run 'ui'
$legacy=Join-Path $run 'legacy'
$lvgl=Join-Path $run 'lvgl'
$evidence=Join-Path $projectRoot "build\board-lvgl\$OS"
New-Item -ItemType Directory -Path $stage,$ui,$legacy,$evidence -Force | Out-Null
$oldFiles=@(Get-ChildItem -LiteralPath $evidence -File)
if($oldFiles.Count){
  $history=Join-Path $evidence "history\$id"
  New-Item -ItemType Directory -Path $history -Force | Out-Null
  foreach($f in $oldFiles){Move-Item -LiteralPath $f.FullName -Destination $history}
}
Get-ChildItem -LiteralPath $PSScriptRoot -File | Copy-Item -Destination $stage -Force
Copy-Item -LiteralPath (Join-Path $projectRoot 'firmware\Xilinx.spec') -Destination $stage
foreach($name in @('scope_ui.c','scope_ui.h','scope_calibration.c','scope_calibration.h','scope_calibration_profile.c','scope_calibration_profile.h','scope_model.c','scope_model.h','scope_trigger.c','scope_trigger.h','scope_cursor.c','scope_cursor.h','scope_acquisition.c','scope_acquisition.h','scope_measurement.c','scope_measurement.h','scope_autoset.c','scope_autoset.h','scope_record.c','scope_record.h','scope_settings.c','scope_settings.h','scope_spectrum.c','scope_spectrum.h','scope_storage.c','scope_storage.h','scope_storage_ui.c','scope_storage_ui.h')){
  Copy-Item -LiteralPath (Join-Path $projectRoot "src\$name") -Destination $ui
}
Copy-Item -LiteralPath (Join-Path $projectRoot 'src\fonts') -Destination $ui -Recurse
foreach($name in @('frame_source.c','frame_source.h','memory_probe.c','memory_probe.h')){
  Copy-Item -LiteralPath (Join-Path $projectRoot "firmware\standalone\$name") -Destination $legacy
}
Copy-Item -LiteralPath (Join-Path $projectRoot 'third_party\lvgl-9.2.2') -Destination $lvgl -Recurse
Copy-Item -LiteralPath $xsa -Destination (Join-Path $run 'hardware.xsa')
$linker=Get-Content (Join-Path $projectRoot 'firmware\linker_template.ld') -Raw
if($linker -notmatch 'LENGTH\s*=\s*0x1FF00000'){throw 'Unexpected linker template'}
$linker=$linker -replace 'LENGTH\s*=\s*0x1FF00000','LENGTH = 0x00F00000'
$linker=$linker -replace '_HEAP_SIZE = DEFINED\(_HEAP_SIZE\) \? _HEAP_SIZE : 0x2000;','_HEAP_SIZE = 0x400000;'
$linker=$linker -replace '_STACK_SIZE = DEFINED\(_STACK_SIZE\) \? _STACK_SIZE : 0x2000;','_STACK_SIZE = 0x20000;'
Set-Content -LiteralPath (Join-Path $stage 'lscript.ld') -Value $linker -Encoding ascii
$workspace=if($BspWorkspace){$BspWorkspace}else{Join-Path $run 'workspace'}
$inputs=@(Get-ChildItem $stage,$ui,$legacy,$lvgl -File -Recurse)
$state=@{state='building';os=$OS;run=$run;workspace=$workspace;xsa_sha256=$expected;artifacts=@();
  source_hashes=@($inputs | Get-FileHash | Select-Object Path,Hash)}
$statusPath=Join-Path $evidence 'build-status.json'
$state | ConvertTo-Json -Depth 5 | Set-Content $statusPath -Encoding utf8
$tempDir=Join-Path $run 'tmp'
New-Item -ItemType Directory -Path $tempDir -Force | Out-Null
$saved=@{TEMP=$env:TEMP;TMP=$env:TMP;TMPDIR=$env:TMPDIR}
$env:TEMP=$tempDir;$env:TMP=$tempDir;$env:TMPDIR=$tempDir
try {
  if($BspWorkspace){
    $old=Get-ChildItem (Join-Path $workspace 'sp2_platform\export\sp2_platform\hw') -Filter *.xsa -File | Select-Object -First 1
    if(-not $old -or (Get-FileHash $old.FullName).Hash -ne $expected){throw 'Reusable BSP hardware hash mismatch'}
    if($OS -ne 'standalone'){throw 'RTOS builds generate a fresh configured BSP'}
  }else{
    & D:\Xilinx_2020.2\Vitis\2020.2\bin\xsct.bat (Join-Path $stage 'bsp.tcl') (Join-Path $run 'hardware.xsa') $workspace $OS *> (Join-Path $run 'bsp.log')
    if($LASTEXITCODE -or -not (Select-String (Join-Path $run 'bsp.log') -Pattern '^LVGL_BSP_PASS:' -Quiet)){throw 'LVGL BSP generation failed'}
  }
  $lib=Get-ChildItem (Join-Path $workspace 'sp2_platform') -File -Recurse -Filter libxil.a | Where-Object {Test-Path (Join-Path $_.Directory.Parent.FullName 'include\xparameters.h')} | Select-Object -First 1
  if(-not $lib){throw 'No BSP library/includes'}
  $libdir=$lib.DirectoryName;$inc=Join-Path $lib.Directory.Parent.FullName 'include'
  $state.bsp_include=$inc;$state.bsp_lib=$libdir
  if($OS -eq 'freertos'){
    Copy-Item (Join-Path $inc 'FreeRTOSConfig.h') -Destination $evidence
    if(-not (Test-Path (Join-Path $libdir 'libfreertos.a'))){throw 'No FreeRTOS kernel archive'}
  }
  $build=Join-Path $run 'cmake'
  & D:\msys64\ucrt64\bin\cmake.exe -S $stage -B $build -G Ninja '-DCMAKE_SYSTEM_NAME=Generic' '-DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY' '-DCMAKE_C_COMPILER=D:/Xilinx_2020.2/Vitis/2020.2/gnu/aarch32/nt/gcc-arm-none-eabi/bin/arm-none-eabi-gcc.exe' '-DCMAKE_MAKE_PROGRAM=D:/Python/Scripts/ninja.exe' "-DLVGL_ROOT=$lvgl" "-DUI_ROOT=$ui" "-DLEGACY_ROOT=$legacy" "-DBSP_INCLUDE=$inc" "-DBSP_LIB=$libdir" "-DSCOPE_OS=$OS" *> (Join-Path $run 'configure.log')
  if($LASTEXITCODE){throw 'Board CMake configuration failed'}
  & D:\msys64\ucrt64\bin\cmake.exe --build $build --parallel 6 *> (Join-Path $run 'compile.log')
  if($LASTEXITCODE){throw 'Board ARM compilation failed'}
  foreach($name in @("scope_lvgl_$OS.elf","scope_lvgl_$OS.map")){
    $f=Join-Path $build $name
    Copy-Item $f -Destination $evidence
    $state.artifacts+=@{file=$name;sha256=(Get-FileHash $f).Hash}
  }
  Copy-Item (Join-Path $stage 'lscript.ld') -Destination $evidence
  $state.state='succeeded'
} catch {$state.state='failed';$state.error=$_.Exception.Message;throw}
finally {
  foreach($kv in $saved.GetEnumerator()){[Environment]::SetEnvironmentVariable($kv.Key,$kv.Value,'Process')}
  Get-ChildItem $run -File -Filter '*.log' | Copy-Item -Destination $evidence -Force
  $state | ConvertTo-Json -Depth 5 | Set-Content $statusPath -Encoding utf8
}

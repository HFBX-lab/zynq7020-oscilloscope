param(
  [string]$Xsa='D:/FPGA_Project/SP2_Storage_Build/hardware/storage.xsa',
  [string]$BuildRoot='D:/FPGA_Project/SP2_Storage_Build/firmware'
)
$ErrorActionPreference='Stop'
$expected='820B9017A5D13D679BE8CFF9E759E1433A2566DFB13BCF9E42EF011F34A6FE02'
$sha=(Get-FileHash -LiteralPath $Xsa).Hash
if($sha-ne $expected){throw 'Storage XSA identity mismatch'}
$run=Join-Path $BuildRoot ('bsp-runs/'+(Get-Date -Format 'yyyyMMdd-HHmmss-ffff'))
$workspace=Join-Path $run 'workspace'
$bsp=Join-Path $run 'bsp'
$tmp=Join-Path $run 'tmp'
New-Item -ItemType Directory -Path $run,$tmp | Out-Null
Copy-Item -LiteralPath $Xsa -Destination (Join-Path $run 'hardware.xsa')
Copy-Item -LiteralPath $PSCommandPath,(Join-Path $PSScriptRoot 'bsp_storage.tcl') -Destination $run
$project=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
Copy-Item -LiteralPath (Join-Path $project 'scripts/patch_sdps_identity.py') -Destination $run
$state=@{state='building';workspace=$workspace;xsa_sha256=$sha;run=$run;board_test='not-run'}
$saved=@{TEMP=$env:TEMP;TMP=$env:TMP;TMPDIR=$env:TMPDIR}
$env:TEMP=$tmp;$env:TMP=$tmp;$env:TMPDIR=$tmp
try {
  & D:/Xilinx_2020.2/Vitis/2020.2/bin/xsct.bat (Join-Path $run 'bsp_storage.tcl') (Join-Path $run 'hardware.xsa') $workspace *> (Join-Path $run 'bsp.log')
  if($LASTEXITCODE -or !(Select-String -LiteralPath (Join-Path $run 'bsp.log') -Pattern '^M5_STORAGE_BSP_PASS' -Quiet)){throw 'Storage BSP generation failed'}
  $platform=Join-Path $workspace 'sp2_storage'
  $exported=@(Get-ChildItem -LiteralPath (Join-Path $platform 'export/sp2_storage/hw') -File -Filter *.xsa)
  if($exported.Count-ne 1 -or (Get-FileHash -LiteralPath $exported[0].FullName).Hash-ne $sha){throw 'Exported BSP XSA mismatch'}
  $generated=Join-Path $platform 'ps7_cortexa9_0/freertos10_xilinx_domain/bsp/ps7_cortexa9_0'
  Copy-Item -LiteralPath $generated -Destination $bsp -Recurse
  $inc=Join-Path $bsp 'include';$lib=Join-Path $bsp 'lib'
  $config=Get-Content -LiteralPath (Join-Path $inc 'xparameters.h') -Raw
  foreach($check in @('XPAR_XSDPS_NUM_INSTANCES 1','XPAR_PS7_SD_0_BASEADDR 0xE0100000','XPAR_PS7_SD_0_HAS_CD 1','XPAR_PS7_SD_0_HAS_WP 0','XPAR_PS7_UART_1_BASEADDR 0xE0001000')){if(!$config.Contains($check)){throw "BSP mapping mismatch: $check"}}
  foreach($forbidden in @('FILE_SYSTEM_USE_MKFS','FILE_SYSTEM_READ_ONLY','FILE_SYSTEM_FS_EXFAT')){if($config -match "(?m)^#define $forbidden\b"){throw "Unsafe FatFs configuration: $forbidden"}}
  foreach($name in @('libxil.a','libfreertos.a','libxilffs.a')){if(!(Test-Path -LiteralPath (Join-Path $lib $name))){throw "Missing BSP archive $name"}}
  # Only the copied vendor driver is modified. The separately built full SDPS
  # archive must precede libxil at link time, so libxil's original cannot win.
  $driver=Join-Path $bsp 'libsrc/sdps_v3_10/src'
  $card=Join-Path $driver 'xsdps_card.c'
  & D:/Python/python.exe (Join-Path $run 'patch_sdps_identity.py') $card > (Join-Path $run 'sdps-identity-patch.json')
  if($LASTEXITCODE){throw 'Frozen SDPS identity patch failed'}
  $original=Get-Content -LiteralPath $card -Raw
  $state.sdps_card_original_sha256=(Get-FileHash -LiteralPath $card).Hash
  $patched=$original
  foreach($function in @('XSdPs_CardOpCond','XSdPs_GetCardId')){
    $pattern='(s32 '+$function+'\(XSdPs \*InstancePtr\)\s*\{)'
    if([regex]::Matches($patched,$pattern).Count-ne 1){throw "Unexpected driver function $function"}
    $patched=[regex]::Replace($patched,$pattern,'$1' + "`n`tTickType_t storage_start = xTaskGetTickCount();")
  }
  foreach($condition in @('} while ((RespOCR & XSDPS_RESPOCR_READY) == 0U);','} while (InstancePtr->RelCardAddr == 0U);')){
    if(!$patched.Contains($condition)){throw 'Unexpected unbounded SDPS loop source'}
    $guard="if ((TickType_t)(xTaskGetTickCount() - storage_start) >= pdMS_TO_TICKS(3000U)) { Status = XST_FAILURE; goto RETURN_PATH; }`n`t`tvTaskDelay(1);`n`t"
    $patched=$patched.Replace($condition,$guard+$condition)
  }
  $patched="#include `"FreeRTOS.h`"`n#include `"task.h`"`n"+$patched
  Set-Content -LiteralPath $card -Value $patched -Encoding ascii
  $gcc='D:/Xilinx_2020.2/Vitis/2020.2/gnu/aarch32/nt/gcc-arm-none-eabi/bin/arm-none-eabi-gcc.exe'
  $ar=Join-Path (Split-Path $gcc -Parent) 'arm-none-eabi-ar.exe'
  $objects=Join-Path $run 'sdps-objects';New-Item -ItemType Directory -Path $objects | Out-Null
  $objectList=@()
  foreach($source in Get-ChildItem -LiteralPath $driver -File -Filter *.c){
    $object=Join-Path $objects ($source.BaseName+'.o');$objectList+=$object
    & $gcc -O2 -g -std=c11 -Wall -Wextra -Werror -mcpu=cortex-a9 -mfpu=vfpv3 -mfloat-abi=hard -ffunction-sections -fdata-sections "-I$inc" -c $source.FullName -o $object *> (Join-Path $objects ($source.BaseName+'.log'))
    if($LASTEXITCODE){throw "Patched SDPS compile failed: $($source.Name)"}
  }
  $archive=Join-Path $lib 'libstorage_sdps.a'
  & $ar rcs $archive @objectList
  if($LASTEXITCODE){throw 'Patched SDPS archive failed'}
  $state.state='succeeded-offline-only';$state.bsp_include=$inc;$state.bsp_lib=$lib;$state.sdps_library=$archive
  $state.configuration_sha256=(Get-FileHash -LiteralPath (Join-Path $inc 'FreeRTOSConfig.h')).Hash
  $state.sdps_card_patched_sha256=(Get-FileHash -LiteralPath $card).Hash
  $state.source_bsp_hashes=@(Get-ChildItem -LiteralPath $bsp -File -Recurse | Get-FileHash | Select-Object Path,Hash)
  $state.generator_hashes=@(Get-FileHash -LiteralPath (Join-Path $run 'bsp_storage.tcl'),(Join-Path $run 'build_storage_bsp.ps1'),(Join-Path $run 'hardware.xsa'),(Join-Path $run 'patch_sdps_identity.py'),(Join-Path $run 'sdps-identity-patch.json') | Select-Object Path,Hash)
}catch{$state.state='failed';$state.error=$_.Exception.Message;throw}
finally{
  foreach($item in $saved.GetEnumerator()){[Environment]::SetEnvironmentVariable($item.Key,$item.Value,'Process')}
  $state|ConvertTo-Json -Depth 6|Set-Content -LiteralPath (Join-Path $run 'build-status.json') -Encoding utf8
}
Write-Output "M5_STORAGE_BSP_OFFLINE_PASS $(Join-Path $run 'build-status.json')"

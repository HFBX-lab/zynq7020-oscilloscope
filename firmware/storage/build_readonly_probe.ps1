param(
  [string]$Xsa='D:/FPGA_Project/SP2_Storage_Build/hardware/storage.xsa',
  [string]$BspWorkspace='',
  [string]$BuildRoot='D:/FPGA_Project/SP2_Storage_Build/readonly-probe/runs'
)
$ErrorActionPreference='Stop'
$project=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$run=Join-Path $BuildRoot (Get-Date -Format 'yyyyMMdd-HHmmss-ffff')
New-Item -ItemType Directory -Path $run | Out-Null
$stage=Join-Path $run 'source';$tmp=Join-Path $run 'tmp'
New-Item -ItemType Directory -Path $stage,$tmp | Out-Null
$state=@{state='building';board_test='not-run';run=$run;xsa_sha256=(Get-FileHash -LiteralPath $Xsa).Hash}
$prior=@{TEMP=$env:TEMP;TMP=$env:TMP;TMPDIR=$env:TMPDIR}
$env:TEMP=$tmp;$env:TMP=$tmp;$env:TMPDIR=$tmp
try {
  Copy-Item -LiteralPath $Xsa -Destination (Join-Path $run 'hardware.xsa')
  Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'readonly_probe.c'),(Join-Path $PSScriptRoot 'bsp_readonly.tcl'),$PSCommandPath -Destination $stage
  Copy-Item -LiteralPath (Join-Path $project 'scripts/patch_sdps_identity.py') -Destination $stage
  Copy-Item -LiteralPath (Join-Path $project 'firmware/Xilinx.spec') -Destination $stage
  $linker=(Get-Content -LiteralPath (Join-Path $project 'firmware/linker_template.ld') -Raw).Replace('LENGTH = 0x1FF00000','LENGTH = 0x00F00000')
  Set-Content -LiteralPath (Join-Path $stage 'lscript.ld') -Value $linker -Encoding ascii
  if(!$BspWorkspace){
    $BspWorkspace=Join-Path $run 'workspace'
    & D:/Xilinx_2020.2/Vitis/2020.2/bin/xsct.bat (Join-Path $stage 'bsp_readonly.tcl') (Join-Path $run 'hardware.xsa') $BspWorkspace *> (Join-Path $run 'bsp.log')
    if($LASTEXITCODE -or !(Select-String -LiteralPath (Join-Path $run 'bsp.log') -Pattern '^M5_READONLY_BSP_PASS' -Quiet)){throw 'Readonly BSP generation failed'}
  }
  $bspPlatform=Join-Path $BspWorkspace 'sp2_storage_readonly'
  $exported=Get-ChildItem -LiteralPath (Join-Path $bspPlatform 'export/sp2_storage_readonly/hw') -Filter *.xsa -File
  if(@($exported).Count -ne 1 -or (Get-FileHash -LiteralPath $exported.FullName).Hash -ne $state.xsa_sha256){throw 'BSP XSA mismatch'}
  $bsp=Join-Path $bspPlatform 'ps7_cortexa9_0/standalone_domain/bsp/ps7_cortexa9_0'
  Copy-Item -LiteralPath $bsp -Destination (Join-Path $run 'bsp') -Recurse
  $identityPatch=& D:/Python/python.exe (Join-Path $stage 'patch_sdps_identity.py') (Join-Path $run 'bsp/libsrc/sdps_v3_10/src/xsdps_card.c') | Out-String
  if($LASTEXITCODE){throw 'Frozen SD identity patch failed'}
  $identityPatch | Set-Content -LiteralPath (Join-Path $run 'identity-patch.json') -Encoding utf8
  $state.sdps_identity_patch=$identityPatch | ConvertFrom-Json
  $inc=Join-Path $run 'bsp/include';$lib=Join-Path $run 'bsp/lib'
  $config=Get-Content -LiteralPath (Join-Path $inc 'xparameters.h') -Raw
  foreach($check in @('XPAR_XSDPS_NUM_INSTANCES 1','XPAR_PS7_SD_0_HAS_CD 1','XPAR_PS7_SD_0_HAS_WP 0','XPAR_PS7_UART_1_BASEADDR 0xE0001000')){if(!$config.Contains($check)){throw "BSP mapping mismatch: $check"}}
  $ffconfig=Get-Content -LiteralPath (Join-Path $inc 'xparameters.h') -Raw
  if($ffconfig.Contains('#define FILE_SYSTEM_USE_MKFS') -or !$ffconfig.Contains('#define FILE_SYSTEM_READ_ONLY') -or $ffconfig.Contains('#define FILE_SYSTEM_FS_EXFAT')){throw 'Readonly FatFs configuration mismatch'}
  $gcc='D:/Xilinx_2020.2/Vitis/2020.2/gnu/aarch32/nt/gcc-arm-none-eabi/bin/arm-none-eabi-gcc.exe'
  $elf=Join-Path $run 'readonly_probe.elf';$map=Join-Path $run 'readonly_probe.map'
  # Vendor libxil's sdps objects group read and write code in one section.
  # Recompile only this frozen driver with function sections, then GC all
  # unreferenced media-write routines. Never edit the installed vendor tree.
  $objects=@();$objDir=Join-Path $run 'sdps-objects'
  New-Item -ItemType Directory -Path $objDir | Out-Null
  foreach($driver in Get-ChildItem -LiteralPath (Join-Path $run 'bsp/libsrc/sdps_v3_10/src') -File -Filter *.c){
    $object=Join-Path $objDir ($driver.BaseName+'.o');$objects+=$object
    & $gcc -O2 -g -Wall -Wextra -Werror -mcpu=cortex-a9 -mfpu=vfpv3 -mfloat-abi=hard -ffunction-sections -fdata-sections "-I$inc" -c $driver.FullName -o $object *> (Join-Path $objDir ($driver.BaseName+'.log'))
    if($LASTEXITCODE){throw "SD driver compile failed: $($driver.Name)"}
  }
  & $gcc -O2 -g -std=c11 -Wall -Wextra -Werror -mcpu=cortex-a9 -mfpu=vfpv3 -mfloat-abi=hard -ffunction-sections -fdata-sections "-I$inc" (Join-Path $stage 'readonly_probe.c') @objects "-L$lib" "-specs=$(Join-Path $stage 'Xilinx.spec')" "-Wl,-T,$(Join-Path $stage 'lscript.ld')" "-Wl,-Map,$map" '-Wl,--gc-sections' '-Wl,--undefined=disk_write' -o $elf '-Wl,--start-group' -lxilffs -lxil -lgcc -lc -lm '-Wl,--end-group' *> (Join-Path $run 'compile.log')
  if($LASTEXITCODE){throw 'Readonly ARM build failed'}
  $nm=Join-Path (Split-Path $gcc -Parent) 'arm-none-eabi-nm.exe'
  & $nm -g $elf *> (Join-Path $run 'symbols.txt')
  if($LASTEXITCODE){throw 'Symbol audit failed'}
  $symbols=Get-Content -LiteralPath (Join-Path $run 'symbols.txt') -Raw
  if($symbols -match '(?m)\b(?:f_write|f_mkfs|f_unlink|f_rename|diskio\.o|XSdPs_WritePolled)\s*$'){throw 'Unexpected write-capable function in readonly probe'}
  if($symbols -notmatch '(?m)\bT disk_write\s*$' -or $symbols -notmatch '(?m)\bT outbyte\s*$'){throw 'Write/UART override missing'}
  $objdump=Join-Path (Split-Path $gcc -Parent) 'arm-none-eabi-objdump.exe'
  & $objdump -d $elf *> (Join-Path $run 'disassembly.txt')
  if($LASTEXITCODE){throw 'Disassembly failed'}
  $disassembly=Get-Content -LiteralPath (Join-Path $run 'disassembly.txt') -Raw
  if($disassembly -notmatch '(?m)<disk_write>:\r?\n\s*[0-9a-f]+:\s+e3a00002\s+mov\s+r0, #2\r?\n\s*[0-9a-f]+:\s+e12fff1e\s+bx\s+lr\r?\n\r?\n'){throw 'Compiled disk_write is not the constant RES_WRPRT return'}
  $state.state='succeeded-offline-only';$state.elf=$elf;$state.elf_sha256=(Get-FileHash -LiteralPath $elf).Hash
  $state.source_bsp_hashes=@(Get-ChildItem -LiteralPath $stage,(Join-Path $run 'bsp') -Recurse -File | Get-FileHash | Select-Object Path,Hash)
} catch {$state.state='failed';$state.error=$_.Exception.Message;throw}
finally {
  foreach($item in $prior.GetEnumerator()){[Environment]::SetEnvironmentVariable($item.Key,$item.Value,'Process')}
  $state | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $run 'build-status.json') -Encoding utf8
}
Write-Output "M5_READONLY_PROBE_OFFLINE_PASS $run $($state.elf_sha256)"

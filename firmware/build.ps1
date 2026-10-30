param(
  [string]$BuildRoot='D:\FPGA_Project\SP2_PS_Screen_Build',
  [string]$Xsa='',
  [string]$BspWorkspace=''
)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
if(-not $Xsa){$Xsa=Join-Path $BuildRoot 'vivado\sp2_ps_screen.xsa'}
if(-not (Test-Path -LiteralPath $Xsa)){throw "Missing XSA: $Xsa"}
$id=Get-Date -Format 'yyyyMMdd-HHmmss-ffff'
$run=Join-Path $BuildRoot "vitis\$id"
$stage=Join-Path $run 'source'
$workspace=Join-Path $run 'workspace'
if($BspWorkspace){$workspace=$BspWorkspace}
$evidence=Join-Path $projectRoot 'build\ps-firmware'
New-Item -ItemType Directory -Path $stage,$evidence -Force | Out-Null
$oldFiles=@(Get-ChildItem -LiteralPath $evidence -File | Where-Object {$_.Extension -in '.elf','.map','.ld' -or $_.Name -eq 'build-status.json'})
if($oldFiles.Count){
  $history=Join-Path $evidence "history\$id"
  New-Item -ItemType Directory -Path $history -Force | Out-Null
  foreach($old in $oldFiles){Move-Item -LiteralPath $old.FullName -Destination $history}
}
$originalXsa=$Xsa
$snapshot=Join-Path $run 'hardware.xsa'
Copy-Item -LiteralPath $Xsa -Destination $snapshot -Force
$Xsa=$snapshot
Get-ChildItem -LiteralPath (Join-Path $PSScriptRoot 'standalone') -File | Copy-Item -Destination $stage -Force
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'build.tcl') -Destination $run -Force
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'linker_template.ld'),(Join-Path $PSScriptRoot 'Xilinx.spec') -Destination $run -Force
Copy-Item -LiteralPath $PSCommandPath -Destination (Join-Path $run 'build_driver.ps1') -Force
$inputFiles=@(Get-ChildItem $stage -File)+@(Get-Item (Join-Path $run 'build.tcl'),(Join-Path $run 'linker_template.ld'),(Join-Path $run 'Xilinx.spec'),(Join-Path $run 'build_driver.ps1'))
$state=@{state='building';workspace=$workspace;xsa=$originalXsa;xsa_snapshot=$snapshot;xsa_sha256=(Get-FileHash -LiteralPath $snapshot).Hash;artifacts=@();source_hashes=@($inputFiles | Get-FileHash | Select-Object Path,Hash)}
$state | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $evidence 'build-status.json') -Encoding utf8
$previousTemp=@{TEMP=$env:TEMP;TMP=$env:TMP;TMPDIR=$env:TMPDIR}
$tempDir=Join-Path $run 'tmp'
New-Item -ItemType Directory -Path $tempDir -Force | Out-Null
$env:TEMP=$tempDir;$env:TMP=$tempDir;$env:TMPDIR=$tempDir
Push-Location $run
try {
  if($BspWorkspace){
    $oldXsa=Get-ChildItem -LiteralPath (Join-Path $workspace 'sp2_platform\export\sp2_platform\hw') -File -Filter '*.xsa' | Select-Object -First 1
    if(-not $oldXsa -or (Get-FileHash -LiteralPath $oldXsa.FullName).Hash -ne $state.xsa_sha256){throw 'Reusable BSP XSA does not match this input'}
    'BSP_BUILD_PASS: reused existing BSP with identical XSA hash' | Set-Content -LiteralPath build.log
  }else{
    & 'D:\Xilinx_2020.2\Vitis\2020.2\bin\xsct.bat' build.tcl $Xsa $stage $workspace *> build.log
    if($LASTEXITCODE){throw "XSCT build failed; inspect $run\build.log"}
  }
  if(-not (Select-String -LiteralPath build.log -Pattern '^BSP_BUILD_PASS:' -Quiet)){throw 'No BSP build PASS marker'}
  $lib=Get-ChildItem -LiteralPath (Join-Path $workspace 'sp2_platform') -Filter libxil.a -Recurse -File | Where-Object {Test-Path -LiteralPath (Join-Path $_.Directory.Parent.FullName 'include\xparameters.h')} | Select-Object -First 1
  if(-not $lib){throw 'No generated BSP libxil.a'}
  $libdir=$lib.DirectoryName
  $include=Join-Path (Split-Path $libdir -Parent) 'include'
  if(-not (Test-Path (Join-Path $include 'xparameters.h'))){throw 'No generated BSP xparameters.h'}
  $gcc='D:\Xilinx_2020.2\Vitis\2020.2\gnu\aarch32\nt\gcc-arm-none-eabi\bin\arm-none-eabi-gcc.exe'
  foreach($name in @('ps_probe_ocm','screen_bringup')){
    $appdir=Join-Path $run $name
    New-Item -ItemType Directory -Path $appdir -Force | Out-Null
    $linker=Get-Content -LiteralPath (Join-Path $run 'linker_template.ld') -Raw
    if($name -eq 'ps_probe_ocm'){
      if($linker -notmatch '>\s*ps7_ddr_0\b'){throw 'Unexpected linker template'}
      $linker=$linker -replace '>\s*ps7_ddr_0\b','> ps7_ram_0'
    }else{
      if($linker -notmatch 'LENGTH\s*=\s*0x1FF00000'){throw 'Unexpected DDR linker size'}
      $linker=$linker -replace 'LENGTH\s*=\s*0x1FF00000','LENGTH = 0x00F00000'
    }
    $ld=Join-Path $appdir 'lscript.ld'
    Set-Content -LiteralPath $ld -Value $linker -Encoding ascii
    $elf=Join-Path $appdir "$name.elf"
    $map=Join-Path $appdir "$name.map"
    $sources=@(Get-ChildItem -LiteralPath $stage -Filter '*.c' | ForEach-Object {$_.FullName})
    $compilerArgs=@('-pipe','-mcpu=cortex-a9','-mfpu=vfpv3','-mfloat-abi=hard','-O2','-g','-Wall','-Wextra','-ffunction-sections','-fdata-sections',"-I$include","-I$stage","-L$libdir","-specs=$(Join-Path $run 'Xilinx.spec')","-Wl,-T,$ld","-Wl,-Map,$map",'-Wl,--gc-sections')
    if($name -eq 'ps_probe_ocm'){$compilerArgs+='-DPROBE_ONLY'}
    & $gcc @compilerArgs @sources '-Wl,--start-group' -lxil -lgcc -lc '-Wl,--end-group' -o $elf 2>&1 | Tee-Object -FilePath (Join-Path $appdir 'compiler.log')
    if($LASTEXITCODE){throw "ARM compile/link failed: $name"}
    if(-not (Test-Path -LiteralPath $elf)){throw "Missing compiled ELF: $elf"}
    Copy-Item -LiteralPath $elf -Destination $evidence -Force
    $state.artifacts+=@{file="$name.elf";sha256=(Get-FileHash -LiteralPath $elf).Hash}
    Get-ChildItem -LiteralPath (Split-Path $elf) -File | Where-Object {$_.Extension -in '.map','.ld'} | Copy-Item -Destination $evidence -Force
    Copy-Item -LiteralPath $ld -Destination (Join-Path $evidence "$name.ld") -Force
  }
  'FIRMWARE_BUILD_PASS: OCM probe + DDR framebuffer ELF compiled only, NOT RUN' | Add-Content -LiteralPath build.log
  $state.state='succeeded'
} catch {$state.state='failed';$state.failure=$_.Exception.Message;throw}
finally {
  Pop-Location
  foreach($entry in $previousTemp.GetEnumerator()){[Environment]::SetEnvironmentVariable($entry.Key,$entry.Value,'Process')}
  Copy-Item -LiteralPath (Join-Path $run 'build.log') -Destination $evidence -Force
  $state | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $evidence 'build-status.json') -Encoding utf8
}

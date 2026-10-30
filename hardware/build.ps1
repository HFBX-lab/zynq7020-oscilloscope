param(
  [string]$BuildRoot='D:\FPGA_Project\SP2_Scope_Build',
  [ValidateSet('colorbars','grid','waveform','all')][string]$Pattern='all'
)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'artifact-publication.ps1')
$projectRoot=Split-Path $PSScriptRoot -Parent
$stagedHardware=Join-Path $BuildRoot 'hardware'
New-Item -ItemType Directory -Path $stagedHardware -Force | Out-Null
# Vivado 2020.2 crashes in synthesis under this non-ASCII workspace path.
# Stage only this NEW project's hardware sources; original vendor data stays read-only.
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'rtl'),(Join-Path $PSScriptRoot 'constraints') -Destination $stagedHardware -Recurse -Force
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'build.tcl') -Destination $stagedHardware -Force
$variants=if($Pattern -eq 'all'){@('colorbars','grid','waveform')}else{@($Pattern)}
foreach($variant in $variants){
  $subdir=if($variant -eq 'colorbars'){'vivado'}else{"vivado_$variant"}
  $runDir=Join-Path $BuildRoot $subdir
  $evidenceDir=Join-Path $projectRoot "build\$subdir"
  New-Item -ItemType Directory -Path $runDir,$evidenceDir -Force | Out-Null
  $mode=@{colorbars=0;grid=1;waveform=2}[$variant]
  $sources=Get-ChildItem -LiteralPath $stagedHardware -File -Recurse | Where-Object {$_.Extension -in '.v','.xdc','.tcl'} | Get-FileHash -Algorithm SHA256 | Select-Object Path,Hash
  $attempt=Start-FpgaBuildAttempt -EvidenceDir $evidenceDir -Pattern $variant -SourceHashes @($sources) -RunDir $runDir
  $succeeded=$false;$failure=''
  Push-Location $runDir
  try {
    & 'D:\Xilinx_2020.2\Vivado\2020.2\bin\vivado.bat' -mode batch -nojournal -log build.log -source ../hardware/build.tcl -tclargs $mode
    if($LASTEXITCODE){throw "Vivado build failed: $variant"}
    if(-not (Select-String -LiteralPath 'build.log' -Pattern "BUILD_PASS: $variant" -Quiet)){throw "No build PASS marker: $variant"}
    $succeeded=$true
  } catch {
    $failure=$_.Exception.Message
    throw
  } finally {
    Pop-Location
    Complete-FpgaBuildAttempt -Attempt $attempt -RunDir $runDir -Succeeded $succeeded -Failure $failure
  }
}

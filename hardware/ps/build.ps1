param([string]$BuildRoot='D:\FPGA_Project\SP2_PS_Screen_Build')
$ErrorActionPreference='Stop'
$projectRoot=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$stage=Join-Path $BuildRoot 'hardware\ps'
$rtl=Join-Path $BuildRoot 'hardware\rtl'
$run=Join-Path $BuildRoot 'vivado'
$evidence=Join-Path $projectRoot 'build\ps-screen'
New-Item -ItemType Directory -Path $stage,$rtl,$run,$evidence -Force | Out-Null
Get-ChildItem -LiteralPath $PSScriptRoot -File | Where-Object {$_.Extension -in '.v','.tcl','.xdc'} | Copy-Item -Destination $stage -Force
Copy-Item -LiteralPath (Join-Path $projectRoot 'hardware\rtl\lcd_powerup.v') -Destination $rtl -Force
$id=Get-Date -Format 'yyyyMMdd-HHmmss-ffff'
$history=Join-Path $evidence "history\$id"
foreach($name in @('sp2_ps_screen.bit','sp2_ps_screen.xsa','source-hashes.json','build-status.json')){
  $old=Join-Path $evidence $name
  if(Test-Path -LiteralPath $old){New-Item -ItemType Directory -Path $history -Force | Out-Null;Move-Item -LiteralPath $old -Destination $history}
}
foreach($name in @('sp2_ps_screen.bit','sp2_ps_screen.xsa')){
  $old=Join-Path $run $name
  if(Test-Path -LiteralPath $old){$h=Join-Path $run "history\$id";New-Item -ItemType Directory -Path $h -Force | Out-Null;Move-Item -LiteralPath $old -Destination $h}
}
$hashes=Get-ChildItem -LiteralPath (Join-Path $BuildRoot 'hardware') -File -Recurse | Get-FileHash -Algorithm SHA256 | Select-Object Path,Hash
$state=@{state='building';started_utc=[DateTime]::UtcNow.ToString('o');source_hashes=@($hashes);artifacts=@()}
$state | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $evidence 'build-status.json') -Encoding utf8
Push-Location $run
try {
  & 'D:\Xilinx_2020.2\Vivado\2020.2\bin\vivado.bat' -mode batch -nojournal -log build.log -source ../hardware/ps/build.tcl
  if($LASTEXITCODE){throw 'PS screen hardware build failed'}
  if(-not (Select-String -LiteralPath build.log -Pattern '^PS_BUILD_PASS:' -Quiet)){throw 'No PS build PASS marker'}
  foreach($name in @('sp2_ps_screen.bit','sp2_ps_screen.xsa')){
    $p=Join-Path $run $name
    if(-not (Test-Path -LiteralPath $p)){throw "Missing current artifact $name"}
    Copy-Item -LiteralPath $p -Destination $evidence -Force
    $state.artifacts+=@{file=$name;sha256=(Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash}
  }
  $hashes | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $evidence 'source-hashes.json') -Encoding utf8
  $state.state='succeeded'
} catch {$state.state='failed';$state.failure=$_.Exception.Message;throw}
finally {
  Pop-Location
  Get-ChildItem -LiteralPath $run -File | Where-Object {$_.Extension -in '.log','.rpt','.txt'} | Copy-Item -Destination $evidence -Force
  $state.finished_utc=[DateTime]::UtcNow.ToString('o')
  $state | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $evidence 'build-status.json') -Encoding utf8
}

param([string]$BuildRoot='D:\FPGA_Project\SP2_ADC_PS_Build')
$ErrorActionPreference='Stop'
$root=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$id=Get-Date -Format 'yyyyMMdd-HHmmss-ffff'
$run=Join-Path $BuildRoot "runs\$id"
$hardware=Join-Path $run 'hardware'
$work=Join-Path $run 'vivado'
$evidence=Join-Path $root 'build/adc-soc'
New-Item -ItemType Directory -Path $hardware,$work,$evidence -Force | Out-Null
foreach($name in @('ps','adc_soc')){Copy-Item (Join-Path $root "hardware/$name") -Destination $hardware -Recurse}
foreach($name in @('rtl','adc/rtl','adc_ps/rtl')){
  $dst=Join-Path $hardware $name;New-Item -ItemType Directory $dst -Force | Out-Null
  if($name -eq 'rtl'){Copy-Item (Join-Path $root 'hardware/rtl/lcd_powerup.v') -Destination $dst}
  else{Get-ChildItem (Join-Path $root "hardware/$name") -Filter '*.v' -File | Copy-Item -Destination $dst}
}
if(Test-Path "$evidence/build-status.json"){
  $history=Join-Path $evidence "history/$id";New-Item -ItemType Directory $history -Force | Out-Null
  Get-ChildItem $evidence -File | Move-Item -Destination $history
}
$state=@{state='building';scope='offline-only';run=$run;source_hashes=@(Get-ChildItem $hardware -File -Recurse | Get-FileHash | Select-Object Path,Hash);artifacts=@();board='not-run'}
function Save {$state | ConvertTo-Json -Depth 7 | Set-Content "$evidence/build-status.json" -Encoding utf8}
Save
$oldTemp=$env:TEMP;$oldTmp=$env:TMP
New-Item -ItemType Directory "$run/temp" -Force | Out-Null
$env:TEMP="$run/temp";$env:TMP="$run/temp"
Push-Location $work
try {
  & D:/Xilinx_2020.2/Vivado/2020.2/bin/vivado.bat -mode batch -source ../hardware/adc_soc/build.tcl -log build.log -journal build.jou *> console.log
  if($LASTEXITCODE -or !(Select-String build.log -Pattern '^ADC_SOC_BUILD_PASS:' -Quiet)){Get-Content console.log -Tail 28;throw 'ADC candidate platform failed'}
  foreach($name in @('sp2_adc_internal.bit','sp2_adc_internal.xsa')){
    Copy-Item $name -Destination $evidence
    $state.artifacts+=@{file=$name;sha256=(Get-FileHash $name).Hash}
  }
  $state.state='succeeded'
}catch{$state.state='failed';$state.error=$_.Exception.Message;throw}
finally{
  Pop-Location;$env:TEMP=$oldTemp;$env:TMP=$oldTmp
  Get-ChildItem $work -File | Where-Object Extension -in '.log','.rpt','.txt','.tcl' | Copy-Item -Destination $evidence -Force
  Save
}

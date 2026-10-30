param([ValidateSet('all','core','patterns','top','powerup')][string]$Suite='all')
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
$runDir=Join-Path $projectRoot 'build\xsim\offline'
New-Item -ItemType Directory -Path $runDir,(Join-Path $projectRoot 'artifacts\rtl') -Force | Out-Null
$xbin='D:\Xilinx_2020.2\Vivado\2020.2\bin'
Push-Location $runDir
try {
  $sources=@(Get-ChildItem -LiteralPath (Join-Path $projectRoot 'hardware\rtl') -Filter '*.v' | ForEach-Object {$_.FullName})
  $tests=@(Get-ChildItem -LiteralPath (Join-Path $projectRoot 'hardware\sim') -Filter 'tb_*.sv' | ForEach-Object {$_.FullName})
  & "$xbin\xvlog.bat" --sv @sources @tests
  if($LASTEXITCODE){throw 'xvlog failed'}
  & "$xbin\xvlog.bat" 'D:\Xilinx_2020.2\Vivado\2020.2\data\verilog\src\glbl.v'
  if($LASTEXITCODE){throw 'glbl compile failed'}
  $suites=if($Suite -eq 'all'){@('core','patterns','top','powerup')}else{@($Suite)}
  foreach($name in $suites){
    $bench=switch($name){core {'tb_colorbars'} patterns {'tb_patterns'} top {'tb_lcd_top'} powerup {'tb_powerup'}}
    $elabArgs=@($bench,'-s',"${bench}_sim")
    if($name -eq 'top'){$elabArgs+=@('glbl','-L','unisims_ver')}
    & "$xbin\xelab.bat" @elabArgs
    if($LASTEXITCODE){throw "xelab failed: $name"}
    & "$xbin\xsim.bat" "${bench}_sim" -tclbatch ../../../hardware/sim/run.tcl -log "$name.log"
    if($LASTEXITCODE){throw "xsim failed: $name"}
    $pass=switch($name){core {'PASS: two'} patterns {'PASS: all three'} top {'PASS: UNISIM'} powerup {'PASS: production'}}
    if(-not (Select-String -LiteralPath "$name.log" -Pattern $pass -Quiet)){throw "No PASS marker: $name"}
    if(Select-String -LiteralPath "$name.log" -Pattern '^Fatal:|^ERROR:' -Quiet){throw "Simulation error: $name"}
    if($name -eq 'core'){Copy-Item -LiteralPath 'core.log' -Destination ../simulation.log -Force}
  }
} finally {Pop-Location}

param([switch]$Synth,[string]$BuildRoot='D:\FPGA_Project\SP2_ADC_PS_Test')
$ErrorActionPreference='Stop'
$root=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$id=Get-Date -Format 'yyyyMMdd-HHmmss-ffff'
$run=Join-Path $BuildRoot "runs\$id"
$stage=Join-Path $run 'source'
$evidence=Join-Path $root 'build\adc-ps-test'
New-Item -ItemType Directory -Path $stage,$evidence -Force | Out-Null
Get-ChildItem $PSScriptRoot | Copy-Item -Destination $stage -Recurse -Force
Copy-Item (Join-Path $root 'hardware/adc/rtl/adc_snapshot_core.v') -Destination $stage
$state=@{state='building';run=$run;board='not-run';sources=@()}
$state.sources=@(Get-ChildItem $stage -File -Recurse | Get-FileHash | Select-Object Path,Hash)
if(Test-Path "$evidence/status.json"){
  $h=Join-Path $evidence "history\$id";New-Item -ItemType Directory $h -Force | Out-Null
  Get-ChildItem $evidence -File | Move-Item -Destination $h
}
function Save {$state | ConvertTo-Json -Depth 7 | Set-Content "$evidence/status.json" -Encoding utf8}
function Run([string]$exe,[string[]]$arguments,[string]$log){
  & $exe @arguments *> $log
  if($LASTEXITCODE){Get-Content $log -Tail 15;throw "Tool failed: $log"}
}
Save
$oldTemp=$env:TEMP;$oldTmp=$env:TMP
New-Item -ItemType Directory "$run/temp" -Force | Out-Null
$env:TEMP="$run/temp";$env:TMP="$run/temp"
$xb='D:/Xilinx_2020.2/Vivado/2020.2/bin'
Push-Location $run
try {
  Run "$xb/xvlog.bat" @('--sv','source/adc_snapshot_core.v','source/rtl/adc_axi_capture.v','source/sim/tb_adc_axi.sv') 'compile.log'
  foreach($bits in @(4,12)){
    $name="rtl$bits"
    Run "$xb/xelab.bat" @('tb_adc_axi','-generic_top',('"ADDR_BITS='+$bits+'"'),'-s',$name) "$name-elab.log"
    Run "$xb/xsim.bat" @($name,'-tclbatch','source/run.tcl','-log',"$name-sim.log") "$name-run.log"
    if(!(Select-String "$name-sim.log" -Pattern '^PASS: ADC AXI' -Quiet) -or (Select-String "$name-sim.log" -Pattern '^Fatal:|^ERROR:' -Quiet)){Get-Content "$name-sim.log" -Tail 15;throw "Simulation failed: $name"}
    Select-String "$name-sim.log" -Pattern '^PASS:' | ForEach-Object {$_.Line | Write-Host}
  }
  if($Synth){
    Run "$xb/vivado.bat" @('-mode','batch','-source','source/synth.tcl','-log','synth.log','-journal','synth.jou') 'synth-console.log'
    if(!(Select-String synth.log -Pattern '^AXI_SYNTH_PASS' -Quiet)){throw 'Missing synth marker'}
    Run "$xb/xvlog.bat" @('--sv','-d','POST_SYNTH','adc_axi_funcsim.v','source/sim/tb_adc_axi.sv','D:/Xilinx_2020.2/Vivado/2020.2/data/verilog/src/glbl.v') 'netlist-compile.log'
    Run "$xb/xelab.bat" @('tb_adc_axi','glbl','-L','unisims_ver','-generic_top','"ADDR_BITS=12"','-s','netlist') 'netlist-elab.log'
    Run "$xb/xsim.bat" @('netlist','-tclbatch','source/run.tcl','-log','netlist-sim.log') 'netlist-run.log'
    if(!(Select-String netlist-sim.log -Pattern '^PASS: ADC AXI' -Quiet) -or (Select-String netlist-sim.log -Pattern '^Fatal:|^ERROR:' -Quiet)){throw 'Netlist simulation failed'}
    $state.netlist='passed'
  }
  $state.state='succeeded';$state.rtl='passed'
}catch{$state.state='failed';$state.error=$_.Exception.Message;throw}
finally{
  Pop-Location;$env:TEMP=$oldTemp;$env:TMP=$oldTmp
  Get-ChildItem $run -File | Where-Object Extension -in '.log','.rpt' | Copy-Item -Destination $evidence -Force
  Save
}

# Offline-only ring/AXI simulations. Existing netlist result may be reused only
# when both its netlist and testbench exactly match the current staged inputs.
param([string]$BuildRoot='D:/FPGA_Project/SP2_Hardware_Trigger_Build/ring-tests',
 [string]$EvidenceDirectory='build/hardware-trigger-tests',[string]$ReuseNetlistRun='')
$ErrorActionPreference='Stop'
$root=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$run=Join-Path $BuildRoot ('runs/'+(Get-Date -Format 'yyyyMMdd-HHmmss-ffff'))
$stage=Join-Path $run 'source';$evidence=Join-Path $root $EvidenceDirectory
New-Item -ItemType Directory -Path $stage,$evidence -Force | Out-Null
foreach($name in @('adc_cic_decimator.v','adc_trigger_ring_core.v','adc_live_axi.v')){Copy-Item "$PSScriptRoot/$name" $stage}
foreach($name in @('tb_adc_trigger_ring.sv','tb_adc_hw_trigger_axi.sv')){Copy-Item "$PSScriptRoot/sim/$name" $stage}
$state=@{state='running';scope='offline-only';board='not-run';run=$run;tests=@();source_hashes=@(Get-ChildItem $stage -File | Get-FileHash | Select-Object Path,Hash)}
$xbin='D:/Xilinx_2020.2/Vivado/2020.2/bin'
function Run([string]$Exe,[string[]]$Arguments,[string]$Log){& $Exe @Arguments *> $Log;if($LASTEXITCODE){Get-Content $Log -Tail 10;throw "Tool failed:$Log"}}
Push-Location $run
try{
 Run "$xbin/xvlog.bat" @('--sv','source/adc_cic_decimator.v','source/adc_trigger_ring_core.v','source/adc_live_axi.v','source/tb_adc_trigger_ring.sv','source/tb_adc_hw_trigger_axi.sv') 'compile.log'
 foreach($pair in @(@('tb_adc_trigger_ring','ADC_RING_PASS:'),@('tb_adc_hw_trigger_axi','ADC_HW_AXI_PASS:'))){
  foreach($bits in @(4,12)){
   $name=$pair[0]+$bits
   Run "$xbin/xelab.bat" @($pair[0],'-generic_top',('"ADDR_BITS='+$bits+'"'),'-s',$name) "$name-elab.log"
   Run "$xbin/xsim.bat" @($name,'-runall','-log',"$name-sim.log") "$name-run.log"
   if(!(Select-String "$name-sim.log" -Pattern ('^'+$pair[1]) -Quiet) -or (Select-String "$name-sim.log" -Pattern '^Fatal:|^ERROR:' -Quiet)){throw "Simulation failed:$name"}
   $state.tests+=@{name=$name;state='passed';log_sha256=(Get-FileHash "$name-sim.log").Hash}
   Select-String "$name-sim.log" -Pattern ('^'+$pair[1]) | ForEach-Object {$_.Line | Write-Output}
  }
 }
 if($ReuseNetlistRun){
  $baseline=Get-Content "$root/build/hardware-trigger-rtl/status.json" -Raw | ConvertFrom-Json
  if($baseline.state -ne 'succeeded' -or $baseline.netlist -ne 'passed'){throw 'No validated OOC netlist'}
  foreach($name in @('adc_cic_decimator.v','adc_trigger_ring_core.v','adc_live_axi.v')){
   if((Get-FileHash "$($baseline.run)/source/$name").Hash -ne (Get-FileHash "$stage/$name").Hash){throw 'Netlist RTL mismatch'}
  }
  if((Get-FileHash "$ReuseNetlistRun/adc_axi_funcsim.v").Hash -ne (Get-FileHash "$($baseline.run)/adc_axi_funcsim.v").Hash -or
     (Get-FileHash "$ReuseNetlistRun/tb_adc_hw_trigger_axi.sv").Hash -ne (Get-FileHash "$stage/tb_adc_hw_trigger_axi.sv").Hash){throw 'Reused netlist/testbench mismatch'}
  if(!(Select-String "$ReuseNetlistRun/sim.log" -Pattern '^ADC_HW_AXI_PASS: depth=4096' -Quiet) -or (Select-String "$ReuseNetlistRun/sim.log" -Pattern '^Fatal:|^ERROR:' -Quiet)){throw 'Reused hardware netlist simulation didnot pass'}
  $state.tests+=@{name='hardware-enabled-full4096-netlist';state='passed';reused_run=$ReuseNetlistRun;log_sha256=(Get-FileHash "$ReuseNetlistRun/sim.log").Hash;netlist_sha256=(Get-FileHash "$ReuseNetlistRun/adc_axi_funcsim.v").Hash}
 }
 $state.state='succeeded'
}catch{$state.state='failed';$state.error=$_.Exception.Message;throw}
finally{Pop-Location;$state|ConvertTo-Json -Depth 6|Set-Content "$evidence/status.json" -Encoding utf8}

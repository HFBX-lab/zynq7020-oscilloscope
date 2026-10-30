param([string]$BuildRoot='D:/FPGA_Project/SP2_Highspeed_Build/50m/raw-tests',[switch]$Synth)
$ErrorActionPreference='Stop'
$root=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$run=Join-Path $BuildRoot ('runs/'+(Get-Date -Format 'yyyyMMdd-HHmmss-ffff'))
$stage=Join-Path $run 'source';$evidence=Join-Path $root 'build/highspeed-50m-tests'
New-Item -ItemType Directory -Path $stage,$evidence -Force|Out-Null
foreach($name in @('adc_cic_decimator.v','adc_trigger_ring_core.v','adc_live_axi.v','synth-raw50.tcl')){Copy-Item "$PSScriptRoot/$name" $stage}
Copy-Item "$PSScriptRoot/sim/tb_adc_raw50_axi.sv" $stage
$state=@{state='running';scope='offline-only';board='not-run';run=$run;tests=@();source_hashes=@(Get-ChildItem $stage -File|Get-FileHash|Select-Object Path,Hash)}
$xb='D:/Xilinx_2020.2/Vivado/2020.2/bin'
function Run([string]$Exe,[string[]]$Arguments,[string]$Log){& $Exe @Arguments *> $Log;if($LASTEXITCODE){Get-Content $Log -Tail 8;throw "Tool failed:$Log"}}
function Verify([string]$Log,[int]$Bits){
 if(!(Select-String $Log -Pattern '^RAW50_AXI_PASS:' -Quiet) -or (Select-String $Log -Pattern '^Fatal:|^ERROR:' -Quiet)){Get-Content $Log -Tail 8;throw 'RAW50 simulation failed'}
 if(!(Select-String $Log -Pattern '^RAW50_OTR_PASS:' -Quiet)){throw 'RAW50 tagged OTR/read ownership coverage missing'}
 $rates=if($Bits -eq 4){@(0,1,2,3)}else{@(0)}
 foreach($rate in $rates){if(!(Select-String $Log -Pattern ('^RAW50_DC_PASS: depth='+[string](1 -shl $Bits)+' rate_index='+$rate+' ') -Quiet)){throw "RAW50 full DC capture missing at rate index $rate"}}
}
Push-Location $run
try{
 Run "$xb/xvlog.bat" @('--sv','source/adc_cic_decimator.v','source/adc_trigger_ring_core.v','source/adc_live_axi.v','source/tb_adc_raw50_axi.sv') 'compile.log'
 foreach($bits in @(4,12)){
  $name="raw$bits";Run "$xb/xelab.bat" @('tb_adc_raw50_axi','-generic_top',('"ADDR_BITS='+$bits+'"'),'-s',$name) "$name-elab.log"
  Run "$xb/xsim.bat" @($name,'-runall','-log',"$name-sim.log") "$name-run.log";Verify "$name-sim.log" $bits
  $state.tests+=@{name=$name;state='passed';log_sha256=(Get-FileHash "$name-sim.log").Hash;full_data_rates=if($bits -eq 4){@(50000000,6250000,625000,62500,6250,625)}else{@(50000000,6250000,625000)};slow_rates_register_only=($bits -ne 4);tagged_otr_all_words=$true;held_arm_reset_config_and_concurrent_ar_arm=$true}
 }
 if($Synth){
  Run "$xb/vivado.bat" @('-mode','batch','-source','source/synth-raw50.tcl','-log','synth.log','-journal','synth.jou') 'synth-console.log'
  if(!(Select-String synth.log -Pattern '^RAW50_SYNTH_PASS:' -Quiet)){throw 'RAW50 synth marker missing'}
  Run "$xb/xvlog.bat" @('--sv','-d','POST_SYNTH','raw50_axi_funcsim.v','source/tb_adc_raw50_axi.sv','D:/Xilinx_2020.2/Vivado/2020.2/data/verilog/src/glbl.v') 'netlist-compile.log'
  Run "$xb/xelab.bat" @('tb_adc_raw50_axi','glbl','-L','unisims_ver','-generic_top','"ADDR_BITS=12"','-s','raw_netlist') 'netlist-elab.log'
  Run "$xb/xsim.bat" @('raw_netlist','-runall','-log','raw-netlist-sim.log') 'netlist-run.log';Verify 'raw-netlist-sim.log' 12
  $state.tests+=@{name='raw50M-full4096-netlist';state='passed';log_sha256=(Get-FileHash raw-netlist-sim.log).Hash;netlist_sha256=(Get-FileHash raw50_axi_funcsim.v).Hash;full_data_rates=@(50000000,6250000,625000);slow_rates_register_only=$true;tagged_otr_all_words=$true;held_arm_reset_config_and_concurrent_ar_arm=$true}
 }
 $state.state='succeeded';Write-Output "RAW50_TESTS_PASS: $run"
}catch{$state.state='failed';$state.error=$_.Exception.Message;throw}
finally{Pop-Location;$state|ConvertTo-Json -Depth 6|Set-Content "$evidence/status.json" -Encoding utf8}

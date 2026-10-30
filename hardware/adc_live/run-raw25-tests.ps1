param([string]$BuildRoot='D:/FPGA_Project/SP2_Highspeed_Build/25m/raw-tests',[switch]$Synth,[string]$EvidenceDirectory='build/highspeed-25m-tests')
$ErrorActionPreference='Stop'
$root=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$run=Join-Path $BuildRoot ('runs/'+(Get-Date -Format 'yyyyMMdd-HHmmss-ffff'))
$stage=Join-Path $run 'source';$evidence=Join-Path $root $EvidenceDirectory
New-Item -ItemType Directory -Path $stage,$evidence -Force|Out-Null
foreach($name in @('adc_cic_decimator.v','adc_trigger_ring_core.v','adc_live_axi.v','synth-raw25.tcl')){Copy-Item "$PSScriptRoot/$name" $stage}
Copy-Item "$PSScriptRoot/sim/tb_adc_raw25_axi.sv" $stage
$state=@{state='running';scope='offline-only';board='not-run';run=$run;tests=@();source_hashes=@(Get-ChildItem $stage -File|Get-FileHash|Select-Object Path,Hash)}
$xb='D:/Xilinx_2020.2/Vivado/2020.2/bin'
function Run([string]$Exe,[string[]]$Arguments,[string]$Log){& $Exe @Arguments *> $Log;if($LASTEXITCODE){Get-Content $Log -Tail 8;throw "Tool failed:$Log"}}
function Verify([string]$Log){if(!(Select-String $Log -Pattern '^RAW25_AXI_PASS:' -Quiet) -or (Select-String $Log -Pattern '^Fatal:|^ERROR:' -Quiet)){Get-Content $Log -Tail 8;throw 'RAW25 simulation failed'}}
Push-Location $run
try{
 Run "$xb/xvlog.bat" @('--sv','source/adc_cic_decimator.v','source/adc_trigger_ring_core.v','source/adc_live_axi.v','source/tb_adc_raw25_axi.sv') 'compile.log'
 foreach($bits in @(4,12)){
  $name="raw$bits";Run "$xb/xelab.bat" @('tb_adc_raw25_axi','-generic_top',('"ADDR_BITS='+$bits+'"'),'-s',$name) "$name-elab.log"
  Run "$xb/xsim.bat" @($name,'-runall','-log',"$name-sim.log") "$name-run.log";Verify "$name-sim.log"
  $state.tests+=@{name=$name;state='passed';log_sha256=(Get-FileHash "$name-sim.log").Hash}
 }
 if($Synth){
  Run "$xb/vivado.bat" @('-mode','batch','-source','source/synth-raw25.tcl','-log','synth.log','-journal','synth.jou') 'synth-console.log'
  if(!(Select-String synth.log -Pattern '^RAW25_SYNTH_PASS:' -Quiet)){throw 'RAW25 synth marker missing'}
  Run "$xb/xvlog.bat" @('--sv','-d','POST_SYNTH','raw25_axi_funcsim.v','source/tb_adc_raw25_axi.sv','D:/Xilinx_2020.2/Vivado/2020.2/data/verilog/src/glbl.v') 'netlist-compile.log'
  Run "$xb/xelab.bat" @('tb_adc_raw25_axi','glbl','-L','unisims_ver','-generic_top','"ADDR_BITS=12"','-s','raw_netlist') 'netlist-elab.log'
  Run "$xb/xsim.bat" @('raw_netlist','-runall','-log','raw-netlist-sim.log') 'netlist-run.log';Verify 'raw-netlist-sim.log'
  $state.tests+=@{name='raw25M-full4096-netlist';state='passed';log_sha256=(Get-FileHash raw-netlist-sim.log).Hash;netlist_sha256=(Get-FileHash raw25_axi_funcsim.v).Hash}
 }
 $state.state='succeeded';Write-Output "RAW25_TESTS_PASS: $run"
}catch{$state.state='failed';$state.error=$_.Exception.Message;throw}
finally{Pop-Location;$state|ConvertTo-Json -Depth 6|Set-Content "$evidence/status.json" -Encoding utf8}

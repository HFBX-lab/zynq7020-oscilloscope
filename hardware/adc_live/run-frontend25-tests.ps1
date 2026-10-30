param([string]$BuildRoot='D:/FPGA_Project/SP2_Highspeed_Build/25m/frontend')
$ErrorActionPreference='Stop'
$run=Join-Path $BuildRoot ('runs/'+(Get-Date -Format 'yyyyMMdd-HHmmss-ffff'))
New-Item -ItemType Directory $run -Force|Out-Null
Copy-Item "$PSScriptRoot/adc_frontend.v","$PSScriptRoot/adc_cic_decimator.v","$PSScriptRoot/sim/tb_adc_frontend_25m.sv","$PSScriptRoot/sim/tb_adc_frontend_10m.sv" $run
$state=@{state='running';scope='offline-only';run=$run;tests=@();sources=@(Get-ChildItem $run -File|Get-FileHash|Select-Object Path,Hash)}
$xb='D:/Xilinx_2020.2/Vivado/2020.2/bin'
function Run([string]$Exe,[string[]]$Arguments,[string]$Log){& $Exe @Arguments *> $Log;if($LASTEXITCODE){Get-Content $Log -Tail 8;throw "Tool failed:$Log"}}
Push-Location $run
try{
 Run "$xb/xvlog.bat" @('--sv','adc_cic_decimator.v','adc_frontend.v','tb_adc_frontend_25m.sv','tb_adc_frontend_10m.sv',"$xb/../data/verilog/src/glbl.v") 'compile.log'
 foreach($top in @('tb_adc_frontend_25m','tb_adc_frontend_25m_late','tb_adc_frontend_25m_early','tb_adc_frontend_10m','tb_adc_frontend_10m_bad')){
  Run "$xb/xelab.bat" @($top,'glbl','-L','unisims_ver','-s',$top) "$top-elab.log"
  Run "$xb/xsim.bat" @($top,'-runall','-log',"$top-sim.log") "$top-run.log"
  $text=Get-Content "$top-sim.log" -Raw
  $negative=$top -match 'late|early|bad'
  if($negative){if($text -notmatch 'Fatal: ADC(?:25)?_RETURN_APERTURE_ERROR'){throw "Wrong negative failure:$top"}}
  elseif($text -notmatch 'FRONTEND_(?:25|10)M_PASS:' -or $text -match '^Fatal:|^ERROR:'){throw "Frontend positive failed:$top"}
  $state.tests+=@{name=$top;state=$(if($negative){'expected-aperture-failure'}else{'passed'});sha256=(Get-FileHash "$top-sim.log").Hash}
 }
 $state.state='succeeded';Write-Output "FRONTEND25_TESTS_PASS: $run"
}catch{$state.state='failed';$state.error=$_.Exception.Message;throw}
finally{Pop-Location;$state|ConvertTo-Json -Depth 5|Set-Content "$run/status.json" -Encoding utf8}

param([string]$BuildRoot='D:/FPGA_Project/SP2_Highspeed_Build/50m/cic-pipeline')
$ErrorActionPreference='Stop'
$run=Join-Path $BuildRoot ('runs/'+(Get-Date -Format 'yyyyMMdd-HHmmss-ffff'))
New-Item -ItemType Directory $run -Force|Out-Null
Copy-Item "$PSScriptRoot/adc_cic_decimator.v","$PSScriptRoot/sim/tb_adc_cic_pipeline.sv" $run
$xb='D:/Xilinx_2020.2/Vivado/2020.2/bin'
Push-Location $run
try{
 & "$xb/xvlog.bat" --sv adc_cic_decimator.v tb_adc_cic_pipeline.sv *> compile.log
 if($LASTEXITCODE){throw 'CIC pipeline compile failed'}
 & "$xb/xelab.bat" tb_adc_cic_pipeline -s cic_pipeline *> elab.log
 if($LASTEXITCODE){Get-Content elab.log -Tail 8;throw 'CIC pipeline elab failed'}
 & "$xb/xsim.bat" cic_pipeline -runall -log simulation.log *> run.log
 if($LASTEXITCODE -or !(Select-String simulation.log -Pattern '^CIC_PIPELINE_PASS:' -Quiet) -or (Select-String simulation.log -Pattern '^Fatal:|^ERROR:' -Quiet)){Get-Content simulation.log -Tail 8;throw 'CIC pipeline simulation failed'}
 Write-Output "CIC_PIPELINE_TESTS_PASS: $run"
}finally{Pop-Location}

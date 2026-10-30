param([string]$BuildRoot='D:\FPGA_Project\SP2_ADC_Live_Build')
$ErrorActionPreference='Stop'
$projectRoot=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$run=Join-Path $BuildRoot 'frontend_sim'
New-Item -ItemType Directory -Path $run -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'adc_cic_decimator.v'),(Join-Path $PSScriptRoot 'adc_frontend.v'),(Join-Path $PSScriptRoot 'sim/tb_adc_frontend.sv') -Destination $run -Force
$xbin='D:\Xilinx_2020.2\Vivado\2020.2\bin'
Push-Location $run
try {
  & "$xbin\xvlog.bat" --sv adc_cic_decimator.v adc_frontend.v tb_adc_frontend.sv
  if($LASTEXITCODE){throw 'RGB IO xvlog failed'}
  & "$xbin\xvlog.bat" 'D:\Xilinx_2020.2\Vivado\2020.2\data\verilog\src\glbl.v'
  if($LASTEXITCODE){throw 'glbl compile failed'}
  & "$xbin\xelab.bat" tb_adc_frontend glbl -L unisims_ver -s adc_frontend_sim
  if($LASTEXITCODE){throw 'RGB IO xelab failed'}
  & "$xbin\xsim.bat" adc_frontend_sim -runall -log simulation.log
  if($LASTEXITCODE -or -not (Select-String simulation.log -Pattern '^ADC_FRONTEND_PASS:' -Quiet)){throw 'RGB IO simulation failed'}
} finally {Pop-Location}

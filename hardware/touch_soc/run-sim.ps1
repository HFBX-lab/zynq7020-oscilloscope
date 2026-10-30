param([string]$BuildRoot='D:\FPGA_Project\SP2_Touch_Build')
$ErrorActionPreference='Stop'
$projectRoot=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$run=Join-Path $BuildRoot 'pad_sim'
New-Item -ItemType Directory -Path $run -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'touch_pads.v'),(Join-Path $PSScriptRoot 'tb_touch_pads.sv') -Destination $run -Force
$xbin='D:\Xilinx_2020.2\Vivado\2020.2\bin'
Push-Location $run
try {
  & "$xbin\xvlog.bat" --sv touch_pads.v tb_touch_pads.sv
  if($LASTEXITCODE){throw 'RGB IO xvlog failed'}
  & "$xbin\xvlog.bat" 'D:\Xilinx_2020.2\Vivado\2020.2\data\verilog\src\glbl.v'
  if($LASTEXITCODE){throw 'glbl compile failed'}
  & "$xbin\xelab.bat" tb_touch_pads glbl -L unisims_ver -s touch_pad_sim
  if($LASTEXITCODE){throw 'RGB IO xelab failed'}
  & "$xbin\xsim.bat" touch_pad_sim -runall -log simulation.log
  if($LASTEXITCODE -or -not (Select-String simulation.log -Pattern '^TOUCH_PADS_TEST_PASS:' -Quiet)){throw 'RGB IO simulation failed'}
} finally {Pop-Location}

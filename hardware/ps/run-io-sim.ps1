param([string]$BuildRoot='D:\FPGA_Project\SP2_PS_Screen_Build')
$ErrorActionPreference='Stop'
$projectRoot=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$run=Join-Path $BuildRoot 'io_sim'
New-Item -ItemType Directory -Path $run -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'screen_rgb_io.v'),(Join-Path $PSScriptRoot 'tb_rgb_io.sv'),(Join-Path $projectRoot 'hardware\rtl\lcd_powerup.v') -Destination $run -Force
$xbin='D:\Xilinx_2020.2\Vivado\2020.2\bin'
Push-Location $run
try {
  & "$xbin\xvlog.bat" --sv screen_rgb_io.v lcd_powerup.v tb_rgb_io.sv
  if($LASTEXITCODE){throw 'RGB IO xvlog failed'}
  & "$xbin\xvlog.bat" 'D:\Xilinx_2020.2\Vivado\2020.2\data\verilog\src\glbl.v'
  if($LASTEXITCODE){throw 'glbl compile failed'}
  & "$xbin\xelab.bat" tb_rgb_io glbl -L unisims_ver -s rgb_io_sim
  if($LASTEXITCODE){throw 'RGB IO xelab failed'}
  & "$xbin\xsim.bat" rgb_io_sim -runall -log simulation.log
  if($LASTEXITCODE -or -not (Select-String simulation.log -Pattern '^PASS: PS RGB' -Quiet)){throw 'RGB IO simulation failed'}
} finally {Pop-Location}

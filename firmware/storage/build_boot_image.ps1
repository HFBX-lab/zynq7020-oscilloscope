param(
  [string]$BuildRoot='D:/FPGA_Project/SP2_Storage_Build/boot-update',
  [string]$Xsa='D:/FPGA_Project/SP2_Storage_Build/hardware/storage.xsa'
)
$ErrorActionPreference='Stop'
$project=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$app=Join-Path $project 'build/storage-ui-firmware/freertos/scope_lvgl_freertos.elf'
$bit=Join-Path $project 'build/highspeed-50m-soc/sp2_adc_live.bit'
# This entry point only creates local files and invokes offline build tools.
& D:/Python/python.exe (Join-Path $PSScriptRoot 'prepare_boot_image.py') --root $BuildRoot --xsa $Xsa --app $app --bit $bit
if($LASTEXITCODE){throw 'Offline storage BOOT build/audit failed'}

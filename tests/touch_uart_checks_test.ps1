$ErrorActionPreference='Stop'
. "$PSScriptRoot/../scripts/touch_uart_checks.ps1"
$old="APP_DIAG os=freertos ms=1300000`n"*3
if(@(Get-TouchReadyLines $old).Count){throw 'Old diagnostics must not satisfy readiness'}
$ready=$old+"LVGL_READY os=freertos version=9.2.2 lang=zh-CN input=PANEL_I2C_AND_UART frames=1`n"
if(@(Get-TouchReadyLines $ready).Count){throw 'Need diagnostics after new marker'}
$new=$ready+("APP_DIAG os=freertos ms=1300 locked=1`n"*3)
if(@(Get-TouchReadyLines $new).Count -ne 3){throw 'New diagnostics missing'}
$partial=$ready+("APP_DIAG os=freertos ms=1300 locked=1`n"*2)+"APP_DIAG os=freertos ms=3300 locked=1 pix"
if(@(Get-TouchReadyLines $partial).Count -ne 2){throw 'Incomplete UART diagnostic must not count as a complete third sample'}
Write-Output 'TOUCH_UART_CHECKS_PASS: stale UART cannot satisfy new-program readiness'

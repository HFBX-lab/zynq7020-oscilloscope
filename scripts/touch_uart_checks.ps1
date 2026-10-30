function Get-TouchReadyLines([string]$Captured) {
 $ready=[regex]::Match($Captured,'LVGL_READY os=freertos[^\r\n]*input=PANEL_I2C_AND_UART[^\r\n]*')
 if(!$ready.Success){return @()}
 return @([regex]::Matches($Captured.Substring($ready.Index+$ready.Length),'(?m)^APP_DIAG os=freertos[^\r\n]+\r?\n'))
}

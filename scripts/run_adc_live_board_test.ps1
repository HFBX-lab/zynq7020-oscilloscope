# Temporary PL/RAM board test. No Flash writes. -PrepareOnly is offline.
param([switch]$PrepareOnly,[string]$InputDescription='')
$ErrorActionPreference='Stop'
if(!$PrepareOnly -and !$InputDescription){throw 'Analog input description required'}
$root=Split-Path $PSScriptRoot -Parent
. "$root/scripts/touch_uart_checks.ps1"
. "$root/scripts/adc_live_session_helpers.ps1"
& D:/Python/python.exe -X utf8 "$root/scripts/validate_adc_live_build.py"
if($LASTEXITCODE){throw 'Touch build audit failed'}
$proof=Get-Content "$root/build/adc-live-soc/validation.json" -Raw | ConvertFrom-Json
$run='D:/FPGA_Project/SP2_ADC_Live_Build/board/'+(Get-Date -Format 'yyyyMMdd-HHmmss-ffff')
New-Item -ItemType Directory $run -Force | Out-Null
foreach($pair in @(@('build/adc-live-soc/sp2_adc_live.bit','platform.bit'),@('build/adc-live-firmware/freertos/scope_lvgl_freertos.elf','application.elf'),@('build/adc-live-soc/ps7_init.tcl','ps7_init.tcl'),@('firmware/board_init_helpers.tcl','board_init_helpers.tcl'))){
 Copy-Item -LiteralPath (Join-Path $root $pair[0]) -Destination "$run/$($pair[1])"
}
foreach($p in @(@('platform.bit',$proof.bit_sha256),@('application.elf',$proof.elf_sha256),@('ps7_init.tcl',$proof.ps7_init_sha256),@('board_init_helpers.tcl',$proof.board_init_helpers_sha256))){
 if((Get-FileHash "$run/$($p[0])").Hash -ne $p[1]){throw 'Touch snapshot checksum changed'}
}
(Get-Content "$root/firmware/touch/boot.tcl" -Raw).Replace('@RUN@',$run).Replace('TOUCH_APP_RAM_LOADED','ADC_LIVE_APP_RAM_LOADED') | Set-Content "$run/boot.tcl" -Encoding ascii
Copy-Item -LiteralPath "$root/scripts/adc_live_session_helpers.ps1" -Destination "$run/adc_live_session_helpers.ps1"
Copy-Item -LiteralPath "$root/scripts/analyze_adc_live_record.py" -Destination "$run/analyze_adc_live_record.py"
Copy-Item -LiteralPath $PSCommandPath -Destination "$run/loader.ps1"
Copy-Item -LiteralPath "$root/scripts/touch_uart_checks.ps1" -Destination "$run/touch_uart_checks.ps1"
$state=@{state='prepared-offline';run=$run;flash_written=$false;real_adc='not-run';analog_input=$InputDescription;physical_panel='awaiting-user-observation';records=@();resume_needed=$false;
 files=@(Get-ChildItem $run -File | Get-FileHash | Select-Object Path,Hash);loader_sha256=(Get-FileHash $PSCommandPath).Hash}
$statePath="$root/build/adc-live-soc/board-session.json"
if($PrepareOnly){$state | ConvertTo-Json -Depth 8 | Set-Content "$root/build/adc-live-soc/prepared-session.json" -Encoding utf8;Write-Output "ADC_LIVE_PREPARED_ONLY: $run";return}
if(Get-NetTCPConnection -LocalPort 3121 -State Listen -ErrorAction SilentlyContinue){throw 'Port3121 already in use; do not take ownership of another server'}
$port=[IO.Ports.SerialPort]::new('COM18',115200,[IO.Ports.Parity]::None,8,[IO.Ports.StopBits]::One)
$port.DtrEnable=$false;$port.RtsEnable=$false;$port.ReadBufferSize=65536
$captured=[Text.StringBuilder]::new();$owned=$null;$launcher=$null;$late=$null
function Pull {
 $chunk=$port.ReadExisting();if($chunk.Length){[void]$captured.Append($chunk);[IO.File]::AppendAllText("$run/uart.log",$chunk)}
 if($captured.ToString() -match 'APP_FATAL|DDR_FAIL|VIDEO_PRESENT_FAIL|VIDEO_STOP_TIMEOUT|ADC_LIVE_FAIL'){throw 'Touch session application failure; inspect UART'}
}
try {
 $port.Open();$port.DiscardInBuffer()
 $launcher=Start-Process -FilePath D:/Xilinx_2020.2/Vitis/2020.2/bin/hw_server.bat -ArgumentList '-s','tcp::3121' -WindowStyle Hidden -PassThru -RedirectStandardOutput "$run/server.log" -RedirectStandardError "$run/server-error.log"
 $watch=[Diagnostics.Stopwatch]::StartNew()
 do {
  $listen=Get-NetTCPConnection -LocalPort 3121 -State Listen -ErrorAction SilentlyContinue | Select-Object -First 1
  if(!$listen){Start-Sleep -Milliseconds 100}
  if($watch.Elapsed.TotalSeconds -gt 15){throw 'hw_server startup timeout'}
 }while(!$listen)
 $p=Get-CimInstance Win32_Process -Filter "ProcessId=$($listen.OwningProcess)"
 if($p.Name -ne 'hw_server.exe' -or $p.ExecutablePath -ne 'D:\Xilinx_2020.2\Vitis\2020.2\bin\unwrapped\win64.o\hw_server.exe'){throw 'Unexpected hw_server binary'}
 $ancestor=$p; $isChild=$false
 for($i=0;$i -lt 5 -and $ancestor;$i++){
  if($ancestor.ParentProcessId -eq $launcher.Id){$isChild=$true;break}
  $ancestor=Get-CimInstance Win32_Process -Filter "ProcessId=$($ancestor.ParentProcessId)"
 }
 if(!$isChild){throw 'Refusing unrelated server'}
 $owned=@{server_pid=$p.ProcessId;parent_pid=$p.ParentProcessId;created_utc_ticks=$p.CreationDate.ToUniversalTime().Ticks}
 $owned | ConvertTo-Json | Set-Content "$run/owned-server.json" -Encoding utf8
 & D:/Xilinx_2020.2/Vitis/2020.2/bin/xsct.bat "$run/boot.tcl" *> "$run/boot.log"
 if($LASTEXITCODE -or !(Select-String "$run/boot.log" -Pattern '^ADC_LIVE_APP_RAM_LOADED' -Quiet)){throw 'Touch JTAG load failed'}
 $watch.Restart()
 do {
  Pull
  if($watch.Elapsed.TotalSeconds -gt 40){throw 'Touch application startup timeout'}
  $diags=@(Get-TouchReadyLines $captured.ToString())
  if($diags.Count -lt 3){Start-Sleep -Milliseconds 50}
 }while($diags.Count -lt 3)
 if($captured.ToString() -notmatch 'LVGL_READY os=freertos.*input=PANEL_I2C_AND_UART'){throw 'Physical input app did not start'}
 foreach($d in $diags){if($d.Value -notmatch '(?:^| )locked=1(?: |$)' -or $d.Value -notmatch 'pixel_locked=1' -or $d.Value -notmatch 'dma_errors=00000000'){throw "Video diagnostic failed: $d"}}
 if($captured.ToString() -notmatch 'ADC_LIVE_READY: EXTERNAL BANK33/J6'){throw 'External ADC marker missing'}
 $state.state='running-external-adc-export'
 $state.controller=if($captured.ToString() -match 'TOUCH_STATUS available=1[^\r\n]+'){$Matches[0]}else{'unidentified'}
 if($state.controller -eq 'unidentified'){$state.state='display-running-controller-unidentified'}
 $state.diagnostic_samples=$diags.Count
 Export-AdcLiveRecords $root $run $port $captured $state
 $state.state='verified-board-external-ddr-transfer-awaiting-signal-and-panel-review'
 $state.real_adc='external-records-exported-CRC-verified' 
}catch{$state.state='failed';$state.error=$_.Exception.Message;throw}
finally{
 try {if($port.IsOpen){if($state.resume_needed){$port.Write("resume`n");Start-Sleep -Milliseconds 150};try{Pull}catch{$late=$_;$state.state='failed';$state.error=$_.Exception.Message};$port.Close()}}finally{$port.Dispose()}
 if($owned){
  $p=Get-CimInstance Win32_Process -Filter "ProcessId=$($owned.server_pid)"
  if($p -and $p.Name -eq 'hw_server.exe' -and $p.ParentProcessId -eq $owned.parent_pid -and $p.CreationDate.ToUniversalTime().Ticks -eq $owned.created_utc_ticks){Stop-Process -Id $owned.server_pid;$state.server='owned-server-stopped'}
  else {$state.server='identity-changed-or-already-stopped'}
 }
 if(Test-Path "$run/uart.log"){$state.uart_sha256=(Get-FileHash "$run/uart.log").Hash}
 $state | ConvertTo-Json -Depth 8 | Set-Content $statePath -Encoding utf8
 $state | ConvertTo-Json -Depth 8 | Set-Content "$run/session.json" -Encoding utf8
}
if($late){throw $late}
Write-Output "ADC_LIVE_BOARD_STARTED: $($state.state); controller=$($state.controller); no Flash written"

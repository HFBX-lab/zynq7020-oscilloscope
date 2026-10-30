# Observe the already-loaded M1 firmware. No processor reset or PL/RAM download.
param([string]$InputDescription,[switch]$HardwareTrigger,[string]$BoardRoot='D:/FPGA_Project/SP2_Long_Timebase_Build/board')
$ErrorActionPreference='Stop'
if(!$InputDescription){throw 'Current known low-frequency input required'}
$root=Split-Path $PSScriptRoot -Parent
$run=$BoardRoot+'/'+(Get-Date -Format 'yyyyMMdd-HHmmss-ffff')+'-lowfreq'
New-Item -ItemType Directory -Path $run -Force | Out-Null
foreach($name in @('acq_ui_session_helpers.ps1','long_timebase_session_helpers.ps1','analyze_adc_live_record.py','decode_board_frame.py','hardware_trigger_session_helpers.ps1')){Copy-Item "$root/scripts/$name" "$run/$name"}
Copy-Item $PSCommandPath "$run/observer.ps1"
. "$run/acq_ui_session_helpers.ps1"
. "$run/long_timebase_session_helpers.ps1"
if($HardwareTrigger){. "$run/hardware_trigger_session_helpers.ps1"}
$state=@{state='starting';analog_input=$InputDescription;flash_written=$false;download_performed=$false;controls=@();long_records=@();resume_needed=$false;
 files=@(Get-ChildItem $run -File | Get-FileHash | Select-Object Path,Hash)}
if(Get-NetTCPConnection -LocalPort 3121 -State Listen -ErrorAction SilentlyContinue){throw 'Port3121 already owned'}
$port=[IO.Ports.SerialPort]::new('COM18',115200,[IO.Ports.Parity]::None,8,[IO.Ports.StopBits]::One)
$port.DtrEnable=$false;$port.RtsEnable=$false;$port.ReadBufferSize=65536
$captured=[Text.StringBuilder]::new();$owned=$null;$late=$null
function Pull {
 $chunk=$port.ReadExisting();if($chunk.Length){[void]$captured.Append($chunk);[IO.File]::AppendAllText("$run/uart.log",$chunk)}
 if($captured.ToString() -match 'APP_FATAL|APP_LOG_ERROR|DDR_FAIL|VIDEO_PRESENT_FAIL|ADC_LIVE_FAIL'){throw 'Application failure in low-frequency observation'}
}
try {
 $port.Open()
 $launcher=Start-Process -FilePath D:/Xilinx_2020.2/Vitis/2020.2/bin/hw_server.bat -ArgumentList '-s','tcp::3121' -WindowStyle Hidden -PassThru -RedirectStandardOutput "$run/server.log" -RedirectStandardError "$run/server-error.log"
 $clock=[Diagnostics.Stopwatch]::StartNew();$listen=$null
 do {
  $listen=Get-NetTCPConnection -LocalPort 3121 -State Listen -ErrorAction SilentlyContinue | Select-Object -First 1
  if(!$listen){Start-Sleep -Milliseconds 100};if($clock.Elapsed.TotalSeconds -gt 15){throw 'Server startup timeout'}
 }while(!$listen)
 $p=Get-CimInstance Win32_Process -Filter "ProcessId=$($listen.OwningProcess)"
 if($p.Name -ne 'hw_server.exe' -or $p.ExecutablePath -ne 'D:\Xilinx_2020.2\Vitis\2020.2\bin\unwrapped\win64.o\hw_server.exe'){throw 'Wrong server binary'}
 $ancestor=$p;$isChild=$false
 for($i=0;$i -lt 5 -and $ancestor;$i++){if($ancestor.ParentProcessId -eq $launcher.Id){$isChild=$true;break};$ancestor=Get-CimInstance Win32_Process -Filter "ProcessId=$($ancestor.ParentProcessId)"}
 if(!$isChild){throw 'Refusing unrelated server'}
 $owned=@{server_pid=$p.ProcessId;parent_pid=$p.ParentProcessId;created_utc_ticks=$p.CreationDate.ToUniversalTime().Ticks}
 $owned | ConvertTo-Json | Set-Content "$run/owned-server.json" -Encoding utf8
 Send-AcqCommands @('source 2','trigsource 2','triggermode auto','dual 1','cursors 0','acqstop')
 if($HardwareTrigger){
  [void](Set-LongTimebase 2);Send-AcqCommands @('trigger rise 500','triggermode normal')
  for($attempt=0;$attempt -lt 3;$attempt++){
   Send-AcqCommands @('single');$s=Wait-AcqState {param($x) $x.ui.single_complete -eq '1' -and $x.adc.hw_matched -eq '1'} 5
   Assert-HardwareRecord $s $true
   if([uint32]$s.adc.hw_seen -ge 12288){break}
  }
  if([uint32]$s.adc.hw_seen -lt 12288){throw '1Hz short-timebase trigger didnot demonstrate multiple ring wraps'}
  Note-Acq 'hardware-1Hz-fast-window-captures-after-multiple-ring-wraps' $s
  Export-LongRecord 'hw-fast-1hz' $true
 }
 [void](Set-LongTimebase 12)
 Send-AcqCommands @('trigger rise 4500','single')
 $s=Wait-AcqState {param($x) $x.adc.pending -eq '1' -and $x.adc.applied_rate -eq '625' -and $x.ui.single_armed -eq '1'}
 $raw=[uint32]$s.adc.id;$clock.Restart()
 if($HardwareTrigger){
  $s=Wait-HardwareWaitingProgress 8192 20
  $state.slow_wait_elapsed_seconds=$clock.Elapsed.TotalSeconds
  if($state.slow_wait_elapsed_seconds -lt 12 -or $s.adc.id -ne [string]$raw -or $s.ui.single_complete -ne '0'){throw 'Hardware slow single didnot wait across two fullrings'}
  Note-Acq 'hardware-slow-unmatched-single-two-full-rings-no-publish' $s
 }else{
  $s=Wait-AcqState {param($x) [uint32]$x.adc.id -gt $raw -and $x.ui.single_armed -eq '1'} 13
  $state.slow_capture_elapsed_seconds=$clock.Elapsed.TotalSeconds
  if($state.slow_capture_elapsed_seconds -lt 6 -or $state.slow_capture_elapsed_seconds -gt 9){throw 'Real slow record duration incompatible with4096at625Sa/s'}
  if($s.ui.single_complete -ne '0'){throw 'Out-of-range low-frequency trigger falsely completed'}
  Note-Acq 'measured-slow-record-duration-and-unmatched-single' $s
 }
 Send-AcqCommands @('trigger rise 500')
 $s=Wait-AcqState {param($x) $x.ui.single_complete -eq '1' -and $x.ui.running -eq '0' -and $x.adc.display_rate -eq '625' -and $x.adc.display_epoch -eq $x.ui.epoch} 13
 Assert-LongProfile $s 12 $true;if($HardwareTrigger){Assert-HardwareRecord $s $true};Note-Acq '1Hz-500ms-div-positive-single-and20percent-alignment' $s
 Export-LongRecord 'lowfreq-1hz-500ms' $true
 $report=Get-Content "$run/lowfreq-1hz-500ms.json" -Raw | ConvertFrom-Json
 $frequency=$report.channels[1].frequency_hz
 if($null -eq $frequency -or $frequency -lt .95 -or $frequency -gt 1.05){throw 'Independent raw frequency does not match nominal1Hz input'}
 $state.independent_ch2_frequency_hz=$frequency
 $held=Read-AcqState;$heldId=$held.ui.display_id;$heldCrc=$held.adc.display_crc;$heldEpoch=$held.ui.epoch
 Send-AcqCommands @('cursors 1','cursor 250 750');$s=Read-AcqState
 $delta=[double]$s.ui.cursor_dt_us;$inverse=[double]$s.ui.cursor_inv_hz
 if([double]::IsNaN($delta) -or [double]::IsInfinity($delta) -or [double]::IsNaN($inverse) -or [double]::IsInfinity($inverse)){throw 'Non-finite slow cursor time-axis result'}
 if([math]::Abs([double]$s.ui.cursor_dt_us-2500000) -gt .0001 -or [math]::Abs([double]$s.ui.cursor_inv_hz-.4) -gt .000001 -or $s.ui.cursor_valid -ne '1'){throw 'Slow cursor time-axis mismatch'}
 if($s.ui.display_id -ne $heldId -or $s.adc.display_crc -ne $heldCrc -or $s.ui.epoch -ne $heldEpoch){throw 'Slow cursor changed frozen acquisition'}
 Note-Acq 'slow-cursor2.5seconds-and0.4Hz-keeps-frozen-record' $s
 Export-LongRecord 'lowfreq-cursors' $true
 Send-AcqCommands @('source 1','source 2');$s=Read-AcqState
 if($s.ui.single_complete -ne '1' -or $s.ui.display_id -ne $heldId -or $s.adc.display_crc -ne $heldCrc -or $s.ui.epoch -ne $heldEpoch){throw 'Slow measurement switch changed completed single'}
 Note-Acq 'slow-measurement-selection-keeps-single-and-CRC' $s
 Send-AcqCommands @('cursors 0','triggermode auto','acqrun')
 $s=Wait-AcqState {param($x) $x.ui.running -eq '1' -and $x.ui.trig_normal -eq '0' -and [uint32]$x.ui.display_id -gt [uint32]$heldId -and $x.ui.triggered -eq '1'} 13
 Assert-LongProfile $s 12 $true;if($HardwareTrigger){Assert-HardwareRecord $s $true};Note-Acq 'final-CH2-1Hz-500ms-div-auto-running' $s
 $state.state=if($HardwareTrigger){'verified-board-hardware-trigger-low-frequency-awaiting-user-panel-feedback'}else{'verified-board-low-frequency-awaiting-user-panel-feedback'}
}catch{$state.state='failed';$state.error=$_.Exception.Message;$late=$_}
finally {
 if($port.IsOpen){if($state.resume_needed){try{$port.Write("resume`n")}catch{}};$port.Close()};$port.Dispose()
 if($owned){
  $p=Get-CimInstance Win32_Process -Filter "ProcessId=$($owned.server_pid)"
  if($p -and $p.Name -eq 'hw_server.exe' -and $p.ParentProcessId -eq $owned.parent_pid -and $p.CreationDate.ToUniversalTime().Ticks -eq $owned.created_utc_ticks){Stop-Process -Id $owned.server_pid;$state.server='owned-server-stopped'}
 }
 if(Test-Path "$run/uart.log"){$state.uart_sha256=(Get-FileHash "$run/uart.log").Hash}
 $state | ConvertTo-Json -Depth 8 | Set-Content "$run/session.json" -Encoding utf8
}
if($late){throw $late}
Write-Output "LONG_TIMEBASE_LOWFREQ_PASS: $run; no download or Flash"

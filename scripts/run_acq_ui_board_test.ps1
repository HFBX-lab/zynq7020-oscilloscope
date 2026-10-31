# Temporary PL/RAM board test. No Flash writes. -PrepareOnly is offline.
param([switch]$PrepareOnly,[string]$InputDescription='',
 [string]$FirmwareDirectory='build/acq-ui-firmware/freertos',
 [string]$BoardRoot='D:/FPGA_Project/SP2_Trigger_Build/board',
 [switch]$DualChannel,[switch]$Cursors,[switch]$Channels,[switch]$IndependentTrigger,
 [switch]$LongTimebase,[switch]$HardwareTrigger,[switch]$Highspeed10M,[switch]$Highspeed25M,[switch]$Highspeed50M,[switch]$Measurements,[switch]$Autoset,[switch]$Memory,[switch]$Parameters,[switch]$ZeroMarkers,[switch]$Spectrum,[string]$HardwareDirectory='build/adc-live-soc',
 [string]$RtlDirectory='build/adc-live-test',
 [string]$PlacementFile='D:/FPGA_Project/SP2_ADC_Live_Build/io-audit/capture-placements.csv',
 [switch]$SpectrumRetest,[string]$PriorSpectrumRun='',[switch]$Timing)
$ErrorActionPreference='Stop'
if(!$PrepareOnly -and !$InputDescription){throw 'Analog input description required'}
$root=Split-Path $PSScriptRoot -Parent
if($Timing){$Spectrum=$true}
if($Spectrum){$ZeroMarkers=$true}
if($ZeroMarkers){$Parameters=$true}
if($Parameters){$Memory=$true}
if($Memory){$Autoset=$true}
if($Autoset){$Measurements=$true}
if($Measurements){$Highspeed50M=$true}
if(($Highspeed10M -and $Highspeed25M) -or ($Highspeed10M -and $Highspeed50M) -or ($Highspeed25M -and $Highspeed50M)){throw 'Select one highspeed profile'}
if($Highspeed10M -or $Highspeed25M -or $Highspeed50M){$HardwareTrigger=$true}
if($LongTimebase -or $HardwareTrigger){$IndependentTrigger=$true}
if($IndependentTrigger){$Channels=$true}
if($Channels){$Cursors=$true}
if($Cursors){$DualChannel=$true}
. "$root/scripts/touch_uart_checks.ps1"
. "$root/scripts/adc_live_session_helpers.ps1"
. "$root/scripts/acq_ui_session_helpers.ps1"
& D:/Python/python.exe -X utf8 "$root/scripts/validate_acq_ui_build.py" --firmware-directory $FirmwareDirectory --hardware-directory $HardwareDirectory --rtl-directory $RtlDirectory --placement-file $PlacementFile
if($LASTEXITCODE){throw 'Touch build audit failed'}
$firmware=Join-Path $root $FirmwareDirectory
$proof=Get-Content "$firmware/validation.json" -Raw | ConvertFrom-Json
if($SpectrumRetest){
 if(!$Spectrum -or !$PriorSpectrumRun){throw 'Focused FFT retest requires a prior full spectrum run'}
 $previous=Get-Content "$PriorSpectrumRun/session.json" -Raw|ConvertFrom-Json
 $audited=Get-Content "$PriorSpectrumRun/spectrum-evidence-audit.json" -Raw|ConvertFrom-Json
 if($previous.state -notlike 'verified-board-*' -or $previous.controls.Count -lt 155 -or $audited.state -ne 'verified-independent-numpy-spectrum-evidence' -or $audited.spectra -ne 7){throw 'Prior full spectrum run was not independently verified'}
 foreach($pair in @(@('application.elf',$proof.elf_sha256),@('platform.bit',$proof.bit_sha256))){if((Get-FileHash "$PriorSpectrumRun/$($pair[0])").Hash -ne $pair[1]){throw 'Focused retest binary differs from full regression'}}
}
$run=$BoardRoot+'/'+(Get-Date -Format 'yyyyMMdd-HHmmss-ffff')
New-Item -ItemType Directory $run -Force | Out-Null
foreach($pair in @(@(($HardwareDirectory+'/sp2_adc_live.bit'),'platform.bit'),@("$FirmwareDirectory/scope_lvgl_freertos.elf",'application.elf'),@(($HardwareDirectory+'/ps7_init.tcl'),'ps7_init.tcl'),@('firmware/board_init_helpers.tcl','board_init_helpers.tcl'))){
 Copy-Item -LiteralPath (Join-Path $root $pair[0]) -Destination "$run/$($pair[1])"
}
foreach($p in @(@('platform.bit',$proof.bit_sha256),@('application.elf',$proof.elf_sha256),@('ps7_init.tcl',$proof.ps7_init_sha256),@('board_init_helpers.tcl',$proof.board_init_helpers_sha256))){
 if((Get-FileHash "$run/$($p[0])").Hash -ne $p[1]){throw 'Touch snapshot checksum changed'}
}
(Get-Content "$root/firmware/touch/boot.tcl" -Raw).Replace('@RUN@',$run).Replace('TOUCH_APP_RAM_LOADED','ACQ_UI_APP_RAM_LOADED') | Set-Content "$run/boot.tcl" -Encoding ascii
Copy-Item -LiteralPath "$root/scripts/adc_live_session_helpers.ps1" -Destination "$run/adc_live_session_helpers.ps1"
Copy-Item -LiteralPath "$root/scripts/analyze_adc_live_record.py" -Destination "$run/analyze_adc_live_record.py"
Copy-Item -LiteralPath "$root/scripts/acq_ui_session_helpers.ps1" -Destination "$run/acq_ui_session_helpers.ps1"
Copy-Item -LiteralPath $PSCommandPath -Destination "$run/loader.ps1"
Copy-Item -LiteralPath "$root/scripts/touch_uart_checks.ps1" -Destination "$run/touch_uart_checks.ps1"
$state=@{state='prepared-offline';run=$run;flash_written=$false;real_adc='not-run';analog_input=$InputDescription;physical_panel='awaiting-user-observation';records=@();controls=@();resume_needed=$false;
 files=@(Get-ChildItem $run -File | Get-FileHash | Select-Object Path,Hash);loader_sha256=(Get-FileHash $PSCommandPath).Hash}
$statePath="$firmware/board-session.json"
if($SpectrumRetest){$state.retest_from=$PriorSpectrumRun}
if($DualChannel){
 foreach($name in @('dual_channel_session_helpers.ps1','check_dual_frame.py','decode_board_frame.py')){Copy-Item "$root/scripts/$name" "$run/$name"}
 . "$run/dual_channel_session_helpers.ps1"
 $state.files=@(Get-ChildItem $run -File | Get-FileHash | Select-Object Path,Hash)
}
if($Cursors){
 foreach($name in @('cursor_session_helpers.ps1','check_cursor_frame.py')){Copy-Item "$root/scripts/$name" "$run/$name"}
 . "$run/cursor_session_helpers.ps1"
 $state.files=@(Get-ChildItem $run -File | Get-FileHash | Select-Object Path,Hash)
}
if($Channels){
 foreach($name in @('channel_session_helpers.ps1','check_channel_frame.py')){Copy-Item "$root/scripts/$name" "$run/$name"}
 . "$run/channel_session_helpers.ps1"
 $state.files=@(Get-ChildItem $run -File | Get-FileHash | Select-Object Path,Hash)
}
if($IndependentTrigger){
 foreach($name in @('trigger_source_session_helpers.ps1','check_trigger_source_record.py')){Copy-Item "$root/scripts/$name" "$run/$name"}
 . "$run/trigger_source_session_helpers.ps1"
 $state.files=@(Get-ChildItem $run -File | Get-FileHash | Select-Object Path,Hash)
}
if($LongTimebase -or $HardwareTrigger){
 Copy-Item "$root/scripts/long_timebase_session_helpers.ps1" "$run/long_timebase_session_helpers.ps1"
 . "$run/long_timebase_session_helpers.ps1"
 $state.files=@(Get-ChildItem $run -File | Get-FileHash | Select-Object Path,Hash)
}
if($HardwareTrigger){
 Copy-Item "$root/scripts/hardware_trigger_session_helpers.ps1" "$run/hardware_trigger_session_helpers.ps1"
 . "$run/hardware_trigger_session_helpers.ps1"
 $state.files=@(Get-ChildItem $run -File | Get-FileHash | Select-Object Path,Hash)
}
if($Highspeed10M){
 Copy-Item "$root/scripts/highspeed_10m_session_helpers.ps1" "$run/highspeed_10m_session_helpers.ps1"
 . "$run/highspeed_10m_session_helpers.ps1"
 $state.files=@(Get-ChildItem $run -File|Get-FileHash|Select-Object Path,Hash)
}
if($Highspeed25M){
 Copy-Item "$root/scripts/highspeed_25m_session_helpers.ps1" "$run/highspeed_25m_session_helpers.ps1"
 . "$run/highspeed_25m_session_helpers.ps1"
 $state.files=@(Get-ChildItem $run -File|Get-FileHash|Select-Object Path,Hash)
}
if($Highspeed50M){
 Copy-Item "$root/scripts/highspeed_50m_session_helpers.ps1" "$run/highspeed_50m_session_helpers.ps1"
 . "$run/highspeed_50m_session_helpers.ps1"
 $state.files=@(Get-ChildItem $run -File|Get-FileHash|Select-Object Path,Hash)
}
if($Measurements){
 Copy-Item "$root/scripts/measurement_session_helpers.ps1" "$run/measurement_session_helpers.ps1"
 . "$run/measurement_session_helpers.ps1"
 $state.files=@(Get-ChildItem $run -File|Get-FileHash|Select-Object Path,Hash)
}
if($Autoset){
 Copy-Item "$root/scripts/autoset_session_helpers.ps1" "$run/autoset_session_helpers.ps1"
 . "$run/autoset_session_helpers.ps1"
 $state.files=@(Get-ChildItem $run -File|Get-FileHash|Select-Object Path,Hash)
}
if($Memory){
 foreach($name in @('memory_session_helpers.ps1','record_blob.py')){Copy-Item "$root/scripts/$name" "$run/$name"}
 . "$run/memory_session_helpers.ps1"
 $state.files=@(Get-ChildItem $run -File|Get-FileHash|Select-Object Path,Hash)
}
if($Parameters){
 foreach($name in @('settings_session_helpers.ps1','settings_blob.py')){Copy-Item "$root/scripts/$name" "$run/$name"}
 . "$run/settings_session_helpers.ps1"
 $state.files=@(Get-ChildItem $run -File|Get-FileHash|Select-Object Path,Hash)
}
if($ZeroMarkers){
 Copy-Item "$root/scripts/zero_markers_session_helpers.ps1" "$run/zero_markers_session_helpers.ps1"
 . "$run/zero_markers_session_helpers.ps1"
 $state.files=@(Get-ChildItem $run -File|Get-FileHash|Select-Object Path,Hash)
}
if($Spectrum){
 foreach($name in @('spectrum_session_helpers.ps1','spectrum_reference.py')){Copy-Item "$root/scripts/$name" "$run/$name"}
 . "$run/spectrum_session_helpers.ps1"
 $state.files=@(Get-ChildItem $run -File|Get-FileHash|Select-Object Path,Hash)
}
if($Timing){
 Copy-Item "$root/scripts/timing_session_helpers.ps1" "$run/timing_session_helpers.ps1"
 . "$run/timing_session_helpers.ps1"
 $state.files=@(Get-ChildItem $run -File|Get-FileHash|Select-Object Path,Hash)
}
if($PrepareOnly){$state | ConvertTo-Json -Depth 8 | Set-Content "$firmware/prepared-session.json" -Encoding utf8;Write-Output "ACQ_UI_PREPARED_ONLY: $run";return}
if(Get-NetTCPConnection -LocalPort 3121 -State Listen -ErrorAction SilentlyContinue){throw 'Port3121 already in use; do not take ownership of another server'}
$port=[IO.Ports.SerialPort]::new('COM18',115200,[IO.Ports.Parity]::None,8,[IO.Ports.StopBits]::One)
$port.DtrEnable=$false;$port.RtsEnable=$false;$port.ReadBufferSize=65536
$captured=[Text.StringBuilder]::new();$owned=$null;$launcher=$null;$late=$null
function Pull {
 $chunk=$port.ReadExisting();if($chunk.Length){[void]$captured.Append($chunk);[IO.File]::AppendAllText("$run/uart.log",$chunk)}
 if($captured.ToString() -match 'APP_FATAL|APP_LOG_ERROR|DDR_FAIL|VIDEO_PRESENT_FAIL|VIDEO_STOP_TIMEOUT|ADC_LIVE_FAIL'){throw 'Touch session application failure; inspect UART'}
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
 if($LASTEXITCODE -or !(Select-String "$run/boot.log" -Pattern '^ACQ_UI_APP_RAM_LOADED' -Quiet)){throw 'Touch JTAG load failed'}
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
 if(!$SpectrumRetest){
 Test-AcqUiBeforeExport
 Export-AdcLiveRecords $root $run $port $captured $state
 Test-AcqUiAfterExport
 if($DualChannel){Test-DualChannelControls}
 if($Cursors){Test-CursorControls}
 if($Channels){Test-ChannelControls}
 if($IndependentTrigger){Test-IndependentTriggerControls}
 if($LongTimebase){Test-LongTimebaseControls}
 if($HardwareTrigger -and !$Highspeed10M -and !$Highspeed25M -and !$Highspeed50M){Test-HardwareTriggerControls}
 if($Highspeed10M){Test-Highspeed10MControls}
 if($Highspeed25M){Test-Highspeed25MControls}
 if($Highspeed50M){Test-Highspeed50MControls}
 if($Measurements){Test-MeasurementControls}
 if($Autoset){Test-AutosetControls}
 if($Memory){Test-MemoryControls}
 if($Parameters){Test-ParameterControls}
 if($ZeroMarkers){Test-ZeroMarkerControls}
 }else{
  # The unchanged binary has a complete independently verified full run.
  # Seed fresh volatile slots for this restart, then repeat all FFT cases.
  $state.memory_checks=@();$state.memory_sources=@();$state.memory_exports=@()
  Send-AcqCommands @('source 2','trigsource 2','trigger rise 500','triggermode auto','dual 1','cursors 0','acqrun')
  [void](Set-LongTimebase 5);$null=Wait-Highspeed50MSingle 5
  Send-AcqCommands @('memsave','paramsave')
  $null=Export-Memory 'memory-fft-retest-seed' $false
  $state.spectrum_seed=Read-ParameterSnapshot
 }
 if($Spectrum){Test-SpectrumControls}
 if($Timing){Test-TimingControls}
 $state.state='verified-board-acquisition-controls-awaiting-user-panel-feedback'
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
Write-Output "ACQ_UI_BOARD_STARTED: $($state.state); controller=$($state.controller); no Flash written"

# User-authorized temporary PL/RAM experiments; NEVER Flash.
param([ValidateSet(300,315,330)][int]$PhaseDeg=315,[string]$InputDescription='',
 [string]$PhaseDirectory='D:/FPGA_Project/SP2_Highspeed_Build/25m/phase-candidates',
 [string]$BoardRoot='D:/FPGA_Project/SP2_Highspeed_Build/25m/phase-board')
$ErrorActionPreference='Stop'
if(!$InputDescription){throw 'User-confirmed input description required'}
$root=Split-Path $PSScriptRoot -Parent
$firmware="$root/build/highspeed-25m-ui-firmware/freertos"
$hardware="$root/build/highspeed-25m-soc"
& D:/Python/python.exe -X utf8 "$root/scripts/validate_acq_ui_build.py" --firmware-directory build/highspeed-25m-ui-firmware/freertos --hardware-directory build/highspeed-25m-soc --rtl-directory build/highspeed-25m-rtl --placement-file D:/FPGA_Project/SP2_Highspeed_Build/25m/io-audit/capture-placements.csv
if($LASTEXITCODE){throw 'Nominal build audit failed'}
$proof=Get-Content "$firmware/validation.json" -Raw|ConvertFrom-Json
$manifest=Get-Content "$PhaseDirectory/manifest.json" -Raw|ConvertFrom-Json
if($manifest.state -ne 'verified-offline-phase-ECO' -or $manifest.nominal_bit_sha256 -ne $proof.bit_sha256 -or
 (Get-FileHash -LiteralPath $manifest.source_routed).Hash -ne $manifest.source_routed_sha256 -or
 (Get-FileHash "$root/scripts/make_highspeed25_phase_candidates.tcl").Hash -ne $manifest.script_sha256){throw 'Phase candidate provenance mismatch'}
$bit="$hardware/sp2_adc_live.bit";$bitHash=$proof.bit_sha256
if($PhaseDeg -ne 315){
 $entry=@($manifest.entries|Where-Object phase_deg -eq $PhaseDeg)
 if($entry.Count -ne 1){throw 'Missing unique phase candidate'}
 $bit=$entry[0].bit;$bitHash=$entry[0].bit_sha256
}
if((Get-FileHash -LiteralPath $bit).Hash -ne $bitHash){throw 'Phase bit checksum mismatch'}
if(Get-NetTCPConnection -LocalPort 3121 -State Listen -ErrorAction SilentlyContinue){throw 'Port3121 already owned by another session'}
$run=$BoardRoot+'/'+(Get-Date -Format 'yyyyMMdd-HHmmss-ffff')+"-phase$PhaseDeg"
New-Item -ItemType Directory $run -Force|Out-Null
Copy-Item -LiteralPath $bit -Destination "$run/platform.bit"
Copy-Item "$firmware/scope_lvgl_freertos.elf" "$run/application.elf"
Copy-Item "$hardware/ps7_init.tcl" "$run/ps7_init.tcl"
Copy-Item "$root/firmware/board_init_helpers.tcl" "$run/board_init_helpers.tcl"
foreach($p in @(@('platform.bit',$bitHash),@('application.elf',$proof.elf_sha256),@('ps7_init.tcl',$proof.ps7_init_sha256),@('board_init_helpers.tcl',$proof.board_init_helpers_sha256))){
 if((Get-FileHash "$run/$($p[0])").Hash -ne $p[1]){throw 'Phase snapshot checksum mismatch'}
}
(Get-Content "$root/firmware/touch/boot.tcl" -Raw).Replace('@RUN@',$run).Replace('TOUCH_APP_RAM_LOADED','PHASE_APP_RAM_LOADED')|Set-Content "$run/boot.tcl" -Encoding ascii
foreach($name in @('touch_uart_checks.ps1','acq_ui_session_helpers.ps1','long_timebase_session_helpers.ps1','hardware_trigger_session_helpers.ps1','highspeed_25m_session_helpers.ps1','analyze_adc_live_record.py','decode_board_frame.py')){
 Copy-Item "$root/scripts/$name" "$run/$name"
}
. "$run/touch_uart_checks.ps1"
. "$run/acq_ui_session_helpers.ps1"
. "$run/long_timebase_session_helpers.ps1"
. "$run/hardware_trigger_session_helpers.ps1"
. "$run/highspeed_25m_session_helpers.ps1"
$HardwareTrigger=$true;$Highspeed25M=$true;$Highspeed10M=$false
$state=@{state='prepared';run=$run;started_utc=[DateTime]::UtcNow.ToString('o');phase_deg=$PhaseDeg;analog_input=$InputDescription;known_input_hz=50000;flash_written=$false;records=@();long_records=@();controls=@();resume_needed=$false;bit_sha256=$bitHash;elf_sha256=$proof.elf_sha256;phase_manifest=$manifest;physical_panel='separate-user-feedback';files=@(Get-ChildItem $run -File|Get-FileHash|Select-Object Path,Hash)}
$port=[IO.Ports.SerialPort]::new('COM18',115200,[IO.Ports.Parity]::None,8,[IO.Ports.StopBits]::One)
$port.DtrEnable=$false;$port.RtsEnable=$false;$port.ReadBufferSize=65536
$captured=[Text.StringBuilder]::new();$owned=$null;$launcher=$null;$late=$null;$launcherTicks=$null;$launchedAt=$null
function Get-OwnedPhaseServer($candidate){
 if(!$launcher -or !$candidate -or $candidate.Name -ne 'hw_server.exe' -or $candidate.ExecutablePath -ne 'D:\Xilinx_2020.2\Vitis\2020.2\bin\unwrapped\win64.o\hw_server.exe' -or $candidate.CreationDate.ToUniversalTime() -lt $launchedAt){return $null}
 $ancestor=$candidate
 for($depth=0;$depth -lt 5 -and $ancestor;$depth++){
  if($ancestor.ParentProcessId -eq $launcher.Id){return @{server_pid=$candidate.ProcessId;parent_pid=$candidate.ParentProcessId;created_utc_ticks=$candidate.CreationDate.ToUniversalTime().Ticks}}
  $ancestor=Get-CimInstance Win32_Process -Filter "ProcessId=$($ancestor.ParentProcessId)"
 }
 return $null
}
function Pull {
 $chunk=$port.ReadExisting();if($chunk.Length){[void]$captured.Append($chunk);[IO.File]::AppendAllText("$run/uart.log",$chunk)}
 if($captured.ToString() -match 'APP_FATAL|APP_LOG_ERROR|DDR_FAIL|VIDEO_PRESENT_FAIL|VIDEO_STOP_TIMEOUT|ADC_LIVE_FAIL'){throw 'Phase application failure'}
}
try{
 $port.Open();$port.DiscardInBuffer()
 $launchedAt=[DateTime]::UtcNow
 $launcher=Start-Process -FilePath D:/Xilinx_2020.2/Vitis/2020.2/bin/hw_server.bat -ArgumentList '-s','tcp::3121' -WindowStyle Hidden -PassThru -RedirectStandardOutput "$run/server.log" -RedirectStandardError "$run/server-error.log"
 try{$launcherTicks=$launcher.StartTime.ToUniversalTime().Ticks}catch{$launcherTicks=$null}
 $watch=[Diagnostics.Stopwatch]::StartNew()
 do{
  $listen=Get-NetTCPConnection -LocalPort 3121 -State Listen -ErrorAction SilentlyContinue|Select-Object -First 1
  if(!$listen){Start-Sleep -Milliseconds 100}
  if($watch.Elapsed.TotalSeconds -gt 15){throw 'Phase hw_server startup timeout'}
 }while(!$listen)
 $p=Get-CimInstance Win32_Process -Filter "ProcessId=$($listen.OwningProcess)"
 if($p.Name -ne 'hw_server.exe' -or $p.ExecutablePath -ne 'D:\Xilinx_2020.2\Vitis\2020.2\bin\unwrapped\win64.o\hw_server.exe'){throw 'Unexpected phase server binary'}
 $owned=Get-OwnedPhaseServer $p
 if(!$owned){throw 'Refusing unrelated server'}
 $owned|ConvertTo-Json|Set-Content "$run/owned-server.json" -Encoding utf8
 & D:/Xilinx_2020.2/Vitis/2020.2/bin/xsct.bat "$run/boot.tcl" *> "$run/boot.log"
 if($LASTEXITCODE -or !(Select-String "$run/boot.log" -Pattern '^PHASE_APP_RAM_LOADED' -Quiet)){throw 'Phase temporary JTAG load failed'}
 $watch.Restart()
 do{
  Pull;$diags=@(Get-TouchReadyLines $captured.ToString())
  if($watch.Elapsed.TotalSeconds -gt 40){throw 'Phase app startup timeout'}
  if($diags.Count -lt 3){Start-Sleep -Milliseconds 50}
 }while($diags.Count -lt 3)
 foreach($d in $diags){if($d.Value -notmatch '(?:^| )locked=1(?: |$)' -or $d.Value -notmatch 'pixel_locked=1' -or $d.Value -notmatch 'dma_errors=00000000'){throw 'Phase startup video failure'}}
 if($captured.ToString() -notmatch 'ADC_LIVE_READY: EXTERNAL BANK33/J6 conversion_hz=25000000[^\r\n]*ABI=00060000'){throw 'Wrong phase app platform'}
 Send-AcqCommands @('acqstop','source 2','trigsource 2','trigger rise 500','triggermode normal','dual 1','cursors 0')
 foreach($index in @(3,4)){
  [void](Set-LongTimebase $index)
  foreach($rep in 1..3){
   $s=Wait-Highspeed25MSingle $index
   $name="phase$PhaseDeg-"+$(if($index -eq 3){'raw'}else{'cic6m25'})+"-$rep"
   Note-Acq $name $s;Export-LongRecord $name ($index -eq 3 -and $rep -eq 1)
  }
 }
 [void](Set-LongTimebase 3);Send-AcqCommands @('cursors 0','triggermode auto','dual 1','acqrun')
 $s=Wait-AcqState {param($x) $x.ui.running -eq '1' -and $x.ui.triggered -eq '1' -and $x.adc.display_rate -eq '25000000' -and $x.adc.display_epoch -eq $x.ui.epoch}
 Assert-Highspeed25MRecord $s 3 $false;Note-Acq "phase$PhaseDeg-final-RAW25M-auto-running" $s
 $state.state='verified-board-phase-records-awaiting-independent-audit'
}catch{$state.state='failed';$state.error=$_.Exception.Message;throw}
finally{
 $cleanupErrors=@()
 try{if($port.IsOpen -and $state.resume_needed){$port.Write("resume`n");Start-Sleep -Milliseconds 150}}catch{$cleanupErrors+=@($_.Exception.Message);$late=$_}
 try{if($port.IsOpen){Pull}}catch{$cleanupErrors+=@($_.Exception.Message);$late=$_}
 try{if($port.IsOpen){$port.Close()}}catch{$cleanupErrors+=@($_.Exception.Message);$late=$_}
 try{$port.Dispose()}catch{$cleanupErrors+=@($_.Exception.Message);$late=$_}
 # Stop our launcher first, so it cannot create a late listener after cleanup.
 try{
  if($launcher -and $null -ne $launcherTicks){
   $liveLauncher=Get-CimInstance Win32_Process -Filter "ProcessId=$($launcher.Id)"
   if($liveLauncher -and $liveLauncher.CreationDate.ToUniversalTime().Ticks -eq $launcherTicks){Stop-Process -Id $launcher.Id}
  }
 }catch{$cleanupErrors+=@($_.Exception.Message);$late=$_}
 try{
  if(!$owned -and $launcher){
   # A startup timeout can occur before the listener has been identified.
   # Only reclaim the exact executable born under our own launcher.
   foreach($candidate in @(Get-CimInstance Win32_Process -Filter "Name='hw_server.exe'")){
    $identity=Get-OwnedPhaseServer $candidate
    if($identity){$owned=$identity;break}
   }
  }
  if($owned){
   $p=Get-CimInstance Win32_Process -Filter "ProcessId=$($owned.server_pid)"
   if($p -and $p.Name -eq 'hw_server.exe' -and $p.ParentProcessId -eq $owned.parent_pid -and $p.CreationDate.ToUniversalTime().Ticks -eq $owned.created_utc_ticks){Stop-Process -Id $owned.server_pid;$state.server='owned-server-stopped'}
   else{$state.server='identity-changed-or-already-stopped'}
  }
 }catch{$cleanupErrors+=@($_.Exception.Message);$late=$_}
 if($cleanupErrors.Count){$state.state='failed';$state.cleanup_errors=$cleanupErrors;$state.error=$cleanupErrors -join '; '}
 try{if(Test-Path "$run/uart.log"){$state.uart_sha256=(Get-FileHash "$run/uart.log").Hash}}catch{$state.state='failed';$state.error=$_.Exception.Message;$late=$_}
 $state.completed_utc=[DateTime]::UtcNow.ToString('o')
 $state|ConvertTo-Json -Depth 12|Set-Content "$run/session.json" -Encoding utf8
}
if($late){throw $late}
Write-Output "PHASE_BOARD_PASS: $PhaseDeg degrees; $run; noFlash"

# Acquisition negative controls using UART commands; no JTAG or Flash access.
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
. "$root/scripts/acq_ui_session_helpers.ps1"
$last=Get-Content "$root/build/acq-ui-firmware/freertos/board-session.json" -Raw | ConvertFrom-Json
$run='D:/FPGA_Project/SP2_Trigger_Build/board/'+(Get-Date -Format 'yyyyMMdd-HHmmss-ffff')+'-negative'
New-Item -ItemType Directory $run -Force | Out-Null
$captured=[Text.StringBuilder]::new();$state=@{state='running';run=$run;last_jtag_run=$last.run;controls=@();mode='UART negative controls, not a positive known-signal trigger test';flash_written=$false}
$port=[IO.Ports.SerialPort]::new('COM18',115200,[IO.Ports.Parity]::None,8,[IO.Ports.StopBits]::One)
$port.DtrEnable=$false;$port.RtsEnable=$false;$port.ReadBufferSize=65536
function Pull {
 $chunk=$port.ReadExisting();if($chunk.Length){[void]$captured.Append($chunk);[IO.File]::AppendAllText("$run/uart.log",$chunk)}
 if($captured.ToString() -match 'APP_FATAL|ADC_LIVE_FAIL|VIDEO_PRESENT_FAIL|VIDEO_STOP_TIMEOUT'){throw 'Application failure during negative controls'}
}
try {
 $port.Open();$port.DiscardInBuffer()
 Send-AcqCommands @('source 2','trigsource 2','trigger rise 4500','triggermode normal','acqrun')
 $s=Wait-AcqState {param($x) $x.ui.waiting -eq '1' -and $x.ui.level_mv -eq '4500'}
 $held=$s.ui.display_id;$crc=$s.adc.display_crc;$raw=[uint32]$s.adc.id
 $s=Wait-AcqState {param($x) [uint32]$x.adc.id -gt $raw+3}
 if($s.ui.display_id -ne $held -or $s.adc.display_crc -ne $crc){throw 'NORMAL overwrote the held record'}
 Note-Acq 'normal-wait-holds-CRC-raw-advances' $s
 Send-AcqCommands @('single')
 $s=Wait-AcqState {param($x) $x.ui.single_armed -eq '1' -and $x.ui.single_complete -eq '0'};$raw=[uint32]$s.adc.id
 $s=Wait-AcqState {param($x) [uint32]$x.adc.id -gt $raw+2 -and $x.ui.single_armed -eq '1'}
 if($s.ui.display_id -ne $held -or $s.adc.display_crc -ne $crc){throw 'Waiting single overwrote held record'}
 Note-Acq 'single-remains-waiting-without-valid-trigger' $s
 Send-AcqCommands @('acqstop')
 $s=Wait-AcqState {param($x) $x.ui.running -eq '0' -and $x.ui.single_armed -eq '0'}
 $watch=[Diagnostics.Stopwatch]::StartNew();while($watch.Elapsed.TotalSeconds -lt .7){Pull;Start-Sleep -Milliseconds 50}
 $s=Read-AcqState;if($s.ui.display_id -ne $held -or $s.adc.display_crc -ne $crc){throw 'Cancel/pause did not retain record'}
 Note-Acq 'cancel-single-and-pause-holds-record' $s
 Send-AcqCommands @('trigger rise 500','triggermode auto','acqrun')
 $s=Wait-AcqState {param($x) $x.ui.running -eq '1' -and [uint32]$x.ui.display_id -gt [uint32]$held}
 Note-Acq 'resume-auto-publishes-new-record' $s
 $state.state='verified-board-negative-control-behaviors'
}catch{$state.state='failed';$state.error=$_.Exception.Message;throw}
finally{
 if($port.IsOpen){$port.Write("source 2`ntrigger rise 500`ntriggermode auto`nacqrun`n");Start-Sleep -Milliseconds 200;try{Pull}catch{$state.state='failed';$state.error=$_.Exception.Message};$port.Close()}
 $port.Dispose();if(Test-Path "$run/uart.log"){$state.uart_sha256=(Get-FileHash "$run/uart.log").Hash}
 $state|ConvertTo-Json -Depth 8|Set-Content "$run/session.json" -Encoding utf8
 $state|ConvertTo-Json -Depth 8|Set-Content "$root/build/acq-ui-firmware/freertos/negative-controls.json" -Encoding utf8
}

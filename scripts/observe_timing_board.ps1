# Read-only post-load observation; no JTAG, reset or UI commands.
param([Parameter(Mandatory=$true)][string]$Run,[int]$Seconds=31)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
. "$root/scripts/acq_ui_session_helpers.ps1"
. "$root/scripts/memory_session_helpers.ps1"
. "$root/scripts/settings_session_helpers.ps1"
. "$root/scripts/spectrum_session_helpers.ps1"
. "$root/scripts/timing_session_helpers.ps1"
$prior=Get-Content -LiteralPath "$Run/session.json" -Raw|ConvertFrom-Json
if($prior.state -notlike 'verified-board-*' -or $Seconds -lt 30){throw 'Observation requires successful candidate load and >=30seconds'}
$port=[IO.Ports.SerialPort]::new('COM18',115200,[IO.Ports.Parity]::None,8,[IO.Ports.StopBits]::One)
$port.DtrEnable=$false;$port.RtsEnable=$false;$port.ReadBufferSize=65536
$captured=[Text.StringBuilder]::new();$samples=@();$result=@{state='observing';read_only=$true;jtag_used=$false}
function Pull {
 $chunk=$port.ReadExisting();if($chunk){[void]$captured.Append($chunk);[IO.File]::AppendAllText("$Run/observation-uart.log",$chunk)}
 if($captured.ToString() -match 'APP_FATAL|APP_LOG_ERROR|ADC_LIVE_FAIL|DDR_FAIL|VIDEO_PRESENT_FAIL|VIDEO_STOP_TIMEOUT'){throw 'Observation app failure'}
}
try{
 $port.Open();$port.DiscardInBuffer();$timer=[Diagnostics.Stopwatch]::StartNew()
 $slots=Read-ParameterState;$fft=Read-SpectrumState;$first=Read-AcqState
 do{
  $s=Read-AcqState;$p=Read-ParameterState;$f=Read-SpectrumState;$t=Read-TimingState
  foreach($key in @('blob_crc','wave_crc')){if($slots.status[$key] -ne $p.status[$key]){throw 'Volatile slot changed during read-only observation'}}
  Assert-SpectrumCacheHeld $fft $f
  $samples+=@{acquisition=$s;parameter=$p;fft=$f;timing=$t}
  Pull;Start-Sleep -Milliseconds 600
 }while($timer.Elapsed.TotalSeconds -lt $Seconds)
 $last=Read-AcqState;Pull
 if($first.ui.running -ne '1' -or $last.ui.running -ne '1' -or [uint64]$last.ui.display_id -le [uint64]$first.ui.display_id){throw 'Acquisition did not advance while running'}
 $video=@([regex]::Matches($captured.ToString(),'(?m)^APP_DIAG os=freertos[^\r\n]+\r?\n'))
 if($video.Count -lt 20){throw 'Insufficient video diagnostics'}
 foreach($line in $video){if($line.Value -notmatch '(?:^| )locked=1(?: |$)' -or $line.Value -notmatch 'pixel_locked=1' -or $line.Value -notmatch 'dma_errors=00000000'){throw 'Video observation failure'}}
 [void](Invoke-AcqCommand 'touchstatus' '(?m)^TOUCH_STATUS [^\r\n]+\r?\n')
 $touch=[regex]::Matches($captured.ToString(),'(?m)^TOUCH_STATUS [^\r\n]+\r?\n')
 if(!$touch.Count -or $touch[-1].Value -notmatch '(?:^| )errors=0(?: |\r?$)'){throw 'Touch error during observation'}
 $result.state='verified-readonly-running-video-slots-fft';$result.elapsed_s=$timer.Elapsed.TotalSeconds
 $result.first=$first;$result.last=$last;$result.samples=$samples;$result.video_samples=$video.Count;$result.touch=$touch[-1].Value
 Write-Output "TIMING_OBSERVATION_PASS: $($result.elapsed_s)s;ID$($first.ui.display_id)->$($last.ui.display_id);$($samples.Count)queries;$($video.Count)video"
}catch{$result.state='failed';$result.error=$_.Exception.Message;throw}
finally{
 if($port.IsOpen){$port.Close()};$port.Dispose()
 $result|ConvertTo-Json -Depth 10|Set-Content -LiteralPath "$Run/observation.json" -Encoding utf8
}

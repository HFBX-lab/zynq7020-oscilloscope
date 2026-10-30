# Read-only UART observation of the candidate; never downloads or changes UI.
param([Parameter(Mandatory=$true)][string]$Run,[int]$Seconds=30,[switch]$Spectrum)
$ErrorActionPreference='Stop'
if($Seconds -lt 30 -or $Seconds -gt 60){throw 'Observation must be 30..60 seconds'}
$runPath=(Resolve-Path -LiteralPath $Run).Path
$session=Get-Content "$runPath/session.json" -Raw|ConvertFrom-Json
if($session.state -notlike 'verified-board-*' -or (!$session.parameter_final -and !$session.spectrum_final)){throw 'Require passed parameter/spectrum session'}
$expectedParameter=if($session.spectrum_final){$session.spectrum_final.parameter}else{$session.parameter_final.parameter}
. "$PSScriptRoot/acq_ui_session_helpers.ps1"
. "$PSScriptRoot/settings_session_helpers.ps1"
if($Spectrum){
 if(!$session.spectrum_final){throw 'Require final spectrum session'}
 . "$PSScriptRoot/spectrum_session_helpers.ps1"
}
$port=[IO.Ports.SerialPort]::new('COM18',115200);$port.DtrEnable=$false;$port.RtsEnable=$false;$port.ReadBufferSize=65536
$captured=[Text.StringBuilder]::new();$samples=@();$watch=[Diagnostics.Stopwatch]::new()
function Pull {
 $chunk=$port.ReadExisting();if($chunk){[void]$captured.Append($chunk);[IO.File]::AppendAllText("$runPath/observation-uart.log",$chunk)}
 if($captured.ToString() -match 'APP_FATAL|APP_LOG_ERROR|ADC_LIVE_FAIL|DDR_FAIL|VIDEO_PRESENT_FAIL|VIDEO_STOP_TIMEOUT'){throw 'Observed firmware error'}
}
try{
 $port.Open();$port.DiscardInBuffer();$watch.Start()
 while($watch.Elapsed.TotalSeconds -lt $Seconds){
  $s=Read-AcqState;$p=Read-ParameterState
  if($s.ui.running -ne '1' -or $s.ui.source -ne 'ADC2' -or $s.ui.time -ne '6' -or $s.adc.display_rate -ne '6250000' -or $p.status.replay -ne '0' -or $p.status.valid -ne '1' -or $p.status.wave_valid -ne '1' -or $p.status.actual_conversion -ne '50000000'){throw 'Observation final configuration drifted'}
  foreach($key in @('blob_crc','wave_crc')){if($p.status[$key] -ne $expectedParameter.status.$key){throw 'Observed saved slot changed'}}
  $sample=@{elapsed_ms=$watch.Elapsed.TotalMilliseconds;ui=$s.ui;adc=$s.adc;parameter=$p}
  if($Spectrum){
   $fft=Read-SpectrumState;$expected=$session.spectrum_final.fft
   foreach($key in @('valid','panel','seq','channel','window','remove_mean','auto_range','end_bin','rate','id','source_epoch','conversion','filter','raw_crc','bins_crc','plot_crc')){
    if($fft.status[$key] -ne $expected.status.$key){throw "Observed FFT snapshot changed $key"}
   }
   if($fft.values.running -ne '1' -or $fft.values.live_rate -ne '6250000' -or $fft.values.replay -ne '0'){throw 'FFT observation changed live context'}
   $sample.fft=$fft
  }
  $samples+=@($sample)
  Start-Sleep -Milliseconds 500
 }
 [void](Invoke-AcqCommand 'touchstatus' '(?m)^TOUCH_STATUS [^\r\n]+\r?\n');Pull
 $diags=[regex]::Matches($captured.ToString(),'(?m)^APP_DIAG [^\r\n]+')
 if($diags.Count -lt 10){throw 'Too few video diagnostics'}
 foreach($d in $diags){if($d.Value -notmatch '(?:^| )locked=1(?: |$)' -or $d.Value -notmatch 'pixel_locked=1' -or $d.Value -notmatch 'dma_errors=00000000'){throw 'Video diagnostic failure'}}
 if([uint64]$samples[-1].ui.display_id -le [uint64]$samples[0].ui.display_id){throw 'No new records during observation'}
 if($captured.ToString() -notmatch '(?m)^TOUCH_STATUS [^\r\n]*errors=0(?: |\r?$)'){throw 'Touch error or missing touch report'}
 $result=@{state='verified-readonly-observation';elapsed_ms=$watch.Elapsed.TotalMilliseconds;samples=$samples;video_diagnostics=$diags.Count;flash_written=$false;sd_used=$false}
 $result|ConvertTo-Json -Depth 10|Set-Content "$runPath/observation.json" -Encoding utf8
 Write-Output "SETTINGS_OBSERVATION_PASS: $($samples[0].ui.display_id) -> $($samples[-1].ui.display_id), samples=$($samples.Count), video=$($diags.Count)"
}finally{if($port.IsOpen){$port.Close()};$port.Dispose()}
